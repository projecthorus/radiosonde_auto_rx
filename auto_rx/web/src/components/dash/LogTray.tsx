import { useMemo, useState } from "react";
import { Code, Search, Pause, Play, Trash2 } from "lucide-react";
import { Panel, PanelHead } from "@/components/ui/panel";
import { Input } from "@/components/ui/input";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type { LogEvent } from "@/lib/types";
import { fmtTime } from "@/lib/units";

const LEVELS: LogEvent["level"][] = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"];

const LEVEL_LETTER: Record<string, string> = { DEBUG: "D", INFO: "I", WARNING: "W", ERROR: "E", CRITICAL: "C" };
const LEVEL_CLASS: Record<string, string> = {
  DEBUG: "text-muted-foreground/70",
  INFO: "text-scan",
  WARNING: "text-warn",
  ERROR: "text-alert",
  CRITICAL: "text-alert",
};

interface Props {
  logs: LogEvent[];
  onClear: () => void;
  paused: boolean;
  onPause: () => void;
  collapsed?: boolean;
  onToggleCollapse?: () => void;
}

export function LogTray({ logs, onClear, paused, onPause, collapsed, onToggleCollapse }: Props) {
  const [search, setSearch] = useState("");
  const [active, setActive] = useState<Record<string, boolean>>({ DEBUG: false, INFO: true, WARNING: true, ERROR: true, CRITICAL: true });

  const filtered = useMemo(() => {
    const s = search.toLowerCase();
    return logs.filter(l => active[l.level] && (!s || (l.msg || "").toLowerCase().includes(s)));
  }, [logs, search, active]);

  const errorCount = useMemo(
    () => logs.filter(l => l.level === "ERROR" || l.level === "CRITICAL").length,
    [logs],
  );

  return (
    <Panel className={collapsed ? undefined : "flex-1 min-h-0"}>
      <PanelHead
        title="Event Log"
        icon={<Code className="w-3.5 h-3.5" strokeWidth={1.75} />}
        meta={
          <span className="flex items-center gap-1.5">
            {errorCount > 0 && (
              <span
                className="inline-flex items-center px-1.5 py-0.5 rounded text-[10px] mono font-semibold bg-alert/15 text-alert ring-1 ring-alert/30"
                title={`${errorCount} error/critical event${errorCount === 1 ? "" : "s"}`}
              >
                ⚠ {errorCount}
              </span>
            )}
            <span className="text-[10px] mono text-muted-foreground/70">{filtered.length}/{logs.length}</span>
          </span>
        }
        actions={
          <>
            <Button size="icon-sm" variant="ghost" onClick={onPause} title={paused ? "Resume" : "Pause"}>
              {paused ? <Play className="w-3 h-3" /> : <Pause className="w-3 h-3" />}
            </Button>
            <Button size="icon-sm" variant="ghost" onClick={onClear} title="Clear">
              <Trash2 className="w-3 h-3" />
            </Button>
          </>
        }
        collapsed={collapsed}
        onToggleCollapse={onToggleCollapse}
      />
      {collapsed ? null : <>
      <div className="flex items-center gap-2 pl-1 pr-px py-1.5 border-b border-border bg-background/40 flex-nowrap">
        <div className="relative flex-1 min-w-0">
          <Search className="absolute left-2 top-1/2 -translate-y-1/2 w-3 h-3 text-muted-foreground/70" />
          <Input type="search" placeholder="Filter…" value={search} onChange={e => setSearch(e.target.value)} className="h-6 w-full pl-7 text-[11px] mono" />
        </div>
        <div className="inline-flex items-center shrink-0 rounded-md bg-secondary p-0.5 gap-px" role="group" aria-label="Log levels">
          {LEVELS.map(lvl => (
            <button
              key={lvl}
              type="button"
              onClick={() => setActive(a => ({ ...a, [lvl]: !a[lvl] }))}
              className={cn(
                "h-5 w-5 mono text-[10px] font-semibold rounded-sm flex items-center justify-center transition-colors",
                active[lvl] ? `bg-accent ${LEVEL_CLASS[lvl]}` : "text-muted-foreground/50 hover:text-muted-foreground"
              )}
              title={`${active[lvl] ? "Hide" : "Show"} ${lvl}`}
              aria-pressed={active[lvl]}
            >{LEVEL_LETTER[lvl]}</button>
          ))}
        </div>
      </div>
      <div className="flex-1 min-h-0 max-h-[210px] overflow-y-auto mono text-[11px] leading-snug" role="log" aria-live="polite">
        {filtered.length === 0 ? (
          <div className="text-center text-muted-foreground/60 py-6">No log events yet.</div>
        ) : (
          filtered.map((l, i) => (
            <div
              key={i + (l.ts || "")}
              className={cn(
                "grid grid-cols-[auto_auto_1fr] gap-2 px-3 py-1 border-l-2 border-transparent hover:bg-accent/30",
                l.level === "WARNING" && "border-l-warn/60 bg-warn/[0.04]",
                (l.level === "ERROR" || l.level === "CRITICAL") && "border-l-alert/60 bg-alert/[0.05]",
                l.level === "INFO" && "border-l-scan/50",
                l.level === "DEBUG" && "border-l-muted/30 text-muted-foreground/70"
              )}
            >
              <span className="text-muted-foreground/60 text-[10px]">{l.ts ? fmtTime(l.ts) : "—"}</span>
              <span className={cn("text-[9px] font-semibold w-4 text-center", LEVEL_CLASS[l.level])}>{LEVEL_LETTER[l.level]}</span>
              <span className="break-words">{l.msg}</span>
            </div>
          ))
        )}
      </div>
      </>}
    </Panel>
  );
}
