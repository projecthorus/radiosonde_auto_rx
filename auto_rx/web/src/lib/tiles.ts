/**
 * Shared Leaflet tile-provider registry. Used by both the dashboard SondeMap
 * and the History page so they offer the same choices.
 *
 * Note: some providers (Stadia-hosted Stamen variants) require an API key for
 * production traffic — they're listed here for parity with the OG UI; users
 * can pick a free provider if their setup gets rate-limited.
 */
import type L from "leaflet";

export type Tile = { url: string; opts: L.TileLayerOptions; theme: "dark" | "light" };

/** Sentinel value used by the tile pref. Resolved at render time to either
 *  the dark or light default depending on the effective UI theme. */
export const AUTO_TILE = "Auto";
const DARK_DEFAULT = "Dark Matter";
const LIGHT_DEFAULT = "Voyager";

/** Resolve a tile pref against the current effective theme. `prefName` may be
 *  any registered tile key or AUTO_TILE; falls back to dark default. */
export function resolveTile(prefName: string, effectiveTheme: "dark" | "light"): { name: string; tile: Tile } {
  if (prefName === AUTO_TILE) {
    const name = effectiveTheme === "light" ? LIGHT_DEFAULT : DARK_DEFAULT;
    return { name, tile: TILES[name] };
  }
  const tile = TILES[prefName] || TILES[DARK_DEFAULT];
  return { name: TILES[prefName] ? prefName : DARK_DEFAULT, tile };
}

export const TILES: Record<string, Tile> = {
  "Dark Matter": {
    url: "https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png",
    opts: { maxZoom: 19, attribution: "© OSM · CARTO", subdomains: "abcd" } as any,
    theme: "dark",
  },
  "Voyager": {
    url: "https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}{r}.png",
    opts: { maxZoom: 19, attribution: "© OSM · CARTO", subdomains: "abcd" } as any,
    theme: "light",
  },
  "OSM": {
    url: "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    opts: { maxZoom: 19, attribution: "© OpenStreetMap" },
    theme: "light",
  },
  "OpenTopoMap": {
    url: "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png",
    opts: { maxZoom: 17, attribution: "© OSM · OpenTopoMap", subdomains: "abc" } as any,
    theme: "light",
  },
  "Stamen Terrain": {
    url: "https://tiles.stadiamaps.com/tiles/stamen_terrain/{z}/{x}/{y}{r}.png",
    opts: { maxZoom: 18, attribution: "© Stadia · Stamen · OSM" } as any,
    theme: "light",
  },
  "Satellite": {
    url: "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    opts: { maxZoom: 19, attribution: "© Esri" },
    theme: "dark",
  },
  "Stamen Toner": {
    url: "https://tiles.stadiamaps.com/tiles/stamen_toner/{z}/{x}/{y}{r}.png",
    opts: { maxZoom: 18, attribution: "© Stadia · OSM" } as any,
    theme: "dark",
  },
};
