import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { TaskStrip } from "@/components/dash/TaskStrip";
import { SondeMap } from "@/components/dash/SondeMap";
import { ScanChart } from "@/components/dash/ScanChart";
import { LogTray } from "@/components/dash/LogTray";
import { TelemetryTable } from "@/components/dash/TelemetryTable";
import { ControlsDialog } from "@/components/dash/ControlsDialog";
import { usePasswordPrompt } from "@/components/PasswordPrompt";
import { apiGet, apiPostForm } from "@/lib/api";
import { useSocketConnected, useSocketEvent } from "@/lib/socket";
import { useCollapse } from "@/lib/collapse";
import type { LogEvent, ScanData, SondeTelemetry, TaskList } from "@/lib/types";
import { Download, AlertCircle } from "lucide-react";
import { toast } from "sonner";

const SONDE_COLORS = ["#6ee7a4", "#6ec1ff", "#f5b955", "#ff7e7e", "#c084fc", "#34d399", "#fbbf24", "#fb7185"];
const EMPTY_SCAN: ScanData = { freq: [], power: [], peak_freq: [], peak_lvl: [], threshold: 10 };

export function Dashboard() {
  const connected = useSocketConnected();
  const [tasks, setTasks] = useState<TaskList>({});
  const [scanData, setScanData] = useState<ScanData>(EMPTY_SCAN);
  const [sondes, setSondes] = useState<Record<string, SondeTelemetry>>({});
  const [logs, setLogs] = useState<LogEvent[]>([]);
  const [paused, setPaused] = useState(false);
  const pausedRef = useRef(paused);
  pausedRef.current = paused;
  const colorIdx = useRef(0);
  // Tracks which sonde ids we've already tried to hydrate the historical
  // path for from the on-disk log file. Each id is attempted at most once
  // per page load — independent of whether the fetch found anything.
  const hydratedRef = useRef<Set<string>>(new Set());
  // Pull the on-disk path for a sonde and merge it into live state. Runs at
  // most once per id per page load. Uses decim=5 to match History's call so
  // both pages share the same server-side cache entry — first request from
  // either page warms it for the other. The map only needs ~5-frame
  // resolution; finer detail just bloats the JSON without visible change.
  const hydratePath = useCallback((id: string) => {
    if (!id || hydratedRef.current.has(id)) return;
    hydratedRef.current.add(id);
    apiGet<any>(`/get_log_by_serial/${encodeURIComponent(id)}?path_only=1&decim=5`)
      .then(data => {
        const histPath: [number, number][] = (data?.path || [])
          .filter((p: any) => Array.isArray(p) && p[0] != null && p[1] != null)
          .map((p: any) => [p[0], p[1]] as [number, number]);
        if (histPath.length === 0) return;
        // Authoritative first / burst positions from the log file. SondeMap
        // reads these to plant accurate first-heard and burst markers,
        // independent of whatever the current live path happens to start /
        // peak at (which can be wrong if the archive only has a slice of
        // the flight or burst happens between path samples).
        const firstPos = Array.isArray(data?.first) ? data.first as [number, number, number] : undefined;
        const burstPos = Array.isArray(data?.burst) ? data.burst as [number, number, number] : undefined;
        const firstTime: string | undefined = data?.first_time;
        const burstTime: string | undefined = data?.burst_time;
        setSondes(prev => {
          const cur = prev[id];
          if (!cur) return prev;
          const live = (cur.path || []) as [number, number][];
          // The log file (hist) covers the entire flight from launch. The
          // backend's in-memory archive (live) may only have a recent slice
          // if auto_rx was restarted mid-flight. We want both: the early
          // ascent history from hist *prepended* to whatever live already
          // has, with no overlap.
          //
          // Find the index in hist whose lat/lon is closest to live[0] —
          // that's where the archive starts. Everything in hist BEFORE that
          // point is unique to the log and worth prepending.
          const metadata = { first_pos: firstPos, burst_pos: burstPos, first_time: firstTime, burst_time: burstTime };
          if (live.length === 0) {
            // Fresh tab, no archive — adopt the full log path.
            return { ...prev, [id]: { ...cur, ...metadata, path: histPath } };
          }
          const [lat0, lon0] = live[0];
          let bestIdx = 0;
          let bestD = Infinity;
          for (let i = 0; i < histPath.length; i++) {
            const dx = histPath[i][0] - lat0;
            const dy = histPath[i][1] - lon0;
            const d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; bestIdx = i; }
          }
          // If the closest hist point is at the very end, hist doesn't cover
          // any pre-live history — just keep live but still attach metadata.
          if (bestIdx >= histPath.length - 1) return { ...prev, [id]: { ...cur, ...metadata } };
          const merged = [...histPath.slice(0, bestIdx), ...live];
          const PATH_MAX = 4000;
          const capped = merged.length > PATH_MAX ? merged.slice(merged.length - PATH_MAX) : merged;
          return { ...prev, [id]: { ...cur, ...metadata, path: capped } };
        });
      })
      .catch(() => { /* no log file yet — fine, live path stands alone */ });
  }, []);
  const [follow, setFollow] = useState<string | null>(null);
  const [highlight, setHighlight] = useState<string | null>(null);
  // Map, Spectrum, and SDR Tasks are always-open now (no minimize button).
  // The toggleMap/toggleScan/toggleTasks setters are kept so the components
  // still have signatures that compile, but we always pass `collapsed={false}`
  // and don't wire a toggle.
  const mapOpen = true;
  const [tableOpen, toggleTable] = useCollapse("table", true);
  const [logOpen, toggleLog] = useCollapse("log", true);
  const [config, setConfig] = useState<any>({});
  const station = useMemo(() => {
    const lat = config.station_lat;
    const lon = config.station_lon;
    if (lat == null || lon == null) return null;
    if (lat === 0 && lon === 0) return null;
    return { lat, lon, alt: config.station_alt };
  }, [config]);

  // Re-fetch all REST snapshots. Called once at mount AND every time the
  // socket reconnects, so the UI catches up after an offline period.
  const pullSnapshots = useCallback(async () => {
    try { setConfig(await apiGet("/get_config")); } catch {}
    try { setTasks(await apiGet("/get_task_list")); } catch {}
    try { setScanData(await apiGet("/get_scan_data")); } catch {}
    try {
      const arch = await apiGet<Record<string, any>>("/get_telemetry_archive");
      // Merge the archive snapshot into existing state instead of clobbering
      // — pullSnapshots fires on every socket (re)connect, and naively
      // overwriting nukes any hydrated log path we'd already merged in.
      setSondes(prev => {
        const next: Record<string, SondeTelemetry> = {};
        for (const id of Object.keys(arch || {})) {
          const a = arch[id];
          if (!a?.latest_telem) continue;
          const lt = a.latest_telem;
          const fMhz =
            typeof lt.freq === "string" ? parseFloat(lt.freq)
            : typeof lt.freq_float === "number" && isFinite(lt.freq_float) ? lt.freq_float
            : typeof lt.freq === "number" && isFinite(lt.freq) ? lt.freq
            : NaN;
          const ex = prev[id];
          const archivePath: [number, number][] = (a.path || []).map((p: any) => [p[0], p[1]]);
          // Keep the existing path if it's already longer than archive (it
          // probably has the log-hydrated early flight prepended). Otherwise
          // adopt the archive — the very first pullSnapshots of a session.
          const path = (ex?.path && ex.path.length > archivePath.length) ? ex.path : archivePath;
          const t: SondeTelemetry = {
            ...(ex || {}),
            ...lt,
            id,
            freq: isFinite(fMhz) ? fMhz : (ex?.freq ?? 0),
            color: ex?.color || SONDE_COLORS[colorIdx.current++ % SONDE_COLORS.length],
            path,
            ts: a.timestamp ?? ex?.ts,
            firstSeen: ex?.firstSeen ?? a.timestamp,
          };
          next[id] = t;
        }
        return next;
      });
      // Kick off log-path hydration for every sonde we just learned about.
      // hydratePath self-guards via hydratedRef so this is a no-op for ids
      // we've already pulled from the log.
      for (const id of Object.keys(arch || {})) hydratePath(id);
    } catch {}
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  // Pull on mount; pull again on every (re)connect.
  useEffect(() => { if (connected) pullSnapshots(); }, [connected, pullSnapshots]);

  useSocketEvent<any>("telemetry_event", t => {
    if (!t || !t.id) return;
    let isFirst = false;
    // Cap per-sonde path history. A flight at ~1 frame/s for 3 hours is ~11k
    // points; keeping that fully resident plus re-rendering the polyline per
    // event blows the heap in a multi-day session. 4000 covers the full ascent
    // and descent of any sonde with detail to spare.
    const PATH_MAX = 4000;
    // Normalise frequency. Production live telemetry sends `freq` as a
    // formatted string ("404.011 MHz") and a numeric `freq_float` (MHz).
    // `freq_float` is rounded to 2 decimals server-side, so the *string*
    // actually carries the full 3-decimal measured frequency. Parse the
    // string first to keep that precision (matches the OG v1 UI behaviour);
    // fall back to freq_float / numeric freq if no string is present.
    const freqMhz =
      typeof t.freq === "string" ? parseFloat(t.freq)
      : typeof t.freq_float === "number" && isFinite(t.freq_float) ? t.freq_float
      : typeof t.freq === "number" && isFinite(t.freq) ? t.freq
      : NaN;
    setSondes(prev => {
      const ex = prev[t.id];
      if (!ex && Object.keys(prev).length === 0) isFirst = true;
      let path = ex?.path ? [...ex.path] : [];
      if (t.lat != null && t.lon != null) path.push([t.lat, t.lon]);
      if (path.length > PATH_MAX) path = path.slice(path.length - PATH_MAX);
      const nowTs = t.server_time || Date.now() / 1000;
      const next: SondeTelemetry = {
        ...(ex || {}),
        ...t,
        freq: isFinite(freqMhz) ? freqMhz : (ex?.freq ?? 0),
        color: ex?.color || SONDE_COLORS[colorIdx.current++ % SONDE_COLORS.length],
        path,
        ts: nowTs,
        firstSeen: ex?.firstSeen ?? nowTs,
      };
      return { ...prev, [t.id]: next };
    });
    // First sight of this sonde in our React state — try to hydrate the
    // pre-existing path from the on-disk log so a session restart (or this
    // tab opening mid-flight) doesn't truncate the visible track. Runs at
    // most once per id per page load.
    if (t.id) hydratePath(t.id);
    // Auto-pan to the first sonde of the session.
    if (isFirst) setFollow(t.id);
  });

  // Age-out stale sondes from the live map. The backend keeps its own
  // archive (read via /get_telemetry_archive), so dropping a sonde here just
  // removes it from the dashboard — it's still in History. Default 3 h matches
  // the backend's sonde_time_threshold default.
  useEffect(() => {
    const MAX_AGE_S = 3 * 3600;
    const id = setInterval(() => {
      const cutoff = Date.now() / 1000 - MAX_AGE_S;
      setSondes(prev => {
        let changed = false;
        const next: Record<string, SondeTelemetry> = {};
        for (const [k, v] of Object.entries(prev)) {
          if ((v.ts ?? 0) >= cutoff) next[k] = v;
          else changed = true;
        }
        return changed ? next : prev;
      });
    }, 60_000); // sweep once a minute is plenty
    return () => clearInterval(id);
  }, []);

  // Follow-timeout: if the followed sonde stops sending for >120s, release follow.
  // Read `sondes` through a ref so the interval isn't torn down/re-armed on
  // every telemetry event (which would prevent it from ever reaching 120s).
  const sondesRef = useRef(sondes);
  sondesRef.current = sondes;
  useEffect(() => {
    if (!follow) return;
    const FOLLOW_TIMEOUT_S = 120;
    const id = setInterval(() => {
      const s = sondesRef.current[follow];
      const age = s?.ts ? Date.now() / 1000 - s.ts : Infinity;
      if (age > FOLLOW_TIMEOUT_S) setFollow(null);
    }, 5000);
    return () => clearInterval(id);
  }, [follow]);

  useSocketEvent("scan_event", () => {
    apiGet<ScanData>("/get_scan_data").then(setScanData).catch(() => {});
  });
  useSocketEvent("task_event", () => {
    apiGet<TaskList>("/get_task_list").then(setTasks).catch(() => {});
  });
  useSocketEvent<LogEvent>("log_event", (l) => {
    if (pausedRef.current) return;
    setLogs(prev => {
      const ts = l.ts || (l as any).timestamp || new Date().toISOString();
      const item = { ...l, ts };
      const next = [item, ...prev];
      if (next.length > 500) next.length = 500;
      return next;
    });
  });
  // Backfill the event log tray from the server's recent-log ring buffer on
  // mount, so the panel isn't blank until a fresh log line arrives. Pauses
  // and runs once. Falls back silently if the endpoint isn't there yet
  // (e.g. user hasn't restarted auto_rx with the new web.py).
  useEffect(() => {
    apiGet<any[]>("/recent_logs?n=50")
      .then(arr => {
        if (!Array.isArray(arr) || arr.length === 0) return;
        const seeded = arr.map(l => ({
          level: l.level,
          msg: l.msg,
          ts: l.ts || l.timestamp || new Date().toISOString(),
        }));
        // Server returns oldest → newest; LogTray shows newest at top.
        seeded.reverse();
        setLogs(prev => (prev.length === 0 ? seeded : prev));
      })
      .catch(() => {});
  }, []);
  useSocketEvent<{ lat: number; lon: number; alt: number }>("station_update", p => {
    if (p && p.lat != null) setConfig((c: any) => ({ ...c, station_lat: p.lat, station_lon: p.lon, station_alt: p.alt }));
  });

  // Build the scan-list of frequencies to show as orange dots on the spectrum.
  // From station.cfg:
  //   only_scan, always_scan  → list[float]   (MHz values)
  //   always_decode           → list[[float, string]]  ([MHz, sonde_type] pairs)
  // never_scan is a flat list[float] of MHz values.
  const collectFreqs = (src: any): number[] => {
    if (!Array.isArray(src)) return [];
    const out: number[] = [];
    for (const item of src) {
      if (typeof item === "number") out.push(item);
      // always_decode entries are 2-tuples: ["mhz", "type"] — pull the freq.
      else if (Array.isArray(item) && typeof item[0] === "number") out.push(item[0]);
      else if (item && typeof item === "object") {
        // Defensive: some older docs / forks use {freq_start, freq_end} ranges.
        if (typeof item.freq_start === "number") out.push(item.freq_start);
        if (typeof item.freq_end === "number" && item.freq_end !== item.freq_start) out.push(item.freq_end);
      }
    }
    return out.filter(f => isFinite(f) && f > 0);
  };
  const scanList = useMemo<number[]>(() => [
    ...collectFreqs(config?.only_scan),
    ...collectFreqs(config?.always_scan),
    ...collectFreqs(config?.always_decode),
  ], [config]);
  const blockedList = useMemo<number[]>(() => collectFreqs(config?.never_scan), [config]);

  const sondeList = useMemo(
    () => Object.values(sondes).sort((a, b) => (b.firstSeen || b.ts || 0) - (a.firstSeen || a.ts || 0)),
    [sondes],
  );
  // Frequencies currently in active decoding (one per SDR). /get_task_list
  // reports task.freq in Hz; convert to MHz to match the spectrum chart units.
  const decodingFreqs = useMemo<number[]>(() => {
    return Object.values(tasks)
      .filter(t => t.task && t.task.indexOf("Decoding") === 0 && typeof t.freq === "number" && t.freq > 0)
      .map(t => t.freq / 1e6);
  }, [tasks]);
  const scanningCount = useMemo(() => Object.values(tasks).filter(t => t.task === "Scanning").length, [tasks]);

  // On first load, if a decode is already in progress when the page mounts,
  // pick the first decoded sonde from the telemetry list and follow it on
  // the map. Runs at most once — after that, follow tracking honors the
  // user's manual selections.
  const autoFollowedRef = useRef(false);
  useEffect(() => {
    if (autoFollowedRef.current) return;
    if (follow) { autoFollowedRef.current = true; return; }
    if (decodingFreqs.length === 0) return;
    // Match a sonde's freq (MHz) against any of the decoder centre freqs.
    // ±10 kHz tolerance — same as the spectrum chart for visual consistency.
    const TOL = 0.01;
    for (const s of sondeList) {
      const f = s.freq;
      if (!isFinite(f) || f <= 0) continue;
      if (decodingFreqs.some(d => Math.abs(d - f) <= TOL)) {
        setFollow(s.id);
        autoFollowedRef.current = true;
        return;
      }
    }
  }, [decodingFreqs, sondeList, follow]);

  const refreshTasks = useCallback(() => apiGet<TaskList>("/get_task_list").then(setTasks).catch(() => {}), []);
  const { require: requireAuth, dialog: authDialog } = usePasswordPrompt();
  const rescan = useCallback(async () => {
    const pw = await requireAuth();
    if (!pw) return; // user cancelled the prompt
    try { await apiPostForm("/rescan_now", { password: pw }); toast.success("Rescan requested"); }
    catch (e: any) { toast.error(e.message || "Failed"); }
  }, [requireAuth]);
  const refreshScan = useCallback(() => apiGet<ScanData>("/get_scan_data").then(setScanData).catch(() => {}), []);

  return (
    <div className="space-y-3 md:space-y-4">
      {/* Status strip */}
      <div className="text-[11px] mono text-muted-foreground space-y-1">
        <div className="flex items-center justify-between gap-2">
          <span className="flex items-center gap-2 min-w-0">
            <span className={`pip ${connected ? "pip-signal" : "pip-alert"}`} aria-hidden />
            <span className={connected ? "" : "text-alert"}>
              {connected ? "Telemetry link live" : "Link offline — reconnecting…"}
            </span>
          </span>
          <span className="flex items-center gap-2 shrink-0">
            <ControlsDialog rotatorEnabled={!!config.rotator_enabled} scannerActive={scanningCount > 0} tasks={tasks} onAfter={refreshTasks} />
            {/* KML uses a plain anchor styled as a button — never nest <button> in <a>. */}
            <a
              href="/rs.kml"
              title="Live KML feed"
              className="inline-flex items-center gap-1.5 h-7 px-2.5 rounded-md text-xs font-medium text-muted-foreground hover:text-foreground hover:bg-accent transition-colors"
            ><Download className="w-3 h-3" /> KML</a>
          </span>
        </div>
      </div>

      <TelemetryTable
        sondes={sondeList}
        follow={follow}
        onSelect={(id) => setFollow(prev => prev === id ? null : id)}
        station={station}
        tasks={tasks}
        highlight={highlight}
        onHighlight={setHighlight}
        collapsed={!tableOpen}
        onToggleCollapse={toggleTable}
      />

      {/* When the map is collapsed we drop the two-column grid entirely so
          the (now header-height) map panel stacks above the side rail rather
          than leaving an empty void next to it. */}
      <div className={
        mapOpen
          ? "grid grid-cols-1 lg:grid-cols-[minmax(0,1.6fr)_minmax(360px,1fr)] gap-3 md:gap-4"
          : "flex flex-col gap-3 md:gap-4"
      }>
        <div
          className={
            "rounded-md border border-border bg-card overflow-hidden flex flex-col" +
            (mapOpen ? " h-[min(60vh,640px)] lg:h-[min(65vh,680px)]" : "")
          }
        >
          <SondeMap
            sondes={sondes}
            station={station}
            follow={follow}
            highlight={highlight}
            className="flex-1"
          />
        </div>
        <div className={
          mapOpen
            // Right column matches the map's height on lg+ so the two columns
            // line up visually. The empty space lives inside the TaskStrip
            // (its wrapper is flex-1) — the Spectrum keeps its natural height.
            ? "flex flex-col gap-3 md:gap-4 min-h-0 lg:h-[min(65vh,680px)]"
            : "flex flex-col gap-3 md:gap-4 min-h-0"
        }>
          {/* TaskStrip lives in the side column at every breakpoint. On
              mobile (single-column grid) it appears under the map; on lg+ it
              stacks above the spectrum in the side column. TaskStrip itself
              decides whether to flex-1 (when open) or stay natural-height
              (when collapsed) so the column behaves predictably either way. */}
          <TaskStrip tasks={tasks} onRefresh={refreshTasks} onRescan={rescan} />
          <ScanChart
            data={scanData}
            onRefresh={refreshScan}
            scanList={scanList}
            blockedList={blockedList}
            decodingList={decodingFreqs}
            snrThreshold={typeof config?.snr_threshold === "number" ? config.snr_threshold : 10}
          />
        </div>
      </div>

      <LogTray
        logs={logs}
        onClear={() => setLogs([])}
        paused={paused}
        onPause={() => setPaused(p => !p)}
        collapsed={!logOpen}
        onToggleCollapse={toggleLog}
      />

      {sondeList.length === 0 && !connected && (
        <div className="flex items-center gap-2 text-xs text-warn bg-warn/[0.08] border border-warn/30 rounded-md px-3 py-2">
          <AlertCircle className="w-3.5 h-3.5" /> No live connection. Telemetry counts won't update until the server is reachable.
        </div>
      )}

      {authDialog}
    </div>
  );
}

