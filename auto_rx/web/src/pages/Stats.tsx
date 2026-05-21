import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Link } from "react-router-dom";
import { Panel, PanelHead } from "@/components/ui/panel";
import { Button } from "@/components/ui/button";
import { BarChart3, Compass, Ban, RefreshCcw, CheckCircle2, Award, TrendingUp, MapPin, Clock, Radio, Mountain } from "lucide-react";
import { apiGet } from "@/lib/api";
import { toast } from "sonner";
import type { BlockEntry, HistoricalSonde, RotatorStatus } from "@/lib/types";
import { fmtAlt, fmtDist, fmtTime, usePrefs } from "@/lib/units";

/** A compact key-value with optional accent. Re-used across the page. */
function Metric({ label, value, accent, className = "" }: { label: string; value: React.ReactNode; accent?: "primary" | "warn" | "alert"; className?: string }) {
  return (
    <div className={`flex flex-col gap-0.5 ${className}`}>
      <span className="text-[9px] uppercase tracking-widest text-muted-foreground/80 font-semibold">{label}</span>
      <span className={`mono text-base font-semibold leading-none ${accent === "primary" ? "text-signal" : accent === "warn" ? "text-warn" : accent === "alert" ? "text-alert" : "text-foreground"}`}>{value}</span>
    </div>
  );
}

function blockCountdown(b: BlockEntry) {
  if (!b.until) return "permanent";
  const sec = Math.max(0, b.until - Date.now() / 1000);
  if (sec >= 3600) return `${Math.floor(sec / 3600)}h ${Math.floor((sec % 3600) / 60)}m`;
  if (sec >= 60) return `${Math.floor(sec / 60)}m ${Math.floor(sec % 60)}s`;
  return `${Math.floor(sec)}s`;
}

function fmtDuration(sec: number): string {
  if (!isFinite(sec) || sec < 0) return "—";
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  if (h >= 1) return `${h}h ${String(m).padStart(2, "0")}m`;
  const s = Math.floor(sec % 60);
  if (m >= 1) return `${m}m ${String(s).padStart(2, "0")}s`;
  return `${s}s`;
}

function Compass2D({ az, target }: { az: number; target?: number | null }) {
  const ticks = Array.from({ length: 36 }, (_, i) => i * 10);
  return (
    <svg viewBox="0 0 200 200" className="w-44 h-44 flex-shrink-0">
      <defs>
        <radialGradient id="cg" cx="50%" cy="50%" r="50%">
          <stop offset="0%" stopColor="hsl(195 18% 8%)" />
          <stop offset="100%" stopColor="hsl(195 18% 4%)" />
        </radialGradient>
      </defs>
      <circle cx="100" cy="100" r="92" fill="url(#cg)" stroke="hsl(196 20% 22%)" strokeWidth="1" />
      <circle cx="100" cy="100" r="78" fill="none" stroke="hsl(196 20% 16%)" strokeWidth="1" strokeDasharray="2 4" />
      {ticks.map(deg => {
        const cardinal = deg % 90 === 0;
        const major = deg % 30 === 0;
        const r1 = cardinal ? 80 : major ? 84 : 88;
        const rad = (deg - 90) * Math.PI / 180;
        return (
          <line key={deg}
            x1={100 + r1 * Math.cos(rad)} y1={100 + r1 * Math.sin(rad)}
            x2={100 + 92 * Math.cos(rad)} y2={100 + 92 * Math.sin(rad)}
            stroke={cardinal ? "hsl(195 8% 60%)" : major ? "hsl(196 20% 30%)" : "hsl(196 20% 22%)"}
            strokeWidth={cardinal ? 2 : 1}
          />
        );
      })}
      {(["N", "E", "S", "W"] as const).map((d, i) => {
        const a = (i * 90 - 90) * Math.PI / 180;
        return <text key={d} x={100 + 70 * Math.cos(a)} y={100 + 70 * Math.sin(a) + 4} textAnchor="middle" fill="hsl(195 8% 75%)" fontSize="11" fontFamily="'Plex Mono',monospace" fontWeight="600">{d}</text>;
      })}
      {target != null && (
        <g transform={`rotate(${target} 100 100)`}>
          <line x1="100" y1="100" x2="100" y2="20" stroke="hsl(38 88% 62%)" strokeWidth="2" opacity="0.75" />
          <circle cx="100" cy="20" r="3" fill="hsl(38 88% 62%)" />
        </g>
      )}
      <g transform={`rotate(${az || 0} 100 100)`}>
        <line x1="100" y1="100" x2="100" y2="22" stroke="hsl(152 70% 67%)" strokeWidth="2.5" />
        <polygon points="96,28 100,18 104,28" fill="hsl(152 70% 67%)" />
      </g>
      <circle cx="100" cy="100" r="6" fill="hsl(196 18% 11%)" stroke="hsl(196 30% 35%)" strokeWidth="1" />
    </svg>
  );
}

function ElevBar({ el, target }: { el: number; target?: number | null }) {
  const fill = Math.max(0, Math.min(90, el)) / 90;
  return (
    <div className="relative w-12 h-44 rounded-md border border-border bg-background/60 overflow-hidden flex-shrink-0">
      <div className="absolute inset-x-0 bottom-0 bg-gradient-to-t from-signal-dim to-signal" style={{ height: `${fill * 100}%`, opacity: 0.55 }} />
      {target != null && (
        <div className="absolute inset-x-0 h-0.5 bg-warn shadow-[0_0_6px_hsl(38_88%_62%/0.6)]" style={{ bottom: `${Math.max(0, Math.min(90, target)) / 90 * 100}%` }} />
      )}
      <div className="absolute inset-0 flex flex-col justify-between py-1 pointer-events-none">
        {[90, 60, 30, 0].map(t => <span key={t} className="text-[9px] mono text-muted-foreground/70 text-right pr-1.5">{t}°</span>)}
      </div>
    </div>
  );
}

/* ---------------------------------------------------------------------------
 * STATS COMPUTATION
 * Everything below is derived client-side from /get_log_list — no extra API
 * calls needed. Memoised so we don't recompute on every render.
 * ------------------------------------------------------------------------- */

interface Stats {
  total: number;
  in24h: number;
  in7d: number;
  in30d: number;
  earliest: number | null;
  latest: number | null;
  byType: Array<{ type: string; count: number }>;
  rangeRecord: { serial: string; type: string; km: number; date: string } | null;
  highestSeen: { serial: string; type: string; alt: number; date: string } | null;
  longestFlight: { serial: string; type: string; lines: number; date: string } | null;
  longestFlightByTime: { serial: string; type: string; durationSec: number; date: string } | null;
  bearingHistogram: number[]; // 16 bins (22.5° each)
  topBearingLabel: string;
  daily: Array<{ date: string; count: number }>; // last 30 days
  byHour: number[]; // 24-bin hour-of-day launches
  mostBusyHour: number;
  longestStreak: number; // max consecutive days with ≥1 sonde, in chosen TZ
  currentStreak: number; // consecutive days ending today with ≥1 sonde
  averageRangeKm: number;
}

function computeStats(rows: HistoricalSonde[], utc: boolean): Stats {
  const now = Date.now();
  const ts = (s: HistoricalSonde) => {
    const t = new Date(s.datetime || "").getTime();
    return isFinite(t) ? t : 0;
  };
  let in24h = 0, in7d = 0, in30d = 0;
  let earliest: number | null = null, latest: number | null = null;
  const byTypeMap = new Map<string, number>();
  const bearingBins = new Array(16).fill(0);
  const dailyMap = new Map<string, number>();
  // All dates (YYYY-MM-DD in chosen TZ) with ≥1 sonde — covers the entire
  // archive, not just the 30-day window. Used for streak calculations.
  const allDates = new Set<string>();
  const byHour = new Array(24).fill(0);
  let rangeRecord: Stats["rangeRecord"] = null;
  let highestSeen: Stats["highestSeen"] = null;
  let longestFlight: Stats["longestFlight"] = null;
  let longestFlightByTime: Stats["longestFlightByTime"] = null;
  let rangeSum = 0, rangeCount = 0;

  for (const s of rows) {
    const t = ts(s);
    if (t) {
      if (earliest == null || t < earliest) earliest = t;
      if (latest == null || t > latest) latest = t;
    }
    const ageMs = now - t;
    if (ageMs < 86400_000) in24h++;
    if (ageMs < 7 * 86400_000) in7d++;
    if (ageMs < 30 * 86400_000) in30d++;

    byTypeMap.set(s.type, (byTypeMap.get(s.type) || 0) + 1);

    // Bearing of the launch (first quicklook). Bucket into 22.5° wedges.
    if (s.first && isFinite(s.first.bearing)) {
      const b = ((s.first.bearing % 360) + 360) % 360;
      bearingBins[Math.floor(b / 22.5) % 16]++;
    }

    // Bucket every sonde by calendar day in the user's chosen time zone.
    // dailyMap is the last-30 strip; allDates is the full archive for streaks.
    if (t) {
      const d = new Date(t);
      const y = utc ? d.getUTCFullYear() : d.getFullYear();
      const mo = utc ? d.getUTCMonth() : d.getMonth();
      const da = utc ? d.getUTCDate() : d.getDate();
      const key = `${y}-${String(mo + 1).padStart(2, "0")}-${String(da).padStart(2, "0")}`;
      allDates.add(key);
      if (ageMs < 30 * 86400_000) dailyMap.set(key, (dailyMap.get(key) || 0) + 1);
    }

    // Hour-of-day launch pattern, again in the chosen TZ.
    if (t) byHour[(utc ? new Date(t).getUTCHours() : new Date(t).getHours())]++;

    if (typeof s.max_range === "number" && isFinite(s.max_range)) {
      rangeSum += s.max_range; rangeCount++;
      if (!rangeRecord || s.max_range > rangeRecord.km) {
        rangeRecord = { serial: s.serial, type: s.type, km: s.max_range, date: s.datetime };
      }
    }
    // No max-altitude in /get_log_list (only first.alt and last.alt = landing).
    // Best we can do is the launch altitude — useful as "highest takeoff" stat.
    const alt = s.first?.alt;
    if (typeof alt === "number" && isFinite(alt)) {
      if (!highestSeen || alt > highestSeen.alt) {
        highestSeen = { serial: s.serial, type: s.type, alt, date: s.datetime };
      }
    }
    if (typeof s.lines === "number" && (!longestFlight || s.lines > longestFlight.lines)) {
      longestFlight = { serial: s.serial, type: s.type, lines: s.lines, date: s.datetime };
    }
    // Wall-clock duration of the flight = last - first telemetry timestamp.
    // Falls back to `s.datetime` (first frame) on the start side if no
    // explicit `first.datetime` is reported by the backend.
    const startIso = s.first?.datetime || s.datetime;
    const endIso = s.last?.datetime;
    const startMs = startIso ? Date.parse(startIso) : NaN;
    const endMs = endIso ? Date.parse(endIso) : NaN;
    if (isFinite(startMs) && isFinite(endMs) && endMs > startMs) {
      const durationSec = (endMs - startMs) / 1000;
      if (!longestFlightByTime || durationSec > longestFlightByTime.durationSec) {
        longestFlightByTime = { serial: s.serial, type: s.type, durationSec, date: s.datetime };
      }
    }
  }

  const byType = Array.from(byTypeMap.entries())
    .map(([type, count]) => ({ type, count }))
    .sort((a, b) => b.count - a.count);

  // Build the last-30-days strip with empty days filled in (TZ matches the
  // bucketing above so labels line up).
  const daily: Array<{ date: string; count: number }> = [];
  for (let d = 29; d >= 0; d--) {
    const day = new Date(now - d * 86400_000);
    const y = utc ? day.getUTCFullYear() : day.getFullYear();
    const mo = utc ? day.getUTCMonth() : day.getMonth();
    const da = utc ? day.getUTCDate() : day.getDate();
    const key = `${y}-${String(mo + 1).padStart(2, "0")}-${String(da).padStart(2, "0")}`;
    daily.push({ date: key, count: dailyMap.get(key) || 0 });
  }

  // Which compass octant gets the most launches?
  const dirLabels = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"];
  let topIdx = 0;
  for (let i = 0; i < bearingBins.length; i++) if (bearingBins[i] > bearingBins[topIdx]) topIdx = i;
  const topBearingLabel = bearingBins[topIdx] > 0 ? dirLabels[topIdx] : "—";

  // ---- Streaks of consecutive days with ≥1 sonde --------------------------
  // Walk sorted dates, counting runs where each date is exactly 1 day after
  // the previous. Same TZ as the bucketing above, so "today" lines up with
  // what the daily strip shows on the right edge.
  const sortedDates = Array.from(allDates).sort(); // YYYY-MM-DD sorts lexically
  const MS_PER_DAY = 86400_000;
  const dateKeyFromMs = (ms: number) => {
    const d = new Date(ms);
    const y = utc ? d.getUTCFullYear() : d.getFullYear();
    const mo = utc ? d.getUTCMonth() : d.getMonth();
    const da = utc ? d.getUTCDate() : d.getDate();
    return `${y}-${String(mo + 1).padStart(2, "0")}-${String(da).padStart(2, "0")}`;
  };
  const isOneDayAfter = (prev: string, curr: string) => {
    // Re-parse via Date.UTC so DST boundaries don't break the +86400 check.
    const [py, pm, pd] = prev.split("-").map(Number);
    const [cy, cm, cd] = curr.split("-").map(Number);
    return Date.UTC(cy, cm - 1, cd) - Date.UTC(py, pm - 1, pd) === MS_PER_DAY;
  };
  let longestStreak = 0;
  let currentStreak = 0;
  if (sortedDates.length) {
    let run = 1;
    longestStreak = 1;
    for (let i = 1; i < sortedDates.length; i++) {
      run = isOneDayAfter(sortedDates[i - 1], sortedDates[i]) ? run + 1 : 1;
      if (run > longestStreak) longestStreak = run;
    }
    // Current streak = tail of `sortedDates` if it ends today or yesterday.
    const today = dateKeyFromMs(now);
    const yesterday = dateKeyFromMs(now - MS_PER_DAY);
    const last = sortedDates[sortedDates.length - 1];
    if (last === today || last === yesterday) {
      currentStreak = 1;
      for (let i = sortedDates.length - 2; i >= 0; i--) {
        if (isOneDayAfter(sortedDates[i], sortedDates[i + 1])) currentStreak++;
        else break;
      }
    }
  }

  let mostBusyHour = 0;
  for (let h = 0; h < 24; h++) if (byHour[h] > byHour[mostBusyHour]) mostBusyHour = h;

  return {
    total: rows.length,
    in24h, in7d, in30d,
    earliest, latest,
    byType,
    rangeRecord,
    highestSeen,
    longestFlight,
    longestFlightByTime,
    bearingHistogram: bearingBins,
    topBearingLabel,
    daily,
    byHour,
    mostBusyHour,
    longestStreak,
    currentStreak,
    averageRangeKm: rangeCount ? rangeSum / rangeCount : 0,
  };
}

/* ---------------------------------------------------------------------------
 * VISUAL COMPONENTS
 * ------------------------------------------------------------------------- */

function TypeBars({ byType, total }: { byType: Stats["byType"]; total: number }) {
  if (!byType.length) return <div className="text-xs text-muted-foreground py-2">No sondes yet.</div>;
  const max = byType[0].count;
  return (
    <ul className="space-y-1.5">
      {byType.map(({ type, count }) => {
        const pct = max ? (count / max) * 100 : 0;
        const share = total ? (count / total) * 100 : 0;
        return (
          <li key={type} className="grid grid-cols-[5rem_1fr_auto] gap-3 items-center">
            <span className="text-[11px] mono text-foreground/80">{type}</span>
            <div className="relative h-2 rounded-sm bg-background/60 overflow-hidden">
              <div className="absolute inset-y-0 left-0 bg-signal/70" style={{ width: `${pct}%` }} />
            </div>
            <span className="text-[10px] mono text-muted-foreground tabular-nums">{count} <span className="text-muted-foreground/60">· {share.toFixed(0)}%</span></span>
          </li>
        );
      })}
    </ul>
  );
}

function BearingRose({ bins }: { bins: number[] }) {
  // Polar bar — 16 wedges around the centre, length scaled to max bin.
  const max = Math.max(1, ...bins);
  const cx = 100, cy = 100, rmin = 16, rmax = 88;
  const wedges = bins.map((count, i) => {
    const fraction = count / max;
    const r = rmin + (rmax - rmin) * fraction;
    const a0 = ((i * 22.5) - 22.5 / 2 - 90) * Math.PI / 180;
    const a1 = ((i * 22.5) + 22.5 / 2 - 90) * Math.PI / 180;
    const p0 = [cx + rmin * Math.cos(a0), cy + rmin * Math.sin(a0)];
    const p1 = [cx + r * Math.cos(a0),    cy + r * Math.sin(a0)];
    const p2 = [cx + r * Math.cos(a1),    cy + r * Math.sin(a1)];
    const p3 = [cx + rmin * Math.cos(a1), cy + rmin * Math.sin(a1)];
    return (
      <path
        key={i}
        d={`M${p0[0]} ${p0[1]} L${p1[0]} ${p1[1]} A${r} ${r} 0 0 1 ${p2[0]} ${p2[1]} L${p3[0]} ${p3[1]} A${rmin} ${rmin} 0 0 0 ${p0[0]} ${p0[1]} Z`}
        fill={`hsl(195 78% ${50 + fraction * 18}%)`}
        opacity={0.35 + fraction * 0.55}
        stroke="hsl(195 18% 6%)"
        strokeWidth={1}
      />
    );
  });
  return (
    <svg viewBox="0 0 200 200" className="w-44 h-44 flex-shrink-0">
      <circle cx={cx} cy={cy} r={rmax} fill="none" stroke="hsl(196 20% 22%)" strokeWidth={1} />
      <circle cx={cx} cy={cy} r={rmax * 0.5} fill="none" stroke="hsl(196 20% 16%)" strokeWidth={1} strokeDasharray="2 4" />
      {wedges}
      {(["N", "E", "S", "W"] as const).map((d, i) => {
        const a = (i * 90 - 90) * Math.PI / 180;
        return <text key={d} x={cx + (rmax + 8) * Math.cos(a)} y={cy + (rmax + 8) * Math.sin(a) + 4} textAnchor="middle" fill="hsl(195 8% 70%)" fontSize="10" fontFamily="'Plex Mono',monospace" fontWeight="600">{d}</text>;
      })}
    </svg>
  );
}

// Shared hover tooltip for the bar charts. Replaces the previous CSS-only
// `:hover` implementation so the tip can:
//  1. fire instantly (no native title= delay), and
//  2. clamp to the viewport — left/right edge bars used to push the tooltip
//     off-screen with the CSS-centered approach.
function useChartTip() {
  const [tip, setTip] = useState<{ content: React.ReactNode; anchorX: number; anchorY: number } | null>(null);
  const show = (e: React.MouseEvent, content: React.ReactNode) => {
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    setTip({ content, anchorX: r.left + r.width / 2, anchorY: r.top });
  };
  const hide = () => setTip(null);
  const Tip = tip ? <FloatingTip content={tip.content} anchorX={tip.anchorX} anchorY={tip.anchorY} /> : null;
  return { show, hide, Tip };
}

function FloatingTip({ content, anchorX, anchorY }: { content: React.ReactNode; anchorX: number; anchorY: number }) {
  const ref = useRef<HTMLDivElement>(null);
  // Default placement directly above the anchor; useLayoutEffect clamps it to
  // the viewport after measuring the rendered tip's width/height.
  const [pos, setPos] = useState({ x: anchorX, y: anchorY, ready: false });
  useEffect(() => {
    const el = ref.current; if (!el) return;
    const w = el.offsetWidth;
    const h = el.offsetHeight;
    const m = 4;
    let nx = anchorX - w / 2;
    if (nx < m) nx = m;
    if (nx + w > window.innerWidth - m) nx = window.innerWidth - w - m;
    let ny = anchorY - h - m;
    if (ny < m) ny = anchorY + m + 14; // overflow below if not enough room above
    setPos({ x: nx, y: ny, ready: true });
  }, [anchorX, anchorY, content]);
  return (
    <div
      ref={ref}
      className="fixed pointer-events-none whitespace-nowrap px-1.5 py-0.5 rounded mono text-[10px] bg-popover text-popover-foreground border border-border shadow-md z-[60]"
      style={{ left: pos.x, top: pos.y, opacity: pos.ready ? 1 : 0 }}
    >
      {content}
    </div>
  );
}

function HourBars({ byHour, peak, tzLabel }: { byHour: number[]; peak: number; tzLabel: string }) {
  const max = Math.max(1, ...byHour);
  const { show, hide, Tip } = useChartTip();
  return (
    <div className="grid gap-px h-24 relative" style={{ gridTemplateColumns: "repeat(24, minmax(0, 1fr))" }}>
      {byHour.map((c, h) => {
        const pct = (c / max) * 100;
        return (
          <div
            key={h}
            className="flex flex-col items-stretch h-full group"
            onMouseEnter={(e) => show(e, `${String(h).padStart(2, "0")}:00 ${tzLabel} · ${c} sonde${c === 1 ? "" : "s"}`)}
            onMouseLeave={hide}
          >
            <div className="flex-1 flex items-end">
              <div
                className={`w-full rounded-sm transition-colors ${h === peak ? "bg-signal/90" : "bg-signal/35 group-hover:bg-signal/60"}`}
                style={{ height: `${Math.max(6, pct)}%` }}
              />
            </div>
            <span className="text-[8px] mono text-muted-foreground/60 leading-none text-center mt-0.5 h-2.5">
              {h % 3 === 0 ? h : ""}
            </span>
          </div>
        );
      })}
      {Tip}
    </div>
  );
}

function DailyStrip({ daily }: { daily: Stats["daily"] }) {
  const max = Math.max(1, ...daily.map(d => d.count));
  const { show, hide, Tip } = useChartTip();
  return (
    <div className="grid grid-flow-col auto-cols-fr gap-px items-end h-14 relative">
      {daily.map(d => {
        const pct = (d.count / max) * 100;
        const lvl = d.count === 0 ? 0 : d.count < max * 0.33 ? 1 : d.count < max * 0.66 ? 2 : 3;
        const bg = ["bg-background/60", "bg-signal/25", "bg-signal/55", "bg-signal/90"][lvl];
        return (
          <div
            key={d.date}
            className="relative h-full flex items-end"
            onMouseEnter={(e) => show(e, `${d.date} · ${d.count} sonde${d.count === 1 ? "" : "s"}`)}
            onMouseLeave={hide}
          >
            <div className={`w-full rounded-sm ${bg}`} style={{ height: `${Math.max(8, pct)}%` }} />
          </div>
        );
      })}
      {Tip}
    </div>
  );
}

/* ------------------------------------------------------------------------- */

export function Stats() {
  const [sondes, setSondes] = useState<HistoricalSonde[]>([]);
  const [rotator, setRotator] = useState<RotatorStatus>({ enabled: false, az: 0, el: 0, mode: "idle" });
  const [blocklist, setBlocklist] = useState<BlockEntry[]>([]);
  const [busy, setBusy] = useState(false);
  const [updatedAt, setUpdatedAt] = useState<Date | null>(null);
  // Tick refresh so anything time-dependent (e.g. active-block count) updates
  // predictably and doesn't read Date.now() during render.
  const [now, setNow] = useState(() => Date.now() / 1000);
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now() / 1000), 30_000);
    return () => clearInterval(t);
  }, []);
  const busyRef = useRef(false);

  const refresh = useCallback(async (silent = false) => {
    if (busyRef.current) return;
    busyRef.current = true;
    setBusy(true);
    try {
      const [logs, r, b] = await Promise.all([
        apiGet<HistoricalSonde[]>("/get_log_list").catch(() => [] as HistoricalSonde[]),
        apiGet<RotatorStatus>("/rotator_status").catch(() => ({ enabled: false, az: 0, el: 0, mode: "idle" } as RotatorStatus)),
        apiGet<BlockEntry[]>("/blocklist").catch(() => [] as BlockEntry[]),
      ]);
      setSondes(Array.isArray(logs) ? logs : []);
      setRotator(r);
      setBlocklist(Array.isArray(b) ? b : []);
      setUpdatedAt(new Date());
      if (!silent) toast.success("Stats refreshed");
    } catch (e: any) { if (!silent) toast.error(e.message || "Failed"); }
    busyRef.current = false;
    setBusy(false);
  }, []);

  useEffect(() => {
    refresh(true);
    // Stats don't need to tick every 5 s — once a minute is plenty since the
    // log list only grows when a flight ends.
    const t = setInterval(() => refresh(true), 60_000);
    return () => clearInterval(t);
  }, [refresh]);

  const prefs = usePrefs();
  const stats = useMemo(() => computeStats(sondes, prefs.utc), [sondes, prefs.utc]);
  const tzLabel = prefs.utc ? "UTC" : "local";
  // Slice a YYYY-MM-DD prefix off an ISO/epoch datetime, honoring the chosen
  // time zone — used in the records cards so "date" matches the daily activity
  // chart's bucketing.
  const fmtDate = (d: string | number | undefined): string => {
    if (d == null) return "—";
    // Numeric inputs from this page are ms-since-epoch (stats.earliest/latest);
    // string inputs are ISO datetimes. Don't multiply by 1000.
    const date = typeof d === "string" ? new Date(d) : new Date(d);
    if (isNaN(date.getTime())) return "—";
    const y = prefs.utc ? date.getUTCFullYear() : date.getFullYear();
    const mo = prefs.utc ? date.getUTCMonth() : date.getMonth();
    const da = prefs.utc ? date.getUTCDate() : date.getDate();
    return `${y}-${String(mo + 1).padStart(2, "0")}-${String(da).padStart(2, "0")}`;
  };
  const blocksTemp = useMemo(
    () => blocklist.filter(b => b.until && b.until > now).length,
    [blocklist, now]
  );
  const blocksPerm = blocklist.filter(b => !b.until).length;

  return (
    <div className="space-y-4">
      <div className="flex items-end gap-3 flex-wrap">
        <div>
          <h1 className="text-base font-semibold">Station stats</h1>
          <p className="text-xs text-muted-foreground">
            {stats.total > 0 ? (
              <>
                Drawn from <b className="text-foreground/80">{stats.total.toLocaleString()}</b> sonde{stats.total === 1 ? "" : "s"} in your log archive
                {stats.earliest && stats.latest && (
                  <> · <span className="mono">{fmtDate(stats.earliest!)}</span> → <span className="mono">{fmtDate(stats.latest!)}</span></>
                )}
              </>
            ) : (
              "No flights logged yet — records will appear once you've received a sonde."
            )}
          </p>
        </div>
        <div className="flex-1" />
        <div className="flex flex-col items-end gap-1">
          <Button size="sm" variant="default" onClick={() => refresh()} disabled={busy}><RefreshCcw className="w-3 h-3" /> Refresh</Button>
          {updatedAt && <span className="text-[10px] mono text-muted-foreground leading-none">updated {fmtTime(updatedAt.getTime() / 1000)}</span>}
        </div>
      </div>

      {/* ----- Headline numbers ------------------------------------------- */}
      <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-6 gap-3">
        <Panel><div className="p-3"><Metric label="Sondes in archive" value={stats.total} accent="primary" /></div></Panel>
        <Panel><div className="p-3"><Metric label="Last 24 h" value={stats.in24h} /></div></Panel>
        <Panel><div className="p-3"><Metric label="Last 7 days" value={stats.in7d} /></div></Panel>
        <Panel><div className="p-3"><Metric label="Last 30 days" value={stats.in30d} /></div></Panel>
        <Panel><div className="p-3"><Metric label="Longest streak" value={`${stats.longestStreak} d`} /></div></Panel>
        <Panel><div className="p-3"><Metric label="Current streak" value={`${stats.currentStreak} d`} /></div></Panel>
      </div>

      {/* ----- Records ---------------------------------------------------- */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-5 gap-3">
        <Panel>
          <PanelHead
            title="Range record"
            icon={<Award className="w-3.5 h-3.5 text-warn" strokeWidth={1.75} />}
          />
          <div className="p-3 space-y-1">
            {stats.rangeRecord ? (
              <>
                <div className="mono text-2xl font-semibold text-warn leading-none">{fmtDist(stats.rangeRecord.km)}</div>
                <div className="text-[11px] mono text-muted-foreground">
                  <Link to={`/historical?q=${encodeURIComponent(stats.rangeRecord.serial)}`} className="hover:text-foreground underline-offset-2 hover:underline">{stats.rangeRecord.serial}</Link>
                  {" · "}{stats.rangeRecord.type}
                </div>
                <div className="text-[10px] mono text-muted-foreground/70">{fmtDate(stats.rangeRecord.date)}</div>
              </>
            ) : <div className="text-xs text-muted-foreground py-2">No range data yet.</div>}
          </div>
        </Panel>
        <Panel>
          <PanelHead title="Longest flight" icon={<TrendingUp className="w-3.5 h-3.5 text-signal" strokeWidth={1.75} />}
            meta={<span className="text-[9px] uppercase tracking-widest text-muted-foreground/60">by frames</span>} />
          <div className="p-3 space-y-1">
            {stats.longestFlight ? (
              <>
                <div className="mono text-2xl font-semibold text-signal leading-none">{stats.longestFlight.lines.toLocaleString()}</div>
                <div className="text-[11px] mono text-muted-foreground">
                  <Link to={`/historical?q=${encodeURIComponent(stats.longestFlight.serial)}`} className="hover:text-foreground underline-offset-2 hover:underline">{stats.longestFlight.serial}</Link>
                  {" · "}{stats.longestFlight.type}
                </div>
                <div className="text-[10px] mono text-muted-foreground/70">{fmtDate(stats.longestFlight.date)}</div>
              </>
            ) : <div className="text-xs text-muted-foreground py-2">No flight data yet.</div>}
          </div>
        </Panel>
        <Panel>
          <PanelHead title="Longest flight" icon={<Clock className="w-3.5 h-3.5 text-signal" strokeWidth={1.75} />}
            meta={<span className="text-[9px] uppercase tracking-widest text-muted-foreground/60">by time</span>} />
          <div className="p-3 space-y-1">
            {stats.longestFlightByTime ? (
              <>
                <div className="mono text-2xl font-semibold text-signal leading-none">{fmtDuration(stats.longestFlightByTime.durationSec)}</div>
                <div className="text-[11px] mono text-muted-foreground">
                  <Link to={`/historical?q=${encodeURIComponent(stats.longestFlightByTime.serial)}`} className="hover:text-foreground underline-offset-2 hover:underline">{stats.longestFlightByTime.serial}</Link>
                  {" · "}{stats.longestFlightByTime.type}
                </div>
                <div className="text-[10px] mono text-muted-foreground/70">{fmtDate(stats.longestFlightByTime.date)}</div>
              </>
            ) : <div className="text-xs text-muted-foreground py-2">No flight data yet.</div>}
          </div>
        </Panel>
        <Panel>
          <PanelHead
            title="Average range"
            icon={<MapPin className="w-3.5 h-3.5" strokeWidth={1.75} />}
          />
          <div className="p-3 space-y-1">
            <div className="mono text-2xl font-semibold leading-none">{fmtDist(stats.averageRangeKm)}</div>
            <div className="text-[11px] mono text-muted-foreground">Over {stats.total.toLocaleString()} sonde{stats.total === 1 ? "" : "s"}</div>
          </div>
        </Panel>
        <Panel>
          <PanelHead
            title="Highest altitude"
            icon={<Mountain className="w-3.5 h-3.5" strokeWidth={1.75} />}
          />
          <div className="p-3 space-y-1">
            {stats.highestSeen ? (
              <>
                <div className="mono text-2xl font-semibold leading-none">{fmtAlt(stats.highestSeen.alt)}</div>
                <div className="text-[11px] mono text-muted-foreground">
                  <Link to={`/historical?q=${encodeURIComponent(stats.highestSeen.serial)}`} className="hover:text-foreground underline-offset-2 hover:underline">{stats.highestSeen.serial}</Link>
                  {" · "}{stats.highestSeen.type}
                </div>
                <div className="text-[10px] mono text-muted-foreground/70">{fmtDate(stats.highestSeen.date)}</div>
              </>
            ) : <div className="text-xs text-muted-foreground py-2">No altitude data yet.</div>}
          </div>
        </Panel>
      </div>

      {/* ----- Type breakdown + bearing rose ------------------------------ */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
        <Panel>
          <PanelHead title="By sonde type" icon={<Radio className="w-3.5 h-3.5" strokeWidth={1.75} />}
            meta={<span className="text-[10px] mono text-muted-foreground">{stats.byType.length} type{stats.byType.length === 1 ? "" : "s"}</span>} />
          <div className="p-3">
            <TypeBars byType={stats.byType} total={stats.total} />
          </div>
        </Panel>
        <Panel>
          <PanelHead title="Reception bearings" icon={<Compass className="w-3.5 h-3.5" strokeWidth={1.75} />}
            meta={<span className="text-[10px] mono text-muted-foreground">top {stats.topBearingLabel}</span>} />
          <div className="p-3 flex items-center gap-4 flex-wrap">
            <BearingRose bins={stats.bearingHistogram} />
            <div className="text-[11px] text-muted-foreground leading-relaxed max-w-[18rem]">
              Where each sonde was first heard, relative to your station. Longer wedges = more sondes arrived from that compass direction.
            </div>
          </div>
        </Panel>
      </div>

      {/* ----- Activity heatmaps ------------------------------------------ */}
      {/* `!overflow-visible` is critical: the bar-hover tooltips render above
          the panel's top edge via position:absolute, and the Panel's default
          overflow-hidden (which rounds the corners) would otherwise clip them. */}
      <div className="grid grid-cols-1 md:grid-cols-[7fr_3fr] gap-3">
        <Panel className="!overflow-visible">
          <PanelHead title="Daily activity" icon={<Clock className="w-3.5 h-3.5" strokeWidth={1.75} />}
            meta={<span className="text-[10px] mono text-muted-foreground">last 30 days</span>} />
          <div className="p-3 space-y-2">
            <DailyStrip daily={stats.daily} />
            <div className="text-[10px] mono text-muted-foreground/70 flex items-center gap-2 justify-between">
              <span>{stats.daily[0].date}</span>
              <span>{stats.daily[stats.daily.length - 1].date}</span>
            </div>
          </div>
        </Panel>
        <Panel className="!overflow-visible">
          <PanelHead title="Hour of day" icon={<Clock className="w-3.5 h-3.5" strokeWidth={1.75} />}
            meta={<span className="text-[10px] mono text-muted-foreground">peak {String(stats.mostBusyHour).padStart(2, "0")}:00 {tzLabel}</span>} />
          <div className="p-3">
            <HourBars byHour={stats.byHour} peak={stats.mostBusyHour} tzLabel={tzLabel} />
          </div>
        </Panel>
      </div>

{/* ----- Rotator pointing (only when configured) -------------------- */}
      {rotator.enabled && (
        <Panel>
          <PanelHead title="Antenna pointing" icon={<Compass className="w-3.5 h-3.5" strokeWidth={1.75} />}
            meta={<span className="text-[10px] mono text-muted-foreground">{rotator.target_id ?? "no target"}</span>} />
          <div className="p-4 flex items-center gap-5 flex-wrap">
            <Compass2D az={rotator.az} target={rotator.target_az ?? null} />
            <ElevBar el={rotator.el} target={rotator.target_el ?? null} />
            <div className="flex flex-col gap-3 min-w-[180px]">
              <Metric label="Current" value={<span>AZ {rotator.az.toFixed(1)}° / EL {rotator.el.toFixed(1)}°</span>} accent="primary" />
              {rotator.target_az != null && (
                <Metric label="Target" value={<span>AZ {(rotator.target_az || 0).toFixed(1)}° / EL {(rotator.target_el || 0).toFixed(1)}°</span>} accent="warn" />
              )}
              <Metric label="Mode" value={<span className="text-xs">{rotator.mode}</span>} className="text-sm" />
            </div>
          </div>
        </Panel>
      )}

      {/* ----- Frequency blocklist ---------------------------------------- */}
      <Panel>
        <PanelHead title="Frequency blocklist" icon={<Ban className="w-3.5 h-3.5" strokeWidth={1.75} />}
          meta={<span className="text-[10px] text-muted-foreground/70">temp: {blocksTemp} · perm: {blocksPerm}</span>} />
        {blocklist.length === 0 ? (
          <div className="px-3 py-3 text-center text-muted-foreground inline-flex items-center justify-center gap-2 mono text-[11px]">
            <CheckCircle2 className="w-4 h-4 text-signal/80" /> All channels clear
          </div>
        ) : (
          <div className="px-3 py-2 flex flex-wrap gap-1.5">
            {blocklist.map((b, i) => {
              const temp = !!b.until;
              return (
                <span
                  key={i}
                  className={`inline-flex items-center gap-1.5 px-1.5 py-0.5 rounded mono text-[11px] ${
                    temp ? "bg-warn/[0.12] text-warn" : "bg-alert/[0.12] text-alert line-through decoration-alert/40"
                  }`}
                  title={temp ? `Temporary block — ${blockCountdown(b)} remaining` : "Permanent (never_scan)"}
                >
                  {(b.freq / 1e6).toFixed(3)}
                  {temp && <span className="text-warn/70">· {blockCountdown(b)}</span>}
                </span>
              );
            })}
          </div>
        )}
      </Panel>

      {sondes.length === 0 && (
        <Panel>
          <div className="p-8 text-center text-muted-foreground">
            <BarChart3 className="w-8 h-8 mx-auto mb-3 opacity-40" />
            <div className="text-sm">No flights logged yet</div>
            <div className="text-[11px] mono mt-1">Records and trends will appear once you've received a sonde.</div>
          </div>
        </Panel>
      )}
    </div>
  );
}
