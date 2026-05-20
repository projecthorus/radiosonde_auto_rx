import { useEffect, useMemo, useRef } from "react";
import uPlot from "uplot";
import type { ScanData } from "@/lib/types";
import { Panel, PanelHead } from "@/components/ui/panel";
import { Waves, RefreshCcw } from "lucide-react";
import { usePrefs, effectiveTheme } from "@/lib/units";
import { Button } from "@/components/ui/button";

// The backend reports an ISO 8601 UTC timestamp. Show it as local HH:MM:SS,
// or a relative tag if it's very recent — the full ISO stays in the title attr.
function formatScanTime(iso: string): string {
  const t = Date.parse(iso);
  if (!isFinite(t)) return iso;
  const ageSec = Math.max(0, (Date.now() - t) / 1000);
  if (ageSec < 60) return `${Math.round(ageSec)}s ago`;
  const d = new Date(t);
  const hh = String(d.getHours()).padStart(2, "0");
  const mm = String(d.getMinutes()).padStart(2, "0");
  const ss = String(d.getSeconds()).padStart(2, "0");
  return `${hh}:${mm}:${ss}`;
}

interface Props {
  data: ScanData;
  onRefresh?: () => void;
  collapsed?: boolean;
  onToggleCollapse?: () => void;
  /** Frequencies in MHz that the user has put on the "always decode" or
   *  "only scan" lists. Rendered as orange dots along the SNR threshold line
   *  so you can see at a glance which channels the scanner is watching. */
  scanList?: number[];
  /** Frequencies in MHz that are explicitly skipped (station.cfg `never_scan`).
   *  Rendered as red chips in the footer so the operator can see which
   *  channels are being deliberately ignored. */
  blockedList?: number[];
  /** Frequencies in MHz currently in active decoding. Drawn as green dots and
   *  green chips to distinguish from raw detections. */
  decodingList?: number[];
  /** Relative SNR threshold in dB from station.cfg → `data.threshold` is the
   *  measured noise floor, the actual detection line is noise_floor +
   *  snrThreshold. Mirrors the OG dashboard's behaviour. */
  snrThreshold?: number;
}

export function ScanChart({ data, onRefresh, collapsed, onToggleCollapse, scanList, blockedList, decodingList, snrThreshold = 10 }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null);
  // Subscribe so the chart rebuilds when the user toggles light/dark — the
  // grid stroke is sampled from --border at mount, so without this the old
  // theme's grid color is rendered against the new theme's background and
  // ends up looking much more contrasty than intended.
  const prefs = usePrefs();
  const themeKey = effectiveTheme(prefs.theme);
  const plotRef = useRef<uPlot | null>(null);

  // Classify a frequency against the scan-list so tooltips can say what it is.
  // scanList = always_scan + always_decode + only_scan from station.cfg —
  // these are entries the scanner is forced to report regardless of signal.
  // A detected freq that matches one of those is conceptually "Always scan",
  // not a raw signal peak.
  const scanSet = useMemo(() => {
    const s = new Set<string>();
    for (const f of scanList || []) s.add(f.toFixed(3));
    return s;
  }, [scanList]);
  const classify = (mhz: number) => (scanSet.has(mhz.toFixed(3)) ? "Always scan" : "Peak");

  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    if (plotRef.current) { plotRef.current.destroy(); plotRef.current = null; }
    el.innerHTML = "";
    if (!data.freq || data.freq.length === 0) return;

    // Production /get_scan_data sends `freq` and `peak_freq` in MHz already
    // (see autorx.scan — scan_result is populated from rtl_power output in MHz).
    // Earlier code divided by 1e6 assuming Hz, which turned 400.05 → 0.0004.
    const freqs = data.freq;
    const peakX = data.peak_freq || [];

    // Detection threshold = noise floor + relative SNR. The line, the orange
    // scan-list dots, and the y-axis range all key off this value.
    const detectionThreshold =
      data.threshold != null && isFinite(snrThreshold)
        ? data.threshold + snrThreshold
        : null;

    // Fix the y-axis range so the threshold line lands in a predictable spot
    // regardless of how the auto-scaler picks padding. Range covers the data
    // and includes margins above (where strong peaks live) and below the
    // detection threshold (so noise floor sits in the lower half).
    let yMin = -Infinity, yMax = Infinity;
    for (let i = 0; i < data.power.length; i++) {
      const v = data.power[i];
      if (!isFinite(v)) continue;
      if (yMin === -Infinity || v < yMin) yMin = v;
      if (yMax === Infinity || v > yMax) yMax = v;
    }
    if (!isFinite(yMin)) yMin = -100;
    if (!isFinite(yMax)) yMax = 0;
    // Make sure the threshold and all peaks are inside the range.
    if (detectionThreshold != null) {
      if (detectionThreshold < yMin) yMin = detectionThreshold;
      if (detectionThreshold > yMax) yMax = detectionThreshold;
    }
    for (const lvl of (data.peak_lvl || [])) {
      if (isFinite(lvl)) {
        if (lvl < yMin) yMin = lvl;
        if (lvl > yMax) yMax = lvl;
      }
    }
    // 3 dB padding top & bottom so points don't sit on the axis line.
    yMin -= 3;
    yMax += 3;

    const css = getComputedStyle(document.documentElement);
    const axisCol = css.getPropertyValue("--muted-foreground") || "195 8% 60%";
    const gridCol = css.getPropertyValue("--border") || "196 20% 17%";

    // Build aligned point series for peaks and scan-list. uPlot needs every
    // series to share the same x-array, so for each freq in `freqs` we look up
    // the nearest peak/scan-list frequency within ~one-bin tolerance and put
    // a y value there, null elsewhere. This makes the dots part of the canvas
    // — no fragile overlay div tricks.
    const binWidth = freqs.length > 1 ? Math.abs(freqs[1] - freqs[0]) : 0.001;
    const tol = Math.max(binWidth, 0.0005);
    const sortedFreqs = freqs;
    const findIdx = (target: number) => {
      // Binary search nearest index in sortedFreqs
      let lo = 0, hi = sortedFreqs.length - 1;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (sortedFreqs[mid] < target) lo = mid + 1; else hi = mid;
      }
      // lo is first index >= target. Compare with lo-1 to find nearest.
      if (lo > 0 && Math.abs(sortedFreqs[lo - 1] - target) < Math.abs(sortedFreqs[lo] - target)) lo--;
      return Math.abs(sortedFreqs[lo] - target) <= tol ? lo : -1;
    };

    // Split peaks into two parallel series: one for freqs currently being
    // decoded (drawn green) and one for everything else (orange).
    // The decoder centre freq from /get_task_list and the scanner's binned
    // peak_freq can disagree by a few kHz (e.g. 404.011 vs 404.01), so use
    // a much looser ±10 kHz tolerance for the decoding match. Sonde channels
    // are spaced ≥ 100 kHz apart so this won't cause false positives.
    const DECODE_TOL = 0.01; // MHz
    const isDecoding = (f: number) => {
      if (!decodingList || decodingList.length === 0) return false;
      for (const d of decodingList) if (Math.abs(d - f) <= DECODE_TOL) return true;
      return false;
    };
    const otherPeaksSeries: (number | null)[] = new Array(freqs.length).fill(null);
    const decodingPeaksSeries: (number | null)[] = new Array(freqs.length).fill(null);
    for (let i = 0; i < peakX.length; i++) {
      const f = peakX[i], l = data.peak_lvl?.[i];
      if (!isFinite(f) || !isFinite(l)) continue;
      const idx = findIdx(f);
      if (idx < 0) continue;
      if (isDecoding(f)) decodingPeaksSeries[idx] = l;
      else otherPeaksSeries[idx] = l;
    }
    // The hover-handler below still wants a single combined view, so build
    // one for back-compat. Source-of-truth for color is the two split arrays.
    const peaksSeries: (number | null)[] = otherPeaksSeries.map((v, i) =>
      v != null ? v : decodingPeaksSeries[i]
    );
    // Horizontal threshold line at detectionThreshold across the whole x range.
    // Done as a series so uPlot draws it on the canvas — no overlay-div tricks.
    const thresholdSeries: (number | null)[] = detectionThreshold != null
      ? new Array(freqs.length).fill(detectionThreshold)
      : new Array(freqs.length).fill(null);

    const opts: uPlot.Options = {
      width: el.clientWidth || 400,
      height: el.clientHeight || 220,
      padding: [8, 8, 4, 8],
      legend: { show: false },
      cursor: { drag: { x: true, y: false } } as any,
      // uPlot wants `range` as a function (or "auto"). Returning a constant
      // gives us a fixed y-axis regardless of data variance.
      // uPlot 1.6: a range function on the y scale overrides auto and gives
      // us a fixed range every time it's recomputed.
      scales: {
        x: {},
        y: { range: (_u, _min, _max) => [yMin, yMax] },
      },
      axes: [
        {
          stroke: `hsl(${axisCol})`,
          grid: { stroke: `hsl(${gridCol} / 0.4)`, width: 1 },
          ticks: { stroke: `hsl(${gridCol} / 0.4)` },
          values: (_u: uPlot, vs: number[]) => vs.map(x => x.toFixed(1) + " MHz"),
          font: "10px 'Plex Mono', monospace",
        },
        {
          stroke: `hsl(${axisCol})`,
          grid: { stroke: `hsl(${gridCol} / 0.4)`, width: 1 },
          ticks: { stroke: `hsl(${gridCol} / 0.4)` },
          values: (_u: uPlot, vs: number[]) => vs.map(x => x.toFixed(0) + " dB"),
          font: "10px 'Plex Mono', monospace",
        },
      ],
      series: [
        {},
        // Spectrum trace
        {
          stroke: "hsl(210 90% 70%)",
          width: 1.4,
          fill: "hsla(210, 90%, 70%, 0.10)",
          // uPlot defaults fillTo=0, which sits above our y-range (typically
          // -90..-20 dB), so the fill ends up clipped to the area *above* the
          // line. Anchor fill to yMin instead so it shades everything below
          // the trace, like a proper waterfall/spectrogram look.
          fillTo: () => yMin,
          points: { show: false },
        } as any,
        // Horizontal threshold line (constant detectionThreshold across all x)
        {
          stroke: "hsl(38 88% 62%)",
          width: 1,
          dash: [4, 4],
          points: { show: false },
        },
        // Orange peak dots at detected freqs NOT currently being decoded.
        {
          label: "Peak",
          stroke: "hsl(38 88% 62%)",
          width: 0,
          points: {
            show: true,
            size: 9,
            stroke: "hsl(38 88% 62%)",
            fill: "hsl(38 88% 62%)",
          } as any,
        },
        // Green dots at freqs currently in active decoding — same shape, the
        // color is the distinguishing signal. `--signal` is hsl(152 70% 67%).
        {
          label: "Decoding",
          stroke: "hsl(152 70% 67%)",
          width: 0,
          points: {
            show: true,
            size: 9,
            stroke: "hsl(152 70% 67%)",
            fill: "hsl(152 70% 67%)",
          } as any,
        },
      ],
    };

    plotRef.current = new uPlot(opts, [freqs, data.power, thresholdSeries, otherPeaksSeries, decodingPeaksSeries], el);

    // ---- Hover tooltip + highlight ring -------------------------------------
    // Only fires when the cursor is actually over a peak dot (within its
    // visual radius). On hit: shows MHz + dB tooltip and draws a bigger
    // outlined ring around the dot.
    const DOT_RADIUS = 5;   // dot is rendered at size 9 → ~4.5px radius
    const HIT_RADIUS = 7;   // a hair larger than the dot to give a forgiving hit zone

    const tip = document.createElement("div");
    tip.style.cssText = "position:absolute;pointer-events:none;font:600 10px 'Plex Mono',monospace;color:hsl(38 88% 62%);background:hsl(195 18% 6% / 0.92);padding:3px 6px;border-radius:4px;border:1px solid hsl(38 88% 62% / 0.4);transform:translate(-50%, -100%);white-space:nowrap;display:none;z-index:60;";
    el.appendChild(tip);

    // Hover-highlight ring: a slightly larger amber dot on top of the hovered peak.
    const ring = document.createElement("div");
    ring.style.cssText = "position:absolute;pointer-events:none;width:14px;height:14px;border-radius:50%;background:hsl(38 88% 62%);box-shadow:0 0 10px hsl(38 88% 62% / 0.8);transform:translate(-50%,-50%);display:none;z-index:4;";
    el.appendChild(ring);

    // Live cursor-frequency readout pinned to the top edge of the chart. Just
    // a small blue text that updates as the mouse moves across the canvas —
    // independent of peak hits, just `posToVal(x)` on the x scale.
    const freqLabel = document.createElement("div");
    freqLabel.style.cssText = "position:absolute;top:4px;left:50%;transform:translateX(-50%);pointer-events:none;font:500 10px 'Plex Mono',monospace;color:hsl(210 90% 70%);white-space:nowrap;display:none;z-index:55;letter-spacing:0.02em;";
    el.appendChild(freqLabel);

    plotRef.current.over.addEventListener("mousemove", (ev) => {
      const u = plotRef.current;
      if (!u) return;
      const rect = u.over.getBoundingClientRect();
      const localX = ev.clientX - rect.left;
      const localY = ev.clientY - rect.top;

      // Always-on cursor freq readout — convert pixel-x to MHz via the x
      // scale and display at the top of the chart.
      const cursorFreq = u.posToVal(localX, "x");
      if (isFinite(cursorFreq)) {
        freqLabel.textContent = `${cursorFreq.toFixed(3)} MHz`;
        freqLabel.style.display = "block";
      } else {
        freqLabel.style.display = "none";
      }

      // Find nearest peak in canvas pixel space.
      let bestI = -1, bestD = Infinity;
      for (let i = 0; i < peaksSeries.length; i++) {
        const v = peaksSeries[i];
        if (v == null) continue;
        const px = u.valToPos(freqs[i], "x", false);
        const py = u.valToPos(v, "y", false);
        const d = Math.hypot(px - localX, py - localY);
        if (d < bestD) { bestD = d; bestI = i; }
      }
      if (bestI >= 0 && bestD <= HIT_RADIUS) {
        const f = freqs[bestI];
        const lvl = peaksSeries[bestI] as number;
        const px = u.valToPos(f, "x", false);
        const py = u.valToPos(lvl, "y", false);
        // The .u-over element is positioned over the canvas; its (0,0) is the
        // canvas top-left. The tip/ring are appended to `el` (the wrapRef
        // outer div) so their coords need to be offset by the .u-over rect.
        const overRect = u.over.getBoundingClientRect();
        const elRect = el.getBoundingClientRect();
        const offX = overRect.left - elRect.left;
        const offY = overRect.top - elRect.top;
        // Match the ring + tooltip color to the underlying dot color so the
        // hover state visually reflects the dot's meaning (decoding=green,
        // peak=orange).
        const decoding = isDecoding(f);
        const dotColor = decoding ? "hsl(152 70% 67%)" : "hsl(38 88% 62%)";
        ring.style.background = dotColor;
        ring.style.boxShadow = `0 0 10px ${decoding ? "hsl(152 70% 67% / 0.8)" : "hsl(38 88% 62% / 0.8)"}`;
        tip.style.color = dotColor;
        tip.style.borderColor = decoding ? "hsl(152 70% 67% / 0.4)" : "hsl(38 88% 62% / 0.4)";
        tip.textContent = `${f.toFixed(3)} MHz · ${lvl.toFixed(1)} dB · ${decoding ? "Decoding" : classify(f)}`;
        // Make visible first (without final position) so we can measure width.
        tip.style.left = "-9999px";
        tip.style.top = `${offY + py - DOT_RADIUS - 2}px`;
        tip.style.display = "block";
        // Clamp the centered tip to the wrap element so it doesn't poke off
        // the chart edge on bars near the left/right border.
        const tipW = tip.offsetWidth;
        const margin = 4;
        const minLeft = margin + tipW / 2;
        const maxLeft = el.clientWidth - margin - tipW / 2;
        let nx = offX + px;
        if (nx < minLeft) nx = minLeft;
        if (nx > maxLeft) nx = maxLeft;
        tip.style.left = `${nx}px`;
        ring.style.left = `${offX + px}px`;
        ring.style.top = `${offY + py}px`;
        ring.style.display = "block";
        u.over.style.cursor = "pointer";
      } else {
        tip.style.display = "none";
        ring.style.display = "none";
        u.over.style.cursor = "";
      }
    });
    plotRef.current.over.addEventListener("mouseleave", () => {
      freqLabel.style.display = "none";
      tip.style.display = "none";
      ring.style.display = "none";
    });

    // After mount, snap to real container width (parent may not have been laid out at init time).
    requestAnimationFrame(() => {
      if (plotRef.current && el.clientWidth) plotRef.current.setSize({ width: el.clientWidth, height: el.clientHeight || 220 });
    });

    // Threshold line, peaks, and scan-list dots are all native uPlot series
    // now — drawn on the canvas, sized by uPlot's coordinate system. No more
    // overlay-div valToPos trickery (it was unreliable on first paint).

    const ro = new ResizeObserver(() => {
      if (plotRef.current && el.clientWidth) {
        plotRef.current.setSize({ width: el.clientWidth, height: el.clientHeight || 220 });
      }
    });
    ro.observe(el);
    return () => { ro.disconnect(); plotRef.current?.destroy(); plotRef.current = null; };
  }, [data, scanList, decodingList, snrThreshold, themeKey]);

  // When un-collapsed, the chart was hidden so its width is back to whatever
  // the parent gives it — tell uPlot to redraw at the right size.
  useEffect(() => {
    if (collapsed) return;
    const el = wrapRef.current;
    if (!el || !plotRef.current) return;
    const id = setTimeout(() => {
      if (plotRef.current && el.clientWidth) {
        plotRef.current.setSize({ width: el.clientWidth, height: el.clientHeight || 220 });
      }
    }, 50);
    return () => clearTimeout(id);
  }, [collapsed]);

  // `freq` and `peak_freq` are already in MHz from the backend.
  // These are signal candidates — frequencies the scanner found activity on
  // above the SNR threshold. Some may be filtered out by never_scan (rendered
  // separately as red chips below).
  const detectedFreqs = (data.peak_freq || []).map(f => f.toFixed(3)).sort();
  const detectedCount = detectedFreqs.length;
  // Chip coloring: match within ±10 kHz of any decoding freq. The chip
  // strings are 3-decimal-MHz; build a Set of every 1-kHz value within tol of
  // each decoder centre. e.g. 404.011 expands to {404.001..404.021}.
  const decodingSet = new Set<string>();
  for (const d of (decodingList || [])) {
    for (let k = -10; k <= 10; k++) decodingSet.add((d + k / 1000).toFixed(3));
  }
  // `Math.min(...arr)` blows the JS arg-list limit at ~100k entries — scans
  // are normally much smaller, but use a manual loop so we never trip it.
  let scanLo = Infinity, scanHi = -Infinity;
  if (data.freq && data.freq.length) {
    for (let i = 0; i < data.freq.length; i++) {
      const v = data.freq[i];
      if (v < scanLo) scanLo = v;
      if (v > scanHi) scanHi = v;
    }
  }
  const range = isFinite(scanLo) ? `${scanLo.toFixed(2)} – ${scanHi.toFixed(2)} MHz` : "—";
  // Only show blocked freqs that actually fall within the current scan range
  // — never_scan entries for unrelated bands would just clutter the panel.
  const blockedFreqs = (blockedList || [])
    .filter(f => isFinite(scanLo) && f >= scanLo && f <= scanHi)
    .map(f => f.toFixed(3))
    .sort();
  const blockedCount = blockedFreqs.length;

  return (
    <Panel>
      <PanelHead
        title="Spectrum"
        icon={<Waves className="w-3.5 h-3.5" strokeWidth={1.75} />}
        actions={
          <Button size="icon-sm" variant="ghost" onClick={onRefresh} title="Refresh">
            <RefreshCcw className="w-3 h-3" />
          </Button>
        }
        collapsed={collapsed}
        onToggleCollapse={onToggleCollapse}
      />
      {/* Keep wrapRef mounted across collapse/expand — destroying it would
          orphan the uPlot instance, and the data-driven useEffect wouldn't
          re-run because `data` hasn't changed on collapse toggle. */}
      <div className={collapsed ? "hidden" : "contents"}>
        <div ref={wrapRef} className="relative h-[260px] w-full overflow-hidden bg-background/40" />
        <div className="flex flex-wrap items-center gap-x-4 gap-y-1 px-3 py-2 border-t border-border bg-background/30 text-[10px] mono text-muted-foreground">
          <span><span className="text-foreground/70">Range</span> {range}</span>
          <span><span className="text-foreground/70">Detected</span> {detectedCount}</span>
          {blockedCount > 0 && (
            <span><span className="text-foreground/70">Ignored</span> {blockedCount}</span>
          )}
          <span><span className="text-foreground/70">SNR ≥</span> {data.threshold ?? "—"} dB</span>
          <span className="flex-1 min-w-0 text-right truncate" title={data.timestamp || ""}>
            {data.timestamp ? `Last scan ${formatScanTime(data.timestamp)}` : "awaiting scan…"}
          </span>
          {detectedFreqs.length > 0 && (
            <div className="basis-full flex flex-wrap gap-1 pt-1 border-t border-border/40">
              {detectedFreqs.map(f => (
                <span
                  key={`d-${f}`}
                  className={decodingSet.has(f)
                    ? "px-1.5 py-0.5 rounded bg-signal/[0.14] text-signal"
                    : "px-1.5 py-0.5 rounded bg-warn/[0.12] text-warn"}
                  title={`${decodingSet.has(f) ? "Decoding" : classify(parseFloat(f))} · ${f} MHz`}
                >{f}</span>
              ))}
            </div>
          )}
          {blockedFreqs.length > 0 && (
            <div className="basis-full flex flex-wrap gap-1 pt-1">
              {blockedFreqs.map(f => (
                <span key={`b-${f}`} className="px-1.5 py-0.5 rounded bg-alert/[0.12] text-alert line-through decoration-alert/40" title={`Ignored (never_scan) at ${f} MHz`}>{f}</span>
              ))}
            </div>
          )}
        </div>
      </div>
    </Panel>
  );
}
