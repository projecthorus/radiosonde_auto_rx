/**
 * One-shot loader for the legacy d3 v3 + skewt.js + skewt.css assets.
 * These are already served by Flask out of /static/, so we just inject script
 * and link tags on first use and resolve once they're all loaded.
 *
 * Both libraries attach themselves to the global scope (window.d3, window.SkewT)
 * which is what skewt.js expects.
 */
let loadPromise: Promise<void> | null = null;

function loadScript(src: string): Promise<void> {
  return new Promise((resolve, reject) => {
    if (document.querySelector(`script[data-skewt="${src}"]`)) return resolve();
    const s = document.createElement("script");
    s.src = src;
    s.async = false;
    s.dataset.skewt = src;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error(`Failed to load ${src}`));
    document.head.appendChild(s);
  });
}

function loadCss(href: string) {
  if (document.querySelector(`link[data-skewt="${href}"]`)) return;
  const l = document.createElement("link");
  l.rel = "stylesheet";
  l.href = href;
  l.dataset.skewt = href;
  document.head.appendChild(l);
}

export function loadSkewT(): Promise<void> {
  if (loadPromise) return loadPromise;
  loadPromise = (async () => {
    loadCss("/static/css/skewt.css");
    // d3 v3 must be available before skewt.js evaluates.
    await loadScript("/static/js/d3.3.5.3.min.js");
    await loadScript("/static/js/skewt.js");
  })();
  return loadPromise;
}

// Minimal shim for TS — the SkewT global is defined by skewt.js
declare global {
  interface Window {
    SkewT: any;
    d3: any;
    jQuery?: any;
    $?: any;
  }
}
