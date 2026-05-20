import { useSyncExternalStore } from "react";

export type ThemePref = "dark" | "light" | "system";
export type MarkerStyle = "triangle" | "balloon";

type Prefs = {
  metric: boolean;
  utc: boolean;
  /** When true (default), times render as 24-hour HH:MM:SS. When false,
   *  12-hour with AM/PM. Applies to both fmtTime and fmtDateTime. */
  hour12: boolean;
  theme: ThemePref;
  tile: string;
  showVersion: boolean;
  markerStyle: MarkerStyle;
};
const KEY = "obs.prefs";
const DEFAULTS: Prefs = {
  metric: true,
  utc: true,
  hour12: false,
  theme: "system",
  // "Auto" picks Dark Matter or Voyager based on the *effective* theme so the
  // map chrome matches the rest of the UI without the user having to pick a
  // tile manually. Anything else from the TILES registry is an explicit choice.
  tile: "Auto",
  showVersion: true,
  markerStyle: "triangle",
};

function load(): Prefs {
  try {
    const raw = localStorage.getItem(KEY);
    const loaded = { ...DEFAULTS, ...JSON.parse(raw || "{}") };
    // Migrate legacy values: theme was previously only "dark" | "light"; if
    // we see something unexpected, fall back to "system".
    if (loaded.theme !== "dark" && loaded.theme !== "light" && loaded.theme !== "system") loaded.theme = "system";
    if (loaded.markerStyle !== "triangle" && loaded.markerStyle !== "balloon") loaded.markerStyle = "triangle";
    // `tile` used to default to "Dark Matter". Anyone whose stored value is
    // still that exact default never picked it consciously — migrate to
    // "Auto" so the map follows their theme. Persist the migration back to
    // localStorage so the next page load reads "Auto" directly (and so
    // setPrefs writes don't repeatedly include the migration flag).
    if (loaded.tile === "Dark Matter" && !loaded._tileMigrated) {
      loaded.tile = "Auto";
      loaded._tileMigrated = true;
      try { localStorage.setItem(KEY, JSON.stringify(loaded)); } catch {}
    }
    return loaded;
  } catch {
    return DEFAULTS;
  }
}

const listeners = new Set<() => void>();
let state: Prefs = load();

function notify() { listeners.forEach(l => l()); }
function subscribe(l: () => void) { listeners.add(l); return () => { listeners.delete(l); }; }
function snapshot() { return state; }

/** Resolves theme="system" to "dark" or "light" using the OS preference.
 *  Explicit "dark"/"light" pass through unchanged. */
export function effectiveTheme(t: ThemePref): "dark" | "light" {
  if (t === "system") {
    if (typeof window === "undefined") return "dark";
    return window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
  }
  return t;
}

function applyTheme() {
  if (typeof document === "undefined") return;
  document.documentElement.classList.toggle("light", effectiveTheme(state.theme) === "light");
}

export function setPrefs(p: Partial<Prefs>) {
  state = { ...state, ...p };
  try { localStorage.setItem(KEY, JSON.stringify(state)); } catch {}
  if (p.theme !== undefined) applyTheme();
  notify();
}

export function usePrefs() { return useSyncExternalStore(subscribe, snapshot, snapshot); }

// Apply theme class on initial load + listen for OS changes (only meaningful
// when theme="system" but the listener is harmless either way). Also notify
// React subscribers so components that derive from the *effective* theme
// (e.g. map tile chooser) re-render when the OS toggles dark/light.
if (typeof window !== "undefined") {
  applyTheme();
  try {
    const mq = window.matchMedia("(prefers-color-scheme: light)");
    const onChange = () => {
      if (state.theme === "system") { applyTheme(); notify(); }
    };
    if (mq.addEventListener) mq.addEventListener("change", onChange);
    else mq.addListener(onChange); // Safari < 14
  } catch {}
}

/* --- Formatters --- */
export function fmtAlt(m: number | null | undefined) {
  if (m == null || !isFinite(m)) return "—";
  return state.metric ? `${Math.round(m).toLocaleString()} m` : `${Math.round(m * 3.28084).toLocaleString()} ft`;
}
export function fmtSpeed(mps: number | null | undefined) {
  if (mps == null || !isFinite(mps)) return "—";
  return state.metric ? `${mps.toFixed(1)} m/s` : `${(mps * 2.23694).toFixed(1)} mph`;
}
export function fmtDist(km: number | null | undefined) {
  if (km == null || !isFinite(km)) return "—";
  return state.metric ? `${km.toFixed(1)} km` : `${(km * 0.621371).toFixed(1)} mi`;
}
export function fmtTemp(c: number | null | undefined) {
  if (c == null || !isFinite(c)) return "—";
  return state.metric ? `${c.toFixed(1)}°C` : `${(c * 9/5 + 32).toFixed(1)}°F`;
}
/** Format a frequency. Accepts either MHz (e.g. 404.011) or Hz (e.g. 404011000)
 *  and returns a MHz string. The split-point heuristic (>1e6) reliably tells
 *  the two apart since radiosondes live in 400–406 MHz and 1.68 GHz bands.
 */
export function fmtFreq(value: number | null | undefined) {
  if (value == null || !isFinite(value) || value === 0) return "—";
  const mhz = value < 1e6 ? value : value / 1e6;
  return `${mhz.toFixed(3)}`;
}
/**
 * Format an ISO timestamp or unix-seconds number as HH:MM:SS, in UTC or local
 * per `state.utc`, and 24-hour or 12-hour per `state.hour12`.
 */
export function fmtTime(iso: string | number | undefined) {
  if (!iso) return "—";
  const d = typeof iso === "number" ? new Date(iso * 1000) : new Date(iso);
  if (isNaN(d.getTime())) return "—";
  return formatHMS(d, state.utc, state.hour12);
}
export function fmtDateTime(d: Date | string | number | undefined) {
  if (!d) return "—";
  const date = typeof d === "string" ? new Date(d) : typeof d === "number" ? new Date(d * 1000) : d;
  if (isNaN(date.getTime())) return "—";
  if (state.utc) {
    const ymd = date.toISOString().slice(0, 10);
    return `${ymd} ${formatHMS(date, true, state.hour12)} UTC`;
  }
  return date.toLocaleString(undefined, {
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit",
    hour12: state.hour12,
  });
}

function formatHMS(d: Date, utc: boolean, hour12: boolean): string {
  const h24 = utc ? d.getUTCHours() : d.getHours();
  const m = utc ? d.getUTCMinutes() : d.getMinutes();
  const s = utc ? d.getUTCSeconds() : d.getSeconds();
  const mm = String(m).padStart(2, "0");
  const ss = String(s).padStart(2, "0");
  if (!hour12) return `${String(h24).padStart(2, "0")}:${mm}:${ss}`;
  const ampm = h24 >= 12 ? "PM" : "AM";
  const h12 = ((h24 + 11) % 12) + 1;
  return `${String(h12).padStart(2, "0")}:${mm}:${ss} ${ampm}`;
}
export function fmtAge(ts: number | undefined) {
  if (!ts) return "—";
  const sec = Date.now() / 1000 - ts;
  if (sec < 60) return `${Math.floor(sec)}s`;
  if (sec < 3600) return `${Math.floor(sec / 60)}m`;
  return `${Math.floor(sec / 3600)}h`;
}
export function fmtBearing(deg: number | null | undefined) {
  if (deg == null || !isFinite(deg)) return "—";
  const dirs = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"];
  return `${Math.round(deg)}° ${dirs[Math.round((deg % 360) / 22.5) % 16]}`;
}

/**
 * Compute look-angles from a station (lat/lon/alt-m) to a target (lat/lon/alt-m).
 * Returns azimuth (°), elevation (°), and slant range (km).
 * Based on the standard ECEF look-angle math; accurate enough for the small
 * cross-sections involved with radiosondes (km-scale).
 */
export function lookAngles(
  station: { lat: number; lon: number; alt?: number } | null | undefined,
  target: { lat: number; lon: number; alt?: number } | null | undefined,
): { az: number; el: number; range_km: number } | null {
  if (!station || !target || target.lat == null || target.lon == null) return null;
  const R = 6371.0; // mean Earth radius in km
  const toRad = (d: number) => (d * Math.PI) / 180;
  const toDeg = (r: number) => (r * 180) / Math.PI;
  const lat1 = toRad(station.lat), lon1 = toRad(station.lon);
  const lat2 = toRad(target.lat),  lon2 = toRad(target.lon);
  const dLat = lat2 - lat1, dLon = lon2 - lon1;
  const a = Math.sin(dLat / 2) ** 2 + Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2;
  const c = 2 * Math.asin(Math.min(1, Math.sqrt(a)));
  const ground_km = R * c;
  // Bearing
  const y = Math.sin(dLon) * Math.cos(lat2);
  const x = Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);
  let az = toDeg(Math.atan2(y, x));
  if (az < 0) az += 360;
  // Elevation
  const dAlt_km = (((target.alt ?? 0) - (station.alt ?? 0)) / 1000);
  const range_km = Math.sqrt(ground_km * ground_km + dAlt_km * dAlt_km);
  const el = toDeg(Math.atan2(dAlt_km, ground_km));
  return { az, el, range_km };
}
