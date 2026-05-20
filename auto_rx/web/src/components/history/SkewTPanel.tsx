import { useEffect, useMemo, useRef, useState } from "react";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription } from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { loadSkewT } from "@/lib/loadSkewT";
import { apiPostForm } from "@/lib/api";
import { fmtTemp } from "@/lib/units";
import { toast } from "sonner";
import { Loader2, RefreshCw } from "lucide-react";

interface Props {
  open: boolean;
  serial: string | null;
  onOpenChange: (v: boolean) => void;
}

/**
 * Skew-T atmospheric chart for one selected sonde.
 *
 * Uses the legacy d3v3 + skewt.js assets (loaded on demand). The chart instance
 * is recreated per-open so we don't leak SVG between sondes.
 */
export function SkewTPanel({ open, serial, onOpenChange }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<any>(null);
  const [loading, setLoading] = useState(false);
  const [decim, setDecim] = useState(25);
  const [launchInfo, setLaunchInfo] = useState<string>("");

  // Build / rebuild the chart when the dialog opens.
  useEffect(() => {
    if (!open || !serial) return;
    let aborted = false;
    (async () => {
      try {
        await loadSkewT();
      } catch (e: any) {
        toast.error(e?.message || "Failed to load Skew-T plotter");
        return;
      }
      if (aborted || !wrapRef.current) return;
      // The legacy skewt.js reads its container width via getComputedStyle inside
      // its constructor. Inside an animating Radix Dialog, that read can return
      // "auto" → parseInt → NaN. Wait until the dialog actually has pixel width
      // before constructing the chart.
      const waitForWidth = async (): Promise<number> => {
        for (let i = 0; i < 60; i++) {
          await new Promise<void>(r => requestAnimationFrame(() => r()));
          if (aborted || !wrapRef.current) return 0;
          const w = wrapRef.current.getBoundingClientRect().width;
          if (w > 50) return w;
        }
        return 0;
      };
      const parentW = await waitForWidth();
      if (aborted || !wrapRef.current) return;
      if (parentW < 50) {
        toast.error(`Skew-T container never got a measurable width.`);
        return;
      }

      wrapRef.current.innerHTML = "";
      const inner = document.createElement("div");
      inner.id = `skewt-${Math.random().toString(36).slice(2)}`;
      // skewt.js sets height = width (square aspect), so cap by viewport height
      // to avoid overflowing the dialog. ~200px allowance for chrome + header.
      const maxByH = window.innerHeight - 220;
      const px = Math.max(360, Math.min(Math.floor(parentW), maxByH));
      inner.style.width = `${px}px`;
      inner.style.height = `${px}px`;
      inner.style.margin = "0 auto";
      inner.style.display = "block";
      wrapRef.current.appendChild(inner);
      // Force layout so getComputedStyle returns "<px>px" rather than "auto".
      void inner.offsetWidth;
      // Sanity check what skewt.js will read — bail before it explodes.
      const computed = parseInt(window.getComputedStyle(inner).width, 10);
      if (!isFinite(computed) || computed < 50) {
        toast.error(`Container computed width is ${window.getComputedStyle(inner).width} — Skew-T can't initialise.`);
        return;
      }
      chartRef.current = new window.SkewT("#" + inner.id);
      await fetchAndPlot();
    })();
    return () => { aborted = true; chartRef.current = null; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, serial]);

  const fetchAndPlot = async () => {
    if (!serial || !chartRef.current) return;
    setLoading(true);
    try {
      // /get_log_detail returns JSON-as-text; apiPostForm gives us the body as a
      // string and we parse it here (keeps the same wire format as the OG UI).
      const raw = await apiPostForm("/get_log_detail", { serial, decimation: String(decim) });
      const data = JSON.parse(raw);
      chartRef.current.clear();
      chartRef.current.plot(data.skewt || []);
      const first = (data.skewt || [])[0];
      if (first) {
        setLaunchInfo(`${(data.skewt || []).length} pts · surface ${first.press?.toFixed?.(0) ?? "—"} hPa, ${fmtTemp(first.temp)}`);
      } else {
        setLaunchInfo("No atmospheric data");
      }
    } catch (e: any) {
      toast.error("Skew-T fetch failed: " + (e.message || ""));
    }
    setLoading(false);
  };

  const title = useMemo(() => serial ? `Skew-T · ${serial}` : "Skew-T", [serial]);

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-4xl">
        <DialogHeader>
          <DialogTitle>{title}</DialogTitle>
          <DialogDescription>
            Atmospheric profile rendered from this sonde's stored log. Drag the slider to thin out
            data points (higher = fewer points, faster render).
          </DialogDescription>
        </DialogHeader>

        <div className="flex items-center gap-3 text-[11px] mono">
          <label className="flex items-center gap-2 flex-1">
            <span className="text-muted-foreground">Decimation</span>
            <input
              type="range" min={1} max={100} step={1}
              value={decim} onChange={e => setDecim(parseInt(e.target.value, 10))}
              className="flex-1"
            />
            <span className="w-8 text-right text-foreground/80">{decim}</span>
          </label>
          <Button size="sm" variant="primary" onClick={fetchAndPlot} disabled={loading || !serial}>
            {loading ? <Loader2 className="w-3 h-3 animate-spin" /> : <RefreshCw className="w-3 h-3" />}
            Regenerate
          </Button>
        </div>

        <div className="rounded-md border border-border bg-card overflow-auto relative max-h-[calc(100vh-220px)]">
          {/* This div is owned by React but kept empty — the legacy d3v3
              skewt.js mutates it imperatively. Anything React renders inside
              would clobber the chart on the next state change. */}
          <div ref={wrapRef} className="bg-white w-full" />
          {(!serial || loading) && (
            <div className="absolute inset-0 flex items-center justify-center text-muted-foreground text-xs pointer-events-none">
              {!serial ? "Select a sonde first." : "Loading…"}
            </div>
          )}
        </div>
        {launchInfo && <div className="text-[10px] mono text-muted-foreground">{launchInfo}</div>}
      </DialogContent>
    </Dialog>
  );
}
