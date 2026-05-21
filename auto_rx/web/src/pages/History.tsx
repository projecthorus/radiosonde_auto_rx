import { useEffect, useMemo, useRef, useState } from "react";
import { useSearchParams } from "react-router-dom";
import L, { type LayerGroup, type Map as LMap, type Marker, type TileLayer } from "leaflet";
import { TILES, AUTO_TILE, resolveTile } from "@/lib/tiles";
import { Panel, PanelHead } from "@/components/ui/panel";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Checkbox } from "@/components/ui/checkbox";
import { Search, RefreshCcw, MapPin, Route, Download, Globe2, X, ArrowUpDown, ArrowDown, ArrowUp, Rocket, Anchor, BarChart3 } from "lucide-react";
import { SkewTPanel } from "@/components/history/SkewTPanel";
import { apiGet } from "@/lib/api";
import type { HistoricalSonde } from "@/lib/types";
import { fmtAlt, fmtDateTime, fmtDist, lookAngles, usePrefs, setPrefs, effectiveTheme } from "@/lib/units";
import { enableTwoFingerPan } from "@/lib/mapGestures";
import { loadLeafletPlugins } from "@/lib/leafletPlugins";
import { cn, escapeHtml } from "@/lib/utils";
import { toast } from "sonner";

const PATH_COLORS = ["#6ee7a4", "#6ec1ff", "#f5b955", "#ff7e7e", "#c084fc", "#34d399", "#fbbf24", "#fb7185"];

function stationIcon() {
  return L.divIcon({
    className: "",
    iconSize: [22, 22],
    iconAnchor: [11, 11],
    html: `<div style="display:flex;align-items:center;justify-content:center;width:22px;height:22px;border:2px solid hsl(152 70% 67%);border-radius:50%;background:hsl(195 18% 6% / 0.6);box-shadow:0 0 12px hsl(152 70% 67% / 0.5);">
      <div style="width:6px;height:6px;background:hsl(152 70% 67%);border-radius:50%;"></div>
    </div>`,
  });
}

function launchIcon(color: string) {
  return L.divIcon({
    className: "",
    iconSize: [16, 16],
    iconAnchor: [8, 14],
    html: `<svg width="16" height="16" viewBox="0 0 16 16" style="filter:drop-shadow(0 0 5px ${color}aa);">
      <polygon points="8,1 14,14 2,14" fill="${color}" stroke="#0c1416" stroke-width="1.5" stroke-linejoin="round"/>
    </svg>`,
  });
}
function landingIcon(color: string) {
  return L.divIcon({
    className: "",
    iconSize: [16, 16],
    iconAnchor: [8, 2],
    html: `<svg width="16" height="16" viewBox="0 0 16 16" style="filter:drop-shadow(0 0 5px ${color}aa);">
      <polygon points="8,15 14,2 2,2" fill="${color}" stroke="#0c1416" stroke-width="1.5" stroke-linejoin="round"/>
    </svg>`,
  });
}
function burstIcon() {
  // A star for the apex / burst point — orange-ish.
  return L.divIcon({
    className: "",
    iconSize: [18, 18],
    iconAnchor: [9, 9],
    html: `<svg width="18" height="18" viewBox="0 0 18 18" style="filter:drop-shadow(0 0 6px hsl(38 90% 60% / 0.7));">
      <polygon points="9,1 11,7 17,7 12,11 14,17 9,13 4,17 6,11 1,7 7,7"
        fill="hsl(38 88% 62%)" stroke="#0c1416" stroke-width="1.2" stroke-linejoin="round"/>
    </svg>`,
  });
}

/**
 * Decimate a path to at most `maxPts` vertices by even stride, while always
 * keeping the indices in `keep` (e.g. first, last, burst). Preserves visual
 * shape well enough for radiosonde flights, which are mostly near-linear
 * climbs and descents — far cheaper than full Douglas-Peucker for the gain.
 */
function simplifyKeep(
  pts: [number, number, number][],
  maxPts: number,
  keep: number[] = [],
): [number, number, number][] {
  if (pts.length <= maxPts) return pts;
  const stride = Math.ceil(pts.length / maxPts);
  const keepSet = new Set(keep.filter(i => i >= 0 && i < pts.length));
  const out: [number, number, number][] = [];
  for (let i = 0; i < pts.length; i++) {
    if (i % stride === 0 || keepSet.has(i)) out.push(pts[i]);
  }
  return out;
}

type SortKey = keyof HistoricalSonde;

export function History() {
  const [sondes, setSondes] = useState<HistoricalSonde[]>([]);
  // Allow other pages (e.g. Stats records) to deep-link straight into a
  // filtered History view by hitting /historical?q=<serial>.
  const [searchParams, setSearchParams] = useSearchParams();
  const [filter, setFilter] = useState(() => searchParams.get("q") || "");
  // Keep the URL in sync as the user edits the filter so deep links remain
  // copy-pasteable. Empty filter strips the param entirely.
  useEffect(() => {
    const cur = searchParams.get("q") || "";
    if (cur === filter) return;
    const next = new URLSearchParams(searchParams);
    if (filter) next.set("q", filter);
    else next.delete("q");
    setSearchParams(next, { replace: true });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [filter]);
  const [type, setType] = useState<string>("");
  const [sortKey, setSortKey] = useState<SortKey>("datetime");
  const [sortDir, setSortDir] = useState<1 | -1>(-1);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [busy, setBusy] = useState(false);
  const [skewtOpen, setSkewtOpen] = useState(false);
  const [skewtSerial, setSkewtSerial] = useState<string | null>(null);
  const prefs = usePrefs();

  const mapEl = useRef<HTMLDivElement>(null);
  const mapRef = useRef<LMap | null>(null);
  // Used to re-run the deep-link auto-plot effect once the map has finished
  // initialising — without this, fast clicks from Stats can win the race and
  // try to plot before layerRef is ready.
  const [mapReady, setMapReady] = useState(false);
  const layerRef = useRef<LayerGroup | null>(null);
  const tileRef = useRef<TileLayer | null>(null);
  const stationRef = useRef<Marker | null>(null);
  const didInitialCenter = useRef(false);
  const [station, setStation] = useState<{ lat: number; lon: number } | null>(null);

  const refresh = async () => {
    setBusy(true);
    try { setSondes(await apiGet<HistoricalSonde[]>("/get_log_list")); }
    catch (e: any) { toast.error("Failed to load history: " + (e.message || "")); }
    setBusy(false);
  };

  // Initial load
  useEffect(() => {
    refresh();
    (async () => {
      try {
        const cfg: any = await apiGet("/get_config");
        const lat = cfg?.station_lat, lon = cfg?.station_lon;
        if (lat != null && lon != null && !(lat === 0 && lon === 0)) setStation({ lat, lon });
      } catch {}
    })();
  }, []);

  // Station marker + initial center. Re-runs when `mapReady` flips true so
  // we still center on the station even if the station coords resolved
  // BEFORE the async leaflet plugins finished loading (the map was null on
  // first run — without the mapReady dep this effect never tried again).
  useEffect(() => {
    const m = mapRef.current;
    if (!m || !station) return;
    if (stationRef.current) {
      stationRef.current.setLatLng([station.lat, station.lon]);
    } else {
      stationRef.current = L.marker([station.lat, station.lon], { icon: stationIcon(), zIndexOffset: 1000 }).addTo(m);
    }
    if (!didInitialCenter.current) {
      didInitialCenter.current = true;
      m.setView([station.lat, station.lon], 7, { animate: false });
    }
  }, [station, mapReady]);

  // Map init — wait for the no-gap/edge-buffer Leaflet plugins so tile seams
  // don't show before the patched renderer is loaded.
  useEffect(() => {
    if (!mapEl.current || mapRef.current) return;
    let cancelled = false;
    let teardownGestures: () => void = () => {};
    loadLeafletPlugins().finally(() => {
      if (cancelled || !mapEl.current) return;
      const m = L.map(mapEl.current).setView([0, 0], 2);
      mapRef.current = m;
      layerRef.current = L.layerGroup().addTo(m);
      const t = resolveTile(prefs.tile, effectiveTheme(prefs.theme)).tile;
      tileRef.current = L.tileLayer(t.url, { ...t.opts, edgeBufferTiles: 2 } as any).addTo(m);
      teardownGestures = enableTwoFingerPan(m, mapEl.current);
      setMapReady(true);
    });
    return () => {
      cancelled = true;
      teardownGestures();
      mapRef.current?.remove();
      mapRef.current = null;
      tileRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Swap tile layer when the user picks a different provider OR the effective
  // theme changes (so "Auto" follows light/dark live).
  const effTheme = effectiveTheme(prefs.theme);
  useEffect(() => {
    const m = mapRef.current;
    if (!m) return;
    if (tileRef.current) m.removeLayer(tileRef.current);
    const t = resolveTile(prefs.tile, effTheme).tile;
    tileRef.current = L.tileLayer(t.url, t.opts).addTo(m);
  }, [prefs.tile, effTheme]);

  const types = useMemo(() => Array.from(new Set(sondes.map(s => s.type).filter(Boolean))).sort(), [sondes]);

  const filtered = useMemo(() => {
    const f = filter.toLowerCase();
    return sondes
      .filter(s => !f || s.serial?.toLowerCase().includes(f) || s.type?.toLowerCase().includes(f))
      .filter(s => !type || s.type === type)
      .sort((a, b) => {
        const av: any = a[sortKey], bv: any = b[sortKey];
        if (av == null) return 1;
        if (bv == null) return -1;
        return (av > bv ? 1 : av < bv ? -1 : 0) * sortDir;
      });
  }, [sondes, filter, type, sortKey, sortDir]);

  const toggle = (serial: string) => {
    setSelected(prev => {
      const next = new Set(prev);
      if (next.has(serial)) next.delete(serial); else next.add(serial);
      return next;
    });
  };
  const selectAll = () => setSelected(new Set(filtered.map(s => s.serial)));
  const clearSel = () => { setSelected(new Set()); layerRef.current?.clearLayers(); };

  /** Fetch /get_log_by_serial for many sondes.
   *
   *  Concurrency: the auto_rx Flask server serializes log-file parsing on the
   *  GIL, so parallel client requests give zero real speedup (we measured).
   *  Sequential keeps the Wyse from juggling threads for no benefit. The big
   *  wins come from the server-side `decim` query + the in-memory LRU cache
   *  (added in autorx/web.py) — re-clicking the same selection is ~instant.
   */
  // Fetch path points for an explicit list of serials. Extracted so the auto
  // deep-link path-plot (e.g. arriving via /historical?q=SERIAL) can drive it
  // without round-tripping through the `selected` set.
  const fetchPathsFor = async (serials: string[]) => {
    const results: { serial: string; color: string; pts: [number, number, number][]; burstTime?: string; firstTime?: string; lastTime?: string }[] = [];
    const DECIM = 5; // server returns every 5th path point — plenty for map
    let i = 0;
    for (const serial of serials) {
      try {
        // path_only=1 makes the server skip the Skew-T calculation — the
        // expensive Python trig loop that dominated cold-path time.
        const data = await apiGet<{ path: number[][]; burst_time?: string; first_time?: string; last_time?: string }>(
          `/get_log_by_serial/${encodeURIComponent(serial)}?decim=${DECIM}&path_only=1`
        );
        if (data?.path) {
          const pts = data.path.filter(p => p[0] != null && p[1] != null).map(p => [p[0], p[1], p[2] ?? 0] as [number, number, number]);
          if (pts.length) results.push({
            serial,
            color: PATH_COLORS[i % PATH_COLORS.length],
            pts,
            burstTime: data.burst_time,
            firstTime: data.first_time,
            lastTime: data.last_time,
          });
        }
      } catch {}
      i++;
    }
    return results;
  };

  // Plot a specific list of serials (used by both the manual "Tracks" button
  // and the auto deep-link from Stats). Shows toasts when `notify` is true.
  const plotSerials = async (serials: string[], notify = true) => {
    if (!serials.length) return;
    if (!layerRef.current || !mapRef.current) return;
    layerRef.current.clearLayers();
    const tId = notify ? toast.loading(`Loading ${serials.length} flight${serials.length === 1 ? "" : "s"}…`) : undefined;
    const paths = await fetchPathsFor(serials);
    if (notify) toast.success(`Plotted ${paths.length} sonde${paths.length === 1 ? "" : "s"}`, { id: tId });

    const bounds: L.LatLngExpression[] = [];
    for (const { serial, color, pts, burstTime, firstTime, lastTime } of paths) {
      // Find burst (apex) and first/last on the FULL path so the markers
      // sit at true positions even after we thin the line for rendering.
      let apex = 0;
      for (let j = 1; j < pts.length; j++) if (pts[j][2] > pts[apex][2]) apex = j;

      // Simplify the line: cap at ~600 visible vertices per sonde via even
      // stride. Always keep first, last, and the burst index so the polyline
      // still passes through them.
      const MAX_PTS = 600;
      const simplified = simplifyKeep(pts, MAX_PTS, [0, apex, pts.length - 1]);
      const ll = simplified.map(p => [p[0], p[1]] as [number, number]);

      // smoothFactor: extra rendering-time generalisation. Default is 1.0;
      // 2.0 drops sub-2px deviations without visibly changing flight shape.
      L.polyline(ll as any, { color, weight: 2.5, opacity: 0.85, smoothFactor: 2.0 }).addTo(layerRef.current!);

      // Markers anchor to original (un-simplified) positions. Use the times
      // returned by /get_log_by_serial (first_time, burst_time, last_time)
      // for the popups — formatted with the user's TZ + 12/24h prefs. Fall
      // back to the quicklook from /get_log_list if those weren't returned.
      const sondeRow = sondes.find(s => s.serial === serial);
      const firstIso = firstTime || sondeRow?.first?.datetime;
      const lastIso  = lastTime  || sondeRow?.last?.datetime;
      const firstTs = firstIso ? `<br>${fmtDateTime(firstIso)}` : "";
      const lastTs  = lastIso  ? `<br>${fmtDateTime(lastIso)}`  : "";
      const burstTs = burstTime ? `<br>${fmtDateTime(burstTime)}` : "";
      const safeSerial = escapeHtml(serial);
      L.marker([pts[0][0], pts[0][1]], { icon: launchIcon(color) }).bindPopup(`<b>${safeSerial}</b><br>first heard${firstTs}`).addTo(layerRef.current!);
      L.marker([pts[pts.length - 1][0], pts[pts.length - 1][1]], { icon: landingIcon(color) }).bindPopup(`<b>${safeSerial}</b><br>last heard${lastTs}`).addTo(layerRef.current!);
      if (apex > 0 && apex < pts.length - 1) {
        L.marker([pts[apex][0], pts[apex][1]], { icon: burstIcon() })
          .bindPopup(`<b>${safeSerial}</b><br>burst @ ${fmtAlt(pts[apex][2])}${burstTs}`)
          .addTo(layerRef.current!);
      }
      ll.forEach(p => bounds.push(p));
    }
    if (bounds.length) mapRef.current.fitBounds(bounds as any, { padding: [40, 40] });
  };

  // Wrapper used by the manual "Tracks" button — drives plotSerials from the
  // user's current selection.
  const plotPaths = () => {
    if (!selected.size) { toast.warning("Select sondes first"); return; }
    return plotSerials(Array.from(selected));
  };

  // Auto-plot a single sonde when arriving via ?q=<exact-serial>. Fires once
  // per `q` value AFTER both the sonde list AND the map are ready — without
  // gating on mapReady, fast clicks from Stats can race the map init,
  // silently no-op in plotSerials (layerRef null), and never retry.
  const autoPlottedRef = useRef<string | null>(null);
  useEffect(() => {
    const q = searchParams.get("q");
    if (!q || sondes.length === 0) return;
    if (!mapReady || !layerRef.current) return;
    if (autoPlottedRef.current === q) return;
    const match = sondes.find(s => s.serial === q);
    if (!match) return;
    autoPlottedRef.current = q;
    setSelected(new Set([q]));
    void plotSerials([q], false);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sondes, searchParams, mapReady]);

  /** "First heard" / "Last heard" — these are the first and last positions
   *  we managed to decode, NOT necessarily the launch site or landing point
   *  (sondes are usually launched out of range and stop transmitting before
   *  hitting the ground). Both are precomputed in the HistoricalSonde rows
   *  from /get_log_list — no per-sonde fetch needed. Operates on the selection
   *  if any, otherwise the full filtered list.
   */
  const plotPointsOnly = (which: "first" | "last") => {
    if (!layerRef.current || !mapRef.current) return;
    const targets = selected.size
      ? filtered.filter(s => selected.has(s.serial))
      : filtered;
    if (!targets.length) return toast.warning("No sondes to plot");
    layerRef.current.clearLayers();
    const bounds: L.LatLngExpression[] = [];
    let i = 0;
    const label = which === "first" ? "first heard" : "last heard";
    for (const s of targets) {
      const p = which === "first" ? s.first : s.last;
      if (!p || p.lat == null || p.lon == null) continue;
      const color = PATH_COLORS[i++ % PATH_COLORS.length];
      const ll: [number, number] = [p.lat, p.lon];
      const icon = which === "first" ? launchIcon(color) : landingIcon(color);
      const ts = p.datetime ? `<br>${fmtDateTime(p.datetime)}` : "";
      L.marker(ll, { icon }).bindPopup(`<b>${escapeHtml(s.serial)}</b><br>${label}${ts}`).addTo(layerRef.current!);
      bounds.push(ll);
    }
    if (bounds.length) mapRef.current.fitBounds(bounds as any, { padding: [40, 40] });
  };

  /**
   * Build the station's "reception range" polygon, the way the OG UI does:
   * for every selected sonde (or all filtered sondes if none selected) take
   * the first and last RX points, compute their bearing+range from the
   * station, bucket by 2° of bearing, keep the max range per bucket, fill in
   * empty buckets with the station coords, then connect the dots. The result
   * is a spiky range ring showing how far you've heard sondes in each
   * direction.
   */
  const plotCoverage = () => {
    if (!layerRef.current || !mapRef.current) return;
    if (!station) return toast.warning("Set your station lat/lon in Settings first");
    const targets = selected.size
      ? filtered.filter(s => selected.has(s.serial))
      : filtered;
    if (!targets.length) return toast.warning("No sondes to plot");

    layerRef.current.clearLayers();
    const BUCKET = 2; // degrees
    type Slot = { range: number; lat: number; lon: number };
    const buckets = new Map<number, Slot>();

    const consider = (lat: number, lon: number) => {
      const la = lookAngles(station, { lat, lon, alt: 0 });
      if (!la) return;
      const b = Math.round(la.az / BUCKET) * BUCKET;
      const existing = buckets.get(b);
      if (!existing || la.range_km > existing.range) {
        buckets.set(b, { range: la.range_km, lat, lon });
      }
    };

    // No per-sonde fetch — the quicklook `first` and `last` from /get_log_list
    // already carry lat/lon for both ends of each flight.
    for (const s of targets) {
      if (s.first?.lat != null && s.first?.lon != null) consider(s.first.lat, s.first.lon);
      if (s.last?.lat != null && s.last?.lon != null) consider(s.last.lat, s.last.lon);
    }

    if (!buckets.size) return toast.warning("No coverage points found");

    // Fill empty bearings with the station position so the polygon collapses
    // back to the origin where we've never heard a sonde.
    const polyLatLng: [number, number][] = [];
    for (let b = 0; b <= 360; b += BUCKET) {
      const s = buckets.get(b === 360 ? 0 : b);
      polyLatLng.push(s ? [s.lat, s.lon] : [station.lat, station.lon]);
    }

    L.polygon(polyLatLng as any, {
      color: "hsl(38 88% 62%)",
      weight: 2,
      fillColor: "hsl(38 88% 62%)",
      fillOpacity: 0.08,
    }).bindPopup(`<b>Reception coverage</b><br>${buckets.size}/${360 / BUCKET} bearings, from ${targets.length} sondes`).addTo(layerRef.current);

    // Station marker as the centre of the ring.
    L.circleMarker([station.lat, station.lon], {
      radius: 4, color: "hsl(152 70% 67%)", weight: 2, fillColor: "hsl(152 70% 67%)", fillOpacity: 1,
    }).addTo(layerRef.current);

    mapRef.current.fitBounds(polyLatLng as any, { padding: [40, 40] });
  };

  const dl = (route: "logs" | "kml") => {
    if (!selected.size) {
      if (route === "kml") return window.open("/generate_kml", "_blank");
      return window.open("/export_all_log_files", "_blank");
    }
    if (selected.size > 50 && !confirm(
      `You're about to ${route === "kml" ? "generate KML for" : "download logs from"} ${selected.size} sondes. ` +
      `This can take a while and produce a large file. Continue?`
    )) return;
    // btoa() only accepts Latin-1; serials are ASCII today but may not always
    // be. Encode to UTF-8 bytes first, then to base64.
    const json = JSON.stringify(Array.from(selected));
    const bytes = new TextEncoder().encode(json);
    let bin = "";
    for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
    const b64 = btoa(bin);
    if (route === "logs") window.open(`/export_log_files/${b64}`, "_blank");
    else window.open(`/generate_kml/${b64}`, "_blank");
  };

  const sortBtn = (key: SortKey, label: string) => (
    <button type="button"
      className="inline-flex items-center gap-1 hover:text-foreground transition-colors"
      onClick={() => { if (sortKey === key) setSortDir(d => (d * -1) as 1 | -1); else { setSortKey(key); setSortDir(-1); } }}
    >
      {label}
      {sortKey === key ? (sortDir === -1 ? <ArrowDown className="w-3 h-3" /> : <ArrowUp className="w-3 h-3" />) : <ArrowUpDown className="w-3 h-3 opacity-40" />}
    </button>
  );

  return (
    // On lg+ pin the page height to viewport minus the AppShell header
    // (3rem) and main's py padding (1rem top + 1rem bottom). Using an
    // explicit calc instead of h-full because main is display:block and
    // h-full doesn't reliably propagate flex-derived heights to children
    // in that case. Below lg the grid stacks and we let it grow naturally.
    <div className="lg:h-[calc(100vh-5rem)] flex flex-col gap-3 md:gap-4">
      <div className="flex items-end justify-between gap-3 flex-wrap">
        <div>
          <h1 className="text-base font-semibold">Sonde archive</h1>
          <p className="text-xs text-muted-foreground">Past radiosondes received by this station</p>
        </div>
        <div className="flex items-center gap-2">
          <span className="text-[11px] mono text-muted-foreground">{sondes.length} total · {selected.size} selected</span>
          <Button size="sm" variant="default" onClick={refresh} disabled={busy}><RefreshCcw className="w-3 h-3" /> Refresh</Button>
        </div>
      </div>

      {/* Toolbar */}
      <Panel className="overflow-visible">
        <div className="flex flex-wrap items-center gap-2 p-2.5">
          <div className="relative flex-1 min-w-[180px]">
            <Search className="absolute left-2 top-1/2 -translate-y-1/2 w-3 h-3 text-muted-foreground/70" />
            <Input type="search" placeholder="Filter by serial or type…" value={filter} onChange={e => setFilter(e.target.value)} className="pl-7" />
          </div>
          <div className="w-36">
            <Select value={type || "_all"} onValueChange={v => setType(v === "_all" ? "" : v)}>
              <SelectTrigger><SelectValue placeholder="All types" /></SelectTrigger>
              <SelectContent>
                <SelectItem value="_all">All types</SelectItem>
                {types.map(t => <SelectItem key={t} value={t}>{t}</SelectItem>)}
              </SelectContent>
            </Select>
          </div>
          <div className="flex-1" />
          {/* `X selected / Clear` lives on the Sondes panel header below. */}
        </div>
      </Panel>

      <div className="grid grid-cols-1 lg:grid-cols-[minmax(0,1fr)_minmax(0,1.3fr)] gap-3 lg:flex-1 lg:min-h-0">
        {/* List */}
        <Panel className="min-h-0">
          <PanelHead
            title="Sondes"
            meta={<span className="text-[10px] mono text-muted-foreground">{filtered.length} results</span>}
            actions={selected.size > 0 ? (
              <>
                <span className="text-[10px] mono text-muted-foreground">{selected.size} selected</span>
                <Button size="icon-sm" variant="ghost" onClick={clearSel} aria-label="Clear selection" title="Clear selection"><X className="w-3 h-3" /></Button>
              </>
            ) : undefined}
          />
          <div className="overflow-auto max-h-[60vh] lg:max-h-none lg:flex-1 lg:min-h-0">
            <table className="w-full text-[11px] mono">
              <thead className="sticky top-0 z-10 bg-card/95 backdrop-blur">
                <tr className="border-b border-border text-[9px] uppercase tracking-widest text-muted-foreground/80">
                  <th className="px-2 py-1.5 w-6">
                    <Checkbox
                      aria-label="Select all rows"
                      checked={filtered.length > 0 && selected.size === filtered.length ? true : selected.size > 0 ? "indeterminate" : false}
                      onCheckedChange={v => v ? selectAll() : clearSel()}
                    />
                  </th>
                  <th className="px-2 py-1.5 text-left">{sortBtn("datetime", "Date")}</th>
                  <th className="px-2 py-1.5 text-left">{sortBtn("type", "Type")}</th>
                  <th className="px-2 py-1.5 text-left">{sortBtn("serial", "Serial")}</th>
                  <th className="px-2 py-1.5 text-left">{sortBtn("freq", "Freq")}</th>
                  <th className="px-2 py-1.5 text-right">{sortBtn("lines", "Frames")}</th>
                  <th className="px-2 py-1.5 text-right">{sortBtn("min_height", "Last alt")}</th>
                  <th className="px-2 py-1.5 text-right">{sortBtn("max_range", "Range")}</th>
                </tr>
              </thead>
              <tbody>
                {filtered.length === 0 ? (
                  <tr><td colSpan={8} className="text-center py-6 text-muted-foreground/60">No sondes match.</td></tr>
                ) : filtered.map(s => (
                  <tr key={s.serial} onClick={() => toggle(s.serial)} className={cn("border-b border-border/40 hover:bg-accent/30 cursor-pointer", selected.has(s.serial) && "bg-signal/[0.06]")}>
                    <td className="px-2 py-1.5"><Checkbox checked={selected.has(s.serial)} onClick={e => e.stopPropagation()} onCheckedChange={() => toggle(s.serial)} /></td>
                    <td className="px-2 py-1.5 text-muted-foreground/80">{fmtDateTime(s.datetime)}</td>
                    <td className="px-2 py-1.5">{s.type}</td>
                    <td className="px-2 py-1.5"><span className="inline-flex items-center px-1.5 py-0.5 rounded bg-secondary border border-border">{s.serial}</span></td>
                    {/* Production /get_log_list returns freq in MHz, so format from Hz equivalent: */}
                    <td className="px-2 py-1.5">{s.freq?.toFixed(3)}</td>
                    <td className="px-2 py-1.5 text-right">{s.lines ?? "—"}</td>
                    <td className="px-2 py-1.5 text-right">{fmtAlt(s.min_height)}</td>
                    <td className="px-2 py-1.5 text-right">{fmtDist(s.max_range)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Panel>

        {/* Map */}
        <Panel className="overflow-hidden min-h-0">
          <PanelHead title="Map" icon={<MapPin className="w-3.5 h-3.5" strokeWidth={1.75} />}
            actions={
              <>
                <div className="hidden md:block w-32 mr-1">
                  <Select value={prefs.tile} onValueChange={v => setPrefs({ tile: v })}>
                    <SelectTrigger className="h-7 text-[11px]"><SelectValue /></SelectTrigger>
                    <SelectContent>
                      {[AUTO_TILE, ...Object.keys(TILES)].map(n => <SelectItem key={n} value={n}>{n}</SelectItem>)}
                    </SelectContent>
                  </Select>
                </div>
                <Button size="sm" variant="default" disabled={!selected.size} onClick={plotPaths} title="Full flight paths with first-heard / burst / last-heard markers (requires fetching log files — slower)"><Route className="w-3 h-3" /> Tracks</Button>
                <Button size="sm" variant="default" onClick={() => plotPointsOnly("first")} title={selected.size ? "First decoded position of each selected sonde" : "First decoded position of every filtered sonde"}><Rocket className="w-3 h-3" /> First heard</Button>
                <Button size="sm" variant="default" onClick={() => plotPointsOnly("last")} title={selected.size ? "Last decoded position of each selected sonde" : "Last decoded position of every filtered sonde"}><Anchor className="w-3 h-3" /> Last heard</Button>
                <Button size="sm" variant="default" onClick={plotCoverage} title={selected.size ? "Reception range polygon from selected sondes" : "Reception range polygon from all filtered sondes"}><Globe2 className="w-3 h-3" /> Coverage</Button>
                <Button
                  size="sm"
                  variant="default"
                  disabled={selected.size !== 1}
                  onClick={() => {
                    const only = Array.from(selected)[0];
                    if (only) { setSkewtSerial(only); setSkewtOpen(true); }
                  }}
                  title={selected.size === 1 ? "Plot Skew-T for selected sonde" : "Select exactly one sonde to plot Skew-T"}
                ><BarChart3 className="w-3 h-3" /> Skew-T</Button>
                <Button size="sm" variant="default" disabled={!selected.size && !sondes.length} onClick={() => dl("logs")}><Download className="w-3 h-3" /> Logs</Button>
                <Button size="sm" variant="default" disabled={!selected.size && !sondes.length} onClick={() => dl("kml")}><Download className="w-3 h-3" /> KML</Button>
              </>
            }
          />
          <div ref={mapEl} className="h-[60vh] lg:h-auto lg:flex-1 lg:min-h-[300px]" />
        </Panel>
      </div>
      <SkewTPanel open={skewtOpen} serial={skewtSerial} onOpenChange={setSkewtOpen} />
    </div>
  );
}
