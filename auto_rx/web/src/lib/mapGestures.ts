import type { Map as LMap } from "leaflet";

// On touch devices, require a two-finger gesture to pan/zoom the map so a
// single-finger drag scrolls the page instead of getting captured by Leaflet.
// On desktop/mouse this is a no-op.
export function enableTwoFingerPan(map: LMap, container: HTMLElement): () => void {
  const isTouch = matchMedia("(hover: none) and (pointer: coarse)").matches;
  if (!isTouch) return () => {};

  map.dragging.disable();
  container.style.touchAction = "pan-y";

  let hintEl: HTMLDivElement | null = null;
  let hintTimer: number | null = null;
  const showHint = () => {
    if (!hintEl) {
      hintEl = document.createElement("div");
      hintEl.textContent = "Use two fingers to move the map";
      hintEl.style.cssText =
        "position:absolute;inset:0;display:flex;align-items:center;justify-content:center;" +
        "background:rgba(0,0,0,0.55);color:#fff;font:500 13px/1.3 system-ui,sans-serif;" +
        "z-index:500;pointer-events:none;text-align:center;padding:0 1rem;" +
        "opacity:0;transition:opacity 120ms ease;";
      container.appendChild(hintEl);
    }
    requestAnimationFrame(() => { if (hintEl) hintEl.style.opacity = "1"; });
    if (hintTimer) window.clearTimeout(hintTimer);
    hintTimer = window.setTimeout(() => {
      if (hintEl) hintEl.style.opacity = "0";
    }, 900);
  };

  const onTouchStart = (e: TouchEvent) => {
    if (e.touches.length >= 2) {
      map.dragging.enable();
      container.style.touchAction = "none";
    } else {
      showHint();
    }
  };
  const onTouchEnd = (e: TouchEvent) => {
    if (e.touches.length < 2) {
      map.dragging.disable();
      container.style.touchAction = "pan-y";
    }
  };

  container.addEventListener("touchstart", onTouchStart, { passive: true });
  container.addEventListener("touchend", onTouchEnd, { passive: true });
  container.addEventListener("touchcancel", onTouchEnd, { passive: true });

  return () => {
    container.removeEventListener("touchstart", onTouchStart);
    container.removeEventListener("touchend", onTouchEnd);
    container.removeEventListener("touchcancel", onTouchEnd);
    if (hintTimer) window.clearTimeout(hintTimer);
    if (hintEl && hintEl.parentNode) hintEl.parentNode.removeChild(hintEl);
    hintEl = null;
  };
}
