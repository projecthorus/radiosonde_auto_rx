import * as TooltipPrimitive from "@radix-ui/react-tooltip";
import { Info as InfoIcon } from "lucide-react";
import { cn } from "@/lib/utils";

interface InfoProps {
  tip: string;
  className?: string;
}

export function Info({ tip, className }: InfoProps) {
  return (
    <TooltipPrimitive.Provider delayDuration={150}>
      <TooltipPrimitive.Root>
        <TooltipPrimitive.Trigger asChild>
          <button
            type="button"
            aria-label="More info"
            className={cn(
              "inline-flex items-center justify-center w-3.5 h-3.5 rounded-full text-muted-foreground/70 hover:text-signal transition-colors cursor-help",
              className
            )}
          >
            <InfoIcon className="w-3 h-3" strokeWidth={2} />
          </button>
        </TooltipPrimitive.Trigger>
        <TooltipPrimitive.Portal>
          <TooltipPrimitive.Content
            sideOffset={4}
            className="z-[1100] max-w-[260px] rounded-md border border-border bg-popover px-2.5 py-1.5 text-[11px] leading-snug text-popover-foreground shadow-md data-[state=delayed-open]:animate-fade-in"
          >
            {tip}
            <TooltipPrimitive.Arrow className="fill-border" />
          </TooltipPrimitive.Content>
        </TooltipPrimitive.Portal>
      </TooltipPrimitive.Root>
    </TooltipPrimitive.Provider>
  );
}
