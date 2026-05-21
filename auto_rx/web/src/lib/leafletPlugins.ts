/**
 * Bundle the two legacy Leaflet plugins that fix tile rendering for the map
 * views:
 *
 *   - L.TileLayer.NoGap.js — patches L.TileLayer to render via canvas without
 *     sub-pixel gaps. Fixes the "white seams" between tiles.
 *   - leaflet.edgebuffer.js — extends keepBuffer so panning is smoother.
 *
 * Both plugins are non-ESM: they expect the global `window.L` and mutate it
 * directly (`L.TileLayer.mergeOptions(...)`, `L.GridLayer.include(...)`).
 *
 * They are imported with Vite's `?url` suffix so Vite emits them as fingerprinted
 * static assets (not parsed/transformed as ES modules — they wouldn't survive
 * that). At runtime we inject them as `<script>` tags after bridging our
 * imported `L` to `window.L`, which is the bit that keeps every previous
 * bundling attempt from working: the script-tag execution sees the same `L`
 * instance our map code holds, so the plugin's `.include()` and
 * `.mergeOptions()` calls patch the right object.
 */
import L from "leaflet";
import noGapUrl from "./leaflet-plugins/L.TileLayer.NoGap.js?url";
import edgeBufferUrl from "./leaflet-plugins/leaflet.edgebuffer.js?url";

let loadPromise: Promise<void> | null = null;

function loadScript(src: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const sel = `script[data-leaflet-plugin="${src}"]`;
    if (document.querySelector(sel)) return resolve();
    const s = document.createElement("script");
    s.src = src;
    s.async = false;
    s.dataset.leafletPlugin = src;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error(`Failed to load ${src}`));
    document.head.appendChild(s);
  });
}

export function loadLeafletPlugins(): Promise<void> {
  if (loadPromise) return loadPromise;
  loadPromise = (async () => {
    // Bridge our imported L to the global BEFORE the plugin scripts run, so
    // their L.TileLayer.* / L.GridLayer.* mutations land on the same object
    // our map code uses.
    (window as any).L = L;
    try {
      // NoGap must load before EdgeBuffer — EdgeBuffer wraps
      // GridLayer.prototype._getTiledPixelBounds which NoGap may also touch.
      await loadScript(noGapUrl);
      await loadScript(edgeBufferUrl);
    } catch (e) {
      // Non-fatal: tiles still render, just with potential seams on pan.
      console.warn("[leafletPlugins]", e);
    }
  })();
  return loadPromise;
}
