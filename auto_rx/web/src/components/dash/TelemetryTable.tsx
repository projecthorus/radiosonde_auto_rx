import { useEffect, useMemo, useState } from "react";
import { Activity, Settings2 } from "lucide-react";
import { Panel, PanelHead } from "@/components/ui/panel";
import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover";
import type { SondeTelemetry, TaskList } from "@/lib/types";
import { fmtAge, fmtAlt, fmtBearing, fmtDist, fmtFreq, fmtSpeed, fmtTemp, fmtTime, lookAngles, usePrefs } from "@/lib/units";
import { cn } from "@/lib/utils";

const ALL_COLS = [
  "sdr", "age", "id", "type", "freq", "frame",
  "lat", "lon", "alt", "vel_h", "vel_v",
  "heading", "temp", "humidity", "pressure", "snr",
  "az", "el", "range", "other", "time", "realid",
] as const;

const COL_LABELS: Record<typeof ALL_COLS[number], string> = {
  sdr: "SDR", age: "Age", id: "ID", type: "Type", freq: "Freq (MHz)", frame: "Frame",
  lat: "Lat", lon: "Lon", alt: "Alt", vel_h: "V·h", vel_v: "V·v",
  heading: "Hdg", temp: "Temp", humidity: "RH", pressure: "P (hPa)", snr: "SNR",
  az: "Az", el: "El", range: "Range", other: "Other",
  time: "Time", realid: "Real ID",
};

const DEFAULT_COLS: Record<typeof ALL_COLS[number], boolean> = {
  sdr: true, age: true, id: true, type: true, freq: true, frame: true,
  lat: true, lon: true, alt: true, vel_h: true, vel_v: true,
  heading: false, temp: true, humidity: true, pressure: false, snr: true,
  az: false, el: false, range: true, other: true,
  time: false, realid: false,
};

interface Props {
  sondes: SondeTelemetry[];
  follow?: string | null;
  onSelect?: (id: string) => void;
  station?: { lat: number; lon: number; alt?: number } | null;
  tasks?: TaskList;
  highlight?: string | null;
  onHighlight?: (id: string | null) => void;
  collapsed?: boolean;
  onToggleCollapse?: () => void;
}

export function TelemetryTable({ sondes, follow, onSelect, station, tasks, highlight, onHighlight, collapsed, onToggleCollapse }: Props) {
  // Build a small list of decoder centre frequencies (in MHz) and the SDR
  // they're running on. /get_task_list reports freq in Hz; sonde telemetry
  // freq is now stored in MHz (parsed from the "404.011 MHz" string), so we
  // normalize to MHz on the map side and do a tolerance lookup at call time.
  const decoderFreqs = useMemo(() => {
    if (!tasks) return [] as Array<{ sdr: string; mhz: number }>;
    const out: Array<{ sdr: string; mhz: number }> = [];
    for (const [sdrId, t] of Object.entries(tasks)) {
      if (typeof t.freq === "number" && t.freq > 0) out.push({ sdr: sdrId, mhz: t.freq / 1e6 });
    }
    return out;
  }, [tasks]);
  // Subscribe so the table re-renders when imperial/metric units flip.
  usePrefs();
  const [cols, setCols] = useState<Record<typeof ALL_COLS[number], boolean>>(() => {
    try { return { ...DEFAULT_COLS, ...JSON.parse(localStorage.getItem("obs.cols") || "{}") }; }
    catch { return DEFAULT_COLS; }
  });
  const setCol = (k: typeof ALL_COLS[number], v: boolean) => {
    const next = { ...cols, [k]: v };
    setCols(next);
    try { localStorage.setItem("obs.cols", JSON.stringify(next)); } catch {}
  };

  const visible = useMemo(() => ALL_COLS.filter(c => cols[c]), [cols]);

  // Pagination
  const PAGE_OPTIONS = [1, 2, 5, 10, -1] as const; // -1 = All
  const [pageSize, setPageSize] = useState<number>(() => {
    try { return parseInt(localStorage.getItem("obs.pageSize") || "10", 10) || 10; }
    catch { return 10; }
  });
  const setPageSizePersist = (n: number) => {
    setPageSize(n);
    try { localStorage.setItem("obs.pageSize", String(n)); } catch {}
  };
  const visibleSondes = useMemo(() => pageSize > 0 ? sondes.slice(0, pageSize) : sondes, [sondes, pageSize]);
  // tick state so age column updates each second
  const [, setTick] = useState(0);
  useEffect(() => {
    const t = setInterval(() => setTick(x => x + 1), 1000);
    return () => clearInterval(t);
  }, []);

  return (
    <Panel>
      <PanelHead
        title="Telemetry"
        icon={<Activity className="w-3.5 h-3.5" strokeWidth={1.75} />}
        meta={<span className="text-[10px] mono text-muted-foreground/70">{sondes.length} active</span>}
        collapsed={collapsed}
        onToggleCollapse={onToggleCollapse}
        actions={
          <>
          <label className="hidden sm:inline-flex items-center gap-1.5 text-[10px] mono text-muted-foreground/80 mr-1">
            <span>Page</span>
            <select
              value={pageSize}
              onChange={e => setPageSizePersist(parseInt(e.target.value, 10))}
              className="bg-card border border-border rounded px-1 py-0.5 text-[10px] mono"
            >
              {PAGE_OPTIONS.map(n => (
                <option key={n} value={n}>{n === -1 ? "All" : n}</option>
              ))}
            </select>
          </label>
          <Popover>
            <PopoverTrigger asChild>
              <Button size="icon-sm" variant="ghost" title="Show / hide columns">
                <Settings2 className="w-3.5 h-3.5" />
              </Button>
            </PopoverTrigger>
            <PopoverContent align="end" className="w-56">
              <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-2">Columns</div>
              <div className="grid grid-cols-2 gap-y-1.5 gap-x-3">
                {ALL_COLS.map(c => (
                  <label key={c} className="flex items-center gap-2 text-xs cursor-pointer">
                    <Checkbox checked={cols[c]} onCheckedChange={(v) => setCol(c, !!v)} />
                    <span>{COL_LABELS[c]}</span>
                  </label>
                ))}
              </div>
            </PopoverContent>
          </Popover>
          </>
        }
      />
      {collapsed ? null : sondes.length === 0 ? (
        // Compact empty state — single row inline with the icon, no big
        // vertical block, so the panel stays out of the way when the sky
        // is empty.
        <div className="px-3 py-2.5 text-[11px] mono text-muted-foreground/80 inline-flex items-center gap-2">
          <Activity className="w-3.5 h-3.5 opacity-60" />
          <span>The sky is quiet — scanner is listening.</span>
        </div>
      ) : (
        <div className="overflow-auto">
          <table className="w-full text-[11px] mono">
            <thead className="sticky top-0 z-10 bg-card/95 backdrop-blur">
              <tr className="border-b border-border">
                {visible.map(c => (
                  <th key={c} className="text-left font-medium text-[9px] uppercase tracking-widest text-muted-foreground/80 px-2.5 py-1.5 whitespace-nowrap">{COL_LABELS[c]}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {visibleSondes.map(s => (
                <tr
                  key={s.id}
                  className={cn(
                    "border-b border-border/40 hover:bg-accent/30 cursor-pointer transition-opacity",
                    follow === s.id && "bg-signal/[0.07]",
                    highlight === s.id && "bg-signal/[0.12] ring-1 ring-signal/40",
                    highlight && highlight !== s.id && "opacity-50"
                  )}
                  onClick={() => onSelect?.(s.id)}
                  onContextMenu={e => {
                    e.preventDefault();
                    onHighlight?.(highlight === s.id ? null : s.id);
                  }}
                  title="Right-click to highlight this sonde on the map"
                >
                  {visible.map(c => (
                    <td key={c} className="px-2.5 py-1.5 whitespace-nowrap">
                      {renderCell(c, s, station, decoderFreqs)}
                    </td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </Panel>
  );
}

function buildOther(s: SondeTelemetry): string {
  const parts: string[] = [];
  if (s.rs41_mainboard && s.rs41_mainboard !== "RSM412") parts.push(s.rs41_mainboard);
  if (s.bt != null && s.bt >= 0 && s.bt < 65535) {
    parts.push(`BT ${fmtTime(s.bt)}`);
  }
  if (s.batt != null) parts.push(`${s.batt.toFixed(1)} V`);
  if (s.encrypted) parts.push("Encrypted");
  return parts.join(" ");
}

function renderCell(
  c: typeof ALL_COLS[number],
  s: SondeTelemetry,
  station?: { lat: number; lon: number; alt?: number } | null,
  decoderFreqs?: Array<{ sdr: string; mhz: number }>,
) {
  switch (c) {
    case "sdr":   {
      // Match sonde MHz to the closest decoder centre within ±10 kHz —
      // mirrors the tolerance used by the spectrum's decoding-color match,
      // since /get_task_list reports a centre that may differ from the
      // sonde's measured freq by a few kHz.
      let sid: string | null = null;
      if (decoderFreqs && isFinite(s.freq) && s.freq > 0) {
        for (const d of decoderFreqs) {
          if (Math.abs(d.mhz - s.freq) <= 0.01) { sid = d.sdr; break; }
        }
      }
      return sid ? <span className="text-muted-foreground/80">{sid}</span> : "—";
    }
    case "age":   return <span className="text-muted-foreground/80">{fmtAge(s.ts)}</span>;
    case "id":    {
      // Strip the prefix (DFM/M10/etc.) for display, like the OG does
      const displayId = s.id.replace(/^(DFM|M10|M20|IMET|IMET5|IMET54|MRZ|IMS100|RS11G|MTS01|WXR)-/, "");
      const sondehubId = displayId;
      const aprsId = s.aprsid?.trim();
      return (
        <span className="inline-flex items-center gap-1.5">
          <span
            className="inline-flex items-center px-1.5 py-0.5 rounded bg-secondary border border-border"
            style={{ boxShadow: `inset 3px 0 0 ${s.color || "#6ee7a4"}` }}
          >{displayId}</span>
          <a
            href={`https://sondehub.org/${encodeURIComponent(sondehubId)}`}
            target="_blank" rel="noreferrer"
            onClick={e => e.stopPropagation()}
            title="View on SondeHub"
            className="inline-flex items-center text-muted-foreground hover:text-foreground"
          >
            <img src="/static/img/sondehub.png" alt="SondeHub" width={14} height={16} />
          </a>
          <a
            href={`https://radiosondy.info/sonde_archive.php?sondenumber=${encodeURIComponent(aprsId || displayId)}`}
            target="_blank" rel="noreferrer"
            onClick={e => e.stopPropagation()}
            title="View on Radiosondy.info"
            className="inline-flex items-center text-muted-foreground hover:text-foreground"
          >
            <img src="/static/img/radiosondy.png" alt="Radiosondy" width={17} height={16} />
          </a>
        </span>
      );
    }
    case "type":  return s.type || "—";
    case "freq":  return fmtFreq(s.freq);
    case "frame": return s.frame ?? "—";
    case "lat":   return s.lat != null ? s.lat.toFixed(4) : "—";
    case "lon":   return s.lon != null ? s.lon.toFixed(4) : "—";
    case "alt":   return fmtAlt(s.alt);
    case "vel_h": return fmtSpeed(s.vel_h);
    case "vel_v": return fmtSpeed(s.vel_v);
    case "heading": return <span className="text-muted-foreground/80">{fmtBearing(s.heading)}</span>;
    case "temp":  return fmtTemp(s.temp);
    case "humidity": return s.humidity != null ? `${Math.round(s.humidity)}%` : "—";
    case "pressure": return s.pressure != null ? s.pressure.toFixed(0) : "—";
    case "snr":   return s.snr != null ? s.snr.toFixed(1) : "—";
    case "az":    {
      const la = lookAngles(station, { lat: s.lat, lon: s.lon, alt: s.alt });
      return la ? <span className="text-muted-foreground/80">{la.az.toFixed(0)}°</span> : "—";
    }
    case "el":    {
      const la = lookAngles(station, { lat: s.lat, lon: s.lon, alt: s.alt });
      return la ? <span className="text-muted-foreground/80">{la.el.toFixed(1)}°</span> : "—";
    }
    case "range": {
      const la = lookAngles(station, { lat: s.lat, lon: s.lon, alt: s.alt });
      return la ? fmtDist(la.range_km) : "—";
    }
    case "other": return <span className="text-muted-foreground/80 truncate inline-block align-middle max-w-[14rem]" title={buildOther(s)}>{buildOther(s) || "—"}</span>;
    case "time":  return <span className="text-muted-foreground/80">{fmtTime(s.datetime)}</span>;
    case "realid": return <span className="text-muted-foreground/80">{s.id}</span>;
  }
}
