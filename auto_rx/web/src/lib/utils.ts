import { clsx, type ClassValue } from "clsx";
import { twMerge } from "tailwind-merge";

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

// Escape arbitrary strings before interpolating them into HTML. Used by the
// Leaflet popup builders, which take template literals and don't get React's
// auto-escape. Sonde serials and other backend-derived strings have to go
// through this — they originate from on-disk log filenames and aren't
// inherently trustworthy.
export function escapeHtml(s: string): string {
  return String(s).replace(/[&<>"']/g, c => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[c]!));
}

// Auto_rx's telemetry sends `freq` as a formatted string ("404.011 MHz") and
// a numeric `freq_float` (MHz). The string carries 3-decimal precision while
// freq_float is rounded to 2 — so we prefer the string, then fall back to
// the floats. Returns NaN if nothing usable.
export function parseFreqMhz(raw: { freq?: unknown; freq_float?: unknown }): number {
  const { freq, freq_float } = raw;
  if (typeof freq === "string") return parseFloat(freq);
  if (typeof freq_float === "number" && isFinite(freq_float)) return freq_float;
  if (typeof freq === "number" && isFinite(freq)) return freq;
  return NaN;
}
