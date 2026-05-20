import * as React from "react";
import { ChevronDown, ChevronRight } from "lucide-react";
import { cn } from "@/lib/utils";

export function Panel({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) {
  return <div className={cn("flex flex-col rounded-md border border-border bg-card overflow-hidden", className)} {...props} />;
}

interface HeadProps extends Omit<React.HTMLAttributes<HTMLDivElement>, "title"> {
  title?: React.ReactNode;
  icon?: React.ReactNode;
  meta?: React.ReactNode;
  actions?: React.ReactNode;
  /** When provided, a chevron toggle is rendered on the far right.
   *  `collapsed` is the current state; `onToggleCollapse` flips it. */
  collapsed?: boolean;
  onToggleCollapse?: () => void;
}
export function PanelHead({ title, icon, meta, actions, collapsed, onToggleCollapse, className, children, ...props }: HeadProps) {
  return (
    <div
      className={cn(
        // `flex-wrap` lets action button rows spill onto a second line on
        // narrow screens (e.g. the History map's Tracks/First/Last/Coverage
        // row), while min-h-9 keeps the single-line case the same height as
        // before. py-1 ensures vertical padding on the wrapped row.
        "flex flex-wrap items-center gap-x-2.5 gap-y-1 px-3 min-h-9 py-1 border-b border-border bg-gradient-to-b from-background/40 to-transparent",
        collapsed && "border-b-0",
        className
      )}
      {...props}
    >
      {(title || icon) && (
        // Icon + title sit 1px lower than the meta to compensate for the
        // optical-center bias: the icon is a geometrically centered SVG and
        // the title uses leading-none, so they read slightly high against
        // the meta's smaller glyph cluster. A 1px nudge brings them into
        // visual agreement without touching font metrics globally.
        <div className="flex items-center gap-2 min-w-0 mt-px">
          {icon && <span className="text-muted-foreground/80 flex-shrink-0 inline-flex items-center">{icon}</span>}
          {title && (
            <h3 className="text-[10px] font-semibold uppercase tracking-[0.12em] text-muted-foreground truncate leading-none">
              {title}
            </h3>
          )}
        </div>
      )}
      {meta && <div className="flex-shrink-0 min-w-0 leading-none">{meta}</div>}
      <div className="flex-1 min-w-0">{children}</div>
      {/* Drop flex-shrink-0 + add flex-wrap so an overflowing action row
          (e.g. History map's Tracks/First/Last/Coverage/Skew-T/Logs/KML)
          wraps onto a second line instead of clipping at the viewport edge. */}
      {actions && <div className="flex flex-wrap items-center gap-1">{actions}</div>}
      {onToggleCollapse && (
        <button
          type="button"
          onClick={onToggleCollapse}
          aria-expanded={!collapsed}
          aria-label={collapsed ? "Expand panel" : "Collapse panel"}
          className="ml-1 inline-flex items-center justify-center w-6 h-6 rounded text-muted-foreground hover:text-foreground hover:bg-accent/50 transition-colors"
        >
          {collapsed
            ? <ChevronRight className="w-3.5 h-3.5" />
            : <ChevronDown className="w-3.5 h-3.5" />}
        </button>
      )}
    </div>
  );
}

export function PanelBody({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) {
  return <div className={cn("p-3", className)} {...props} />;
}
