import type { TaskList } from "@/lib/types";
import { Cpu, RefreshCcw, Crosshair, Radio, Server, RadioTower } from "lucide-react";
import { Button } from "@/components/ui/button";

interface Props {
  tasks: TaskList;
  onRefresh: () => void;
  onRescan: () => void;
}

// Identify the connection method from auto_rx's device_idx naming:
//   RTLSDR  → raw serial like "00000403" (USB dongle)
//   SpyServer → "SPY01", "SPY02", ... (networked SpyServer)
//   KA9Q-radio → "KA9Q-01", "KA9Q-02", ... (networked KA9Q radiod)
function classifySdr(id: string): { kind: "rtl" | "spy" | "ka9q"; label: string; Icon: typeof Radio } {
  if (id.startsWith("KA9Q")) return { kind: "ka9q", label: "KA9Q", Icon: RadioTower };
  if (id.startsWith("SPY")) return { kind: "spy", label: "SPY", Icon: Server };
  return { kind: "rtl", label: "RTL", Icon: Radio };
}

export function TaskStrip({ tasks, onRefresh, onRescan }: Props) {
  const ids = Object.keys(tasks);
  const decoding = ids.filter(i => tasks[i].task?.startsWith("Decoding")).length;
  const scanning = ids.filter(i => tasks[i].task === "Scanning").length;
  return (
    <section className="rounded-md border border-border bg-card overflow-hidden flex flex-col flex-1 min-h-0">
      <div className="w-full flex items-center gap-2 px-3 h-9 border-b border-border bg-gradient-to-b from-background/40 to-transparent">
        <Cpu className="w-3.5 h-3.5 text-muted-foreground/80 flex-shrink-0" strokeWidth={1.75} />
        <h3 className="text-[10px] font-semibold uppercase tracking-[0.12em] text-muted-foreground leading-none">SDR Tasks</h3>
        <span className="text-[10px] mono text-muted-foreground/70 leading-none">
          <b className="text-foreground/80 font-semibold">{ids.length}</b> SDR{ids.length === 1 ? "" : "s"}
          {" · "}<b className="text-foreground/80 font-semibold">{decoding}</b> decoding
          {" · "}<b className="text-foreground/80 font-semibold">{scanning}</b> scanning
        </span>
        <div className="flex-1" />
        <Button size="sm" variant="primary" onClick={onRescan} title="Force immediate rescan">
          <Crosshair className="w-3 h-3" /> Rescan
        </Button>
        <Button size="icon-sm" variant="ghost" onClick={onRefresh} title="Refresh" aria-label="Refresh task list"><RefreshCcw className="w-3 h-3" /></Button>
      </div>
      {/* Body fills remaining vertical room. Empty space below the SDR chips
          lives here when the parent (Live page right column on lg+) has a
          fixed height. */}
      <div id="task-strip-body" className="flex-1">
        {ids.length === 0 ? (
        <div className="px-3 py-6 text-center text-muted-foreground text-xs">
          <Cpu className="w-8 h-8 mx-auto mb-2 opacity-40" />
          No SDRs configured.
        </div>
      ) : (
        <div className="flex flex-wrap gap-2 p-2">
          {ids.map(sdrId => {
            const t = tasks[sdrId];
            const decoding = t.task && t.task.indexOf("Decoding") === 0;
            const scanning = t.task === "Scanning";
            const sig = decoding ? "signal" : scanning ? "scan" : "idle";
            const { label: typeLabel, Icon: SdrIcon } = classifySdr(sdrId);
            return (
              <div
                key={sdrId}
                className="relative flex items-center gap-2.5 px-2.5 py-1.5 rounded-md border border-border/60 bg-background/30 min-w-[220px] flex-1 sm:flex-initial sm:max-w-[280px] overflow-hidden"
              >
                <SdrIcon
                  className={`w-4 h-4 flex-shrink-0 ${decoding ? "text-signal" : scanning ? "text-scan" : "text-muted-foreground/60"}`}
                  strokeWidth={1.75}
                />
                <div className="flex flex-col leading-tight min-w-0">
                  <span className="text-[9px] uppercase tracking-widest text-muted-foreground/70 mono">{typeLabel}</span>
                  <span className="text-sm font-semibold mono truncate">{sdrId}</span>
                </div>
                <div className="flex-1" />
                <div className="text-right leading-tight min-w-0">
                  {decoding ? (
                    <>
                      <div className="mono text-sm font-semibold">{(t.freq / 1e6).toFixed(3)}<span className="text-muted-foreground/60 ml-1 text-[10px] font-normal">MHz</span></div>
                      <div className="flex items-center justify-end gap-1.5 mt-0.5">
                        <span className={`pip pip-${sig}`} aria-hidden />
                        <span className="text-[10px] mono text-muted-foreground leading-none">{t.type || "Decoding"}</span>
                      </div>
                    </>
                  ) : scanning ? (
                    <div className="flex items-center gap-1.5">
                      <span className={`pip pip-${sig}`} aria-hidden />
                      <span className="text-[11px] mono text-scan leading-none">Scanning</span>
                    </div>
                  ) : (
                    <div className="flex items-center gap-1.5">
                      <span className={`pip pip-${sig}`} aria-hidden />
                      <span className="text-[11px] mono text-muted-foreground leading-none">Idle</span>
                    </div>
                  )}
                </div>
                {/* signal underline — fits within the rounded chip */}
                <span className={`absolute left-0 right-0 bottom-0 h-0.5 ${decoding ? "bg-signal shadow-[0_0_6px_hsl(var(--signal)/0.6)]" : scanning ? "bg-scan shadow-[0_0_6px_hsl(var(--scan)/0.55)]" : "bg-transparent"}`} />
              </div>
            );
          })}
        </div>
        )}
      </div>
    </section>
  );
}
