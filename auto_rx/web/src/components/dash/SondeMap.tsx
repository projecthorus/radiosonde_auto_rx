import { useEffect, useMemo, useRef, useState } from "react";
import L, { type LayerGroup, type Map as LMap, type Marker, type Polyline, type TileLayer } from "leaflet";
import type { SondeTelemetry } from "@/lib/types";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Button } from "@/components/ui/button";
import { LocateFixed, MapPin, Maximize2, Minimize2, ChevronDown, ChevronRight } from "lucide-react";
import { fmtAlt, fmtFreq, fmtTime, usePrefs, setPrefs, effectiveTheme } from "@/lib/units";
import { enableTwoFingerPan } from "@/lib/mapGestures";
import { toast } from "sonner";

import { TILES, AUTO_TILE, resolveTile } from "@/lib/tiles";
import { loadLeafletPlugins } from "@/lib/leafletPlugins";

// OG v1 balloon/parachute sprites. Six named colors; v1 cycled them per new
// sonde — we hash the sonde id so each sonde always lands on the same color
// across reloads, which is more useful when monitoring multiple flights.
import balloonRed from "@/assets/markers/balloon-red.png";
import balloonGreen from "@/assets/markers/balloon-green.png";
import balloonBlue from "@/assets/markers/balloon-blue.png";
import balloonPurple from "@/assets/markers/balloon-purple.png";
import balloonYellow from "@/assets/markers/balloon-yellow.png";
import balloonCyan from "@/assets/markers/balloon-cyan.png";
import parachuteRed from "@/assets/markers/parachute-red.png";
import parachuteGreen from "@/assets/markers/parachute-green.png";
import parachuteBlue from "@/assets/markers/parachute-blue.png";
import parachutePurple from "@/assets/markers/parachute-purple.png";
import parachuteYellow from "@/assets/markers/parachute-yellow.png";
import parachuteCyan from "@/assets/markers/parachute-cyan.png";

const BALLOON_COLORS = ["red", "green", "blue", "purple", "yellow", "cyan"] as const;
type BalloonColor = typeof BALLOON_COLORS[number];

const BALLOON_SPRITES: Record<BalloonColor, string> = {
  red: balloonRed, green: balloonGreen, blue: balloonBlue,
  purple: balloonPurple, yellow: balloonYellow, cyan: balloonCyan,
};
const PARACHUTE_SPRITES: Record<BalloonColor, string> = {
  red: parachuteRed, green: parachuteGreen, blue: parachuteBlue,
  purple: parachutePurple, yellow: parachuteYellow, cyan: parachuteCyan,
};

// Pick a stable color for a sonde id by hashing — same id always yields the
// same color, so a sonde re-appearing keeps its identity on the map.
function balloonColorFor(id: string): BalloonColor {
  let h = 0;
  for (let i = 0; i < id.length; i++) h = ((h << 5) - h + id.charCodeAt(i)) | 0;
  return BALLOON_COLORS[Math.abs(h) % BALLOON_COLORS.length];
}

function escapeHtml(s: string): string {
  return String(s).replace(/[&<>"']/g, c => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[c]!));
}

// Popup body for a sonde marker. Extracted so we can refresh the content
// on every telemetry tick (live altitude etc.) — not just at marker create.
function buildPopupHtml(s: SondeTelemetry, color: string): string {
  const safeId = escapeHtml(s.id);
  const safeType = escapeHtml(s.type || "");
  // s.datetime is the timestamp of the frame this telemetry update came from;
  // popup content is refreshed on every telemetry tick (see marker update
  // branch) so it ticks live as new packets arrive.
  const timeLine = s.datetime ? `<br><span style="color:hsl(var(--muted-foreground));">${escapeHtml(fmtTime(s.datetime))}</span>` : "";
  return `<div style="font-family:var(--font-mono);"><b style="color:${color}">${safeId}</b><br>${safeType} · ${fmtFreq(s.freq)}<br>alt ${fmtAlt(s.alt)}${timeLine}</div>`;
}

function triangleIcon(color: string, descending = false) {
  // Triangle pointing up (ascent) or down (descent), in the sonde's colour.
  const rot = descending ? 180 : 0;
  return L.divIcon({
    className: "",
    iconSize: [22, 22],
    iconAnchor: [11, 11],
    html: `<div style="position:relative;width:22px;height:22px;display:flex;align-items:center;justify-content:center;transform:rotate(${rot}deg);">
      <svg width="22" height="22" viewBox="0 0 22 22" style="filter:drop-shadow(0 0 6px ${color}aa);">
        <polygon points="11,2 20,19 2,19" fill="${color}" stroke="hsl(195 18% 6%)" stroke-width="2" stroke-linejoin="round"/>
        <circle cx="11" cy="13" r="2.5" fill="hsl(195 18% 6%)"/>
      </svg>
    </div>`,
  });
}

function balloonIcon(id: string, descending = false) {
  // Use the OG v1 PNG sprites. iconSize/Anchor match the original (46x84/85
  // with anchor at [23,76]) so the bottom of the string/payload sits on the
  // sonde's lat/lon — same hit point users were used to in v1.
  const palette = balloonColorFor(id);
  const url = descending ? PARACHUTE_SPRITES[palette] : BALLOON_SPRITES[palette];
  return L.icon({
    iconUrl: url,
    iconSize: descending ? [46, 84] : [46, 85],
    iconAnchor: [23, 76],
  });
}

function sondeIcon(id: string, color: string, descending: boolean, style: "triangle" | "balloon") {
  return style === "balloon" ? balloonIcon(id, descending) : triangleIcon(color, descending);
}

// Triangle pointing up — pinned at "first heard" position. Mirrors the
// History page's launchIcon so the visual vocabulary is the same.
function launchIcon(color: string) {
  return L.divIcon({
    className: "",
    iconSize: [16, 16],
    iconAnchor: [8, 14],
    html: `<svg width="16" height="16" viewBox="0 0 16 16" style="filter:drop-shadow(0 0 5px ${color}aa);">
      <polygon points="8,1 14,14 2,14" fill="${color}" stroke="hsl(195 18% 6%)" stroke-width="1.5" stroke-linejoin="round"/>
    </svg>`,
  });
}
// Star marker for the apex / burst point — same orange as the OG.
function burstIcon() {
  return L.divIcon({
    className: "",
    iconSize: [18, 18],
    iconAnchor: [9, 9],
    html: `<svg width="18" height="18" viewBox="0 0 18 18" style="filter:drop-shadow(0 0 6px hsl(38 90% 60% / 0.7));">
      <polygon points="9,1 11,7 17,7 12,11 14,17 9,13 4,17 6,11 1,7 7,7"
        fill="hsl(38 88% 62%)" stroke="hsl(195 18% 6%)" stroke-width="1.2" stroke-linejoin="round"/>
    </svg>`,
  });
}

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

interface Props {
  sondes: Record<string, SondeTelemetry>;
  station?: { lat: number; lon: number } | null;
  follow?: string | null;
  highlight?: string | null;
  className?: string;
  collapsed?: boolean;
  onToggleCollapse?: () => void;
}

export function SondeMap({ sondes, station, follow, highlight, className, collapsed, onToggleCollapse }: Props) {
  const ref = useRef<HTMLDivElement>(null);
  const mapRef = useRef<LMap | null>(null);
  const tileRef = useRef<TileLayer | null>(null);
  const stationRef = useRef<Marker | null>(null);
  const layerRef = useRef<LayerGroup | null>(null);
  const markersRef = useRef<Record<string, {
    marker: Marker;
    path: Polyline;
    los: Polyline | null;
    descending: boolean;
    style: "triangle" | "balloon";
    /** Marker pinned at the first lat/lon we ever received for this sonde. */
    firstMarker: Marker | null;
    /** Marker at the apex of the path, only placed once descent is confirmed. */
    burstMarker: Marker | null;
    /** Index into path[] where the apex sits. Only meaningful after burst. */
    burstIdx: number;
  }>>({});
  const wrapRef = useRef<HTMLDivElement>(null);
  const [fullscreen, setFullscreen] = useState(false);
  // Flips true once the async leaflet-plugin loader has finished and the
  // map is wired up. Effects that need the map (station centering, follow
  // pan, sonde markers) gate on this so they're guaranteed to re-run after
  // the map is actually usable — fixes the "world view, never zooms in"
  // race when station / telemetry land before plugin load completes.
  const [mapReady, setMapReady] = useState(false);
  const prefs = usePrefs();

  // Toggle fullscreen and the M key shortcut.
  const toggleFullscreen = () => {
    setFullscreen(v => {
      const next = !v;
      // Let CSS class take effect first, then invalidate the map size.
      setTimeout(() => mapRef.current?.invalidateSize(), 30);
      return next;
    });
  };
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== "m" && e.key !== "M") return;
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable)) return;
      e.preventDefault();
      toggleFullscreen();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Leaflet needs to recompute its tile grid whenever its container resizes.
  // Layout changes from collapse, fullscreen, window resize, or the grid
  // flipping between one and two columns can all trigger this — a single
  // ResizeObserver covers all of them.
  useEffect(() => {
    if (!ref.current) return;
    const ro = new ResizeObserver(() => {
      mapRef.current?.invalidateSize({ animate: false });
    });
    ro.observe(ref.current);
    return () => ro.disconnect();
  }, []);

  // Also explicitly kick a size-recompute on collapse-toggle, in case the
  // ResizeObserver fires before the new layout has settled.
  useEffect(() => {
    if (collapsed) return;
    const t1 = setTimeout(() => mapRef.current?.invalidateSize({ animate: false }), 50);
    const t2 = setTimeout(() => mapRef.current?.invalidateSize({ animate: false }), 250);
    return () => { clearTimeout(t1); clearTimeout(t2); };
  }, [collapsed]);

  // Init
  useEffect(() => {
    if (!ref.current || mapRef.current) return;
    let cancelled = false;
    let teardownGestures: () => void = () => {};

    // Wait for the no-gap / edge-buffer plugins before adding the tile layer,
    // so the patched render path is in place from the first tile.
    loadLeafletPlugins().finally(() => {
      if (cancelled || !ref.current) return;
      const m = L.map(ref.current, { zoomControl: true, attributionControl: true });
      if (station) m.setView([station.lat, station.lon], 7);
      else m.setView([0, 0], 2);
      mapRef.current = m;
      layerRef.current = L.layerGroup().addTo(m);
      const initTile = resolveTile(prefs.tile, effectiveTheme(prefs.theme)).tile;
      tileRef.current = L.tileLayer(initTile.url, {
        ...initTile.opts,
        // edge-buffer plugin option: keep extra tiles around the viewport so
        // panning doesn't show empty areas before they load
        edgeBufferTiles: 2,
      } as any).addTo(m);
      teardownGestures = enableTwoFingerPan(m, ref.current);
      setMapReady(true);
    });
    return () => {
      cancelled = true;
      teardownGestures();
      mapRef.current?.remove();
      mapRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Tile swap — re-runs when the user changes tile, *or* when the effective
  // theme changes (so "Auto" follows light/dark live).
  const eff = effectiveTheme(prefs.theme);
  useEffect(() => {
    const m = mapRef.current;
    if (!m) return;
    if (tileRef.current) m.removeLayer(tileRef.current);
    const t = resolveTile(prefs.tile, eff).tile;
    tileRef.current = L.tileLayer(t.url, t.opts).addTo(m);
  }, [prefs.tile, eff]);

  // Station marker
  const didInitialCenter = useRef(false);
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
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [station, mapReady]);

  // Sonde markers
  useEffect(() => {
    const m = mapRef.current;
    const layer = layerRef.current;
    if (!m || !layer) return;
    const ids = new Set(Object.keys(sondes));

    // remove gone
    for (const id of Object.keys(markersRef.current)) {
      if (!ids.has(id)) {
        const e = markersRef.current[id];
        layer.removeLayer(e.marker); layer.removeLayer(e.path);
        if (e.los) layer.removeLayer(e.los);
        if (e.firstMarker) layer.removeLayer(e.firstMarker);
        if (e.burstMarker) layer.removeLayer(e.burstMarker);
        delete markersRef.current[id];
      }
    }

    // add / update
    for (const id of ids) {
      const s = sondes[id];
      if (s.lat == null || s.lon == null) continue;
      const ll: L.LatLngExpression = [s.lat, s.lon];
      const color = s.color || "#6ee7a4";
      const path = s.path || [];
      const existing = markersRef.current[id];
      const descending = (s.vel_v ?? 0) < -0.1;
      if (existing) {
        existing.marker.setLatLng(ll);
        // Reissue setIcon when either direction or the user's marker-style
        // preference changed. Cheap; runs on every effect tick.
        if (existing.descending !== descending || existing.style !== prefs.markerStyle) {
          existing.marker.setIcon(sondeIcon(id, color, descending, prefs.markerStyle));
          existing.descending = descending;
          existing.style = prefs.markerStyle;
        }
        // Refresh the popup content with the latest telemetry so altitude (etc.)
        // updates live while a popup is open.
        existing.marker.setPopupContent(buildPopupHtml(s, color));
        (existing.path as Polyline).setLatLngs(path.map(p => [p[0], p[1]]) as any);
        if (station) {
          if (existing.los) existing.los.setLatLngs([[station.lat, station.lon], ll] as any);
          else {
            existing.los = L.polyline([[station.lat, station.lon], ll] as any, { color, weight: 1.2, opacity: 0.55, dashArray: "5,5" }).addTo(layer);
          }
        } else if (existing.los) {
          layer.removeLayer(existing.los); existing.los = null;
        }
        // First-heard marker — prefer the authoritative position from the
        // log file (s.first_pos, set by hydratePath). Falls back to path[0]
        // if hydration hasn't completed yet.
        const firstLL: [number, number] | null = s.first_pos
          ? [s.first_pos[0], s.first_pos[1]]
          : (path.length > 0 ? [path[0][0], path[0][1]] : null);
        if (firstLL) {
          if (!existing.firstMarker) {
            existing.firstMarker = L.marker(firstLL, { icon: launchIcon(color), zIndexOffset: 800 })
              .addTo(layer);
          } else {
            existing.firstMarker.setLatLng(firstLL);
          }
          existing.firstMarker.bindPopup(
            `<b>${escapeHtml(s.id)}</b><br>first heard${s.first_time ? `<br>${escapeHtml(fmtTime(s.first_time))}` : ""}`
          );
        }
        // Burst marker — only when the log has recorded a burst position
        // AND the sonde has actually descended from it. Older backends report
        // "burst" as argmax(altitude) unconditionally, so a still-climbing
        // sonde would plant a burst star on top of the live marker. Require
        // current alt to be meaningfully below recorded burst alt (200 m
        // absorbs GPS jitter near apex while still catching real bursts within
        // a few seconds of descent).
        if (s.burst_pos && s.alt < s.burst_pos[2] - 200) {
          const burstLL: [number, number] = [s.burst_pos[0], s.burst_pos[1]];
          const burstAlt = s.burst_pos[2];
          if (!existing.burstMarker) {
            existing.burstMarker = L.marker(burstLL, { icon: burstIcon(), zIndexOffset: 900 })
              .addTo(layer);
          } else {
            existing.burstMarker.setLatLng(burstLL);
          }
          existing.burstMarker.bindPopup(
            `<b>${escapeHtml(s.id)}</b><br>burst @ ${fmtAlt(burstAlt)}${s.burst_time ? `<br>${escapeHtml(fmtTime(s.burst_time))}` : ""}`
          );
        } else if (existing.burstMarker) {
          // Tear down a stale marker (e.g. from an older build that planted
          // one prematurely) so a hard refresh isn't needed.
          layer.removeLayer(existing.burstMarker);
          existing.burstMarker = null;
        }
      } else {
        const marker = L.marker(ll, { icon: sondeIcon(id, color, descending, prefs.markerStyle), riseOnHover: true })
          .bindPopup(buildPopupHtml(s, color));
        marker.addTo(layer);
        const poly = L.polyline(path.map(p => [p[0], p[1]]) as any, { color, weight: 2, opacity: 0.85 }).addTo(layer);
        let los: Polyline | null = null;
        if (station) los = L.polyline([[station.lat, station.lon], ll] as any, { color, weight: 1.2, opacity: 0.55, dashArray: "5,5" }).addTo(layer);
        // First-heard and burst markers are created in the `existing` branch
        // on the next tick once `s.first_pos` / `s.burst_pos` arrive from
        // hydratePath — that way they always plant at the authoritative
        // log positions, not at path[0] which may be mid-flight.
        markersRef.current[id] = {
          marker, path: poly, los, descending, style: prefs.markerStyle,
          firstMarker: null, burstMarker: null, burstIdx: -1,
        };
      }
    }
  }, [sondes, station, prefs.markerStyle]);

  // Highlight: when one sonde is "selected" via right-click, dim the others
  // and brighten the chosen one.
  useEffect(() => {
    for (const [id, e] of Object.entries(markersRef.current)) {
      const isFocus = highlight === id;
      const dim = highlight != null && !isFocus;
      e.path.setStyle({
        opacity: dim ? 0.18 : (isFocus ? 1.0 : 0.85),
        weight: isFocus ? 3 : 2,
        color: isFocus ? "#ffffff" : (sondes[id]?.color || "#6ee7a4"),
      });
      if (e.los) e.los.setStyle({ opacity: dim ? 0.08 : 0.55 });
      const el = (e.marker as any).getElement?.() as HTMLElement | undefined;
      if (el) el.style.opacity = dim ? "0.3" : "1";
    }
  }, [highlight, sondes]);

  // Follow
  useEffect(() => {
    const m = mapRef.current;
    if (!m || !mapReady) return;
    const target = follow ? sondes[follow] : null;
    if (target && target.lat != null) {
      // If the map is still at world view (because no station was set on
      // mount), zoom in instead of just panning — otherwise the user sees
      // a microscopic pin on a global map.
      if (m.getZoom() <= 3) m.setView([target.lat, target.lon], 10, { animate: true });
      else m.panTo([target.lat, target.lon], { animate: true });
    }
  }, [follow, sondes, mapReady]);

  const recenterStation = () => {
    const m = mapRef.current;
    if (!m) return;
    if (!station) { toast.info("Set your station latitude/longitude in Settings → Station first."); return; }
    m.setView([station.lat, station.lon], 7, { animate: true });
  };

  // List of choosable tile names — "Auto" leads, then the explicit providers.
  const tileNames = useMemo(() => [AUTO_TILE, ...Object.keys(TILES)], []);

  return (
    <div
      ref={wrapRef}
      className={
        (fullscreen
          ? "fixed inset-0 z-[1200] bg-background"
          : className) + " flex flex-col"
      }
    >
      <div className="relative z-[401] flex items-center justify-between h-9 px-3 border-b border-border bg-card flex-shrink-0">
        <div className="flex items-center gap-2">
          <MapPin className="w-3.5 h-3.5 text-muted-foreground/80" strokeWidth={1.75} />
          <h3 className="text-[10px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">Sky View</h3>
          <span className="badge mono text-[10px] text-muted-foreground/70">{Object.keys(sondes).length} active</span>
        </div>
        <div className="flex items-center gap-1.5">
          <div className="hidden sm:block w-32">
            <Select value={prefs.tile} onValueChange={v => setPrefs({ tile: v })}>
              <SelectTrigger className="h-6 text-[11px]">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {tileNames.map(n => <SelectItem key={n} value={n}>{n}</SelectItem>)}
              </SelectContent>
            </Select>
          </div>
          <Button size="icon-sm" variant="ghost" onClick={recenterStation} title="Center on station"><LocateFixed className="w-3.5 h-3.5" /></Button>
          <Button size="icon-sm" variant="ghost" onClick={toggleFullscreen} title={fullscreen ? "Exit fullscreen (M)" : "Fullscreen (M)"}>
            {fullscreen ? <Minimize2 className="w-3.5 h-3.5" /> : <Maximize2 className="w-3.5 h-3.5" />}
          </Button>
          {onToggleCollapse && (
            <button
              type="button"
              onClick={onToggleCollapse}
              aria-expanded={!collapsed}
              aria-label={collapsed ? "Expand map" : "Collapse map"}
              className="ml-1 inline-flex items-center justify-center w-6 h-6 rounded text-muted-foreground hover:text-foreground hover:bg-accent/50 transition-colors"
            >
              {collapsed
                ? <ChevronRight className="w-3.5 h-3.5" />
                : <ChevronDown className="w-3.5 h-3.5" />}
            </button>
          )}
        </div>
      </div>
      {/* The map div must stay mounted across collapse/expand — destroying
          and re-mounting it loses the Leaflet instance, and we'd have to
          re-init / re-add all markers. CSS-hide it instead. */}
      <div
        ref={ref}
        className={collapsed ? "hidden" : "flex-1 min-h-[400px]"}
        role="application"
        aria-label="Sonde tracking map"
      />
    </div>
  );
}
