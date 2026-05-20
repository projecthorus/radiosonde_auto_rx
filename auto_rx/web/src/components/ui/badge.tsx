import * as React from "react";
import { cva, type VariantProps } from "class-variance-authority";
import { cn } from "@/lib/utils";

const badgeVariants = cva(
  "inline-flex items-center gap-1 rounded px-1.5 py-px text-[10px] font-medium uppercase tracking-wider mono leading-none border whitespace-nowrap",
  {
    variants: {
      variant: {
        default: "bg-secondary border-border text-muted-foreground",
        signal: "bg-signal/10 border-signal/30 text-signal",
        scan: "bg-scan/10 border-scan/30 text-scan",
        warn: "bg-warn/10 border-warn/30 text-warn",
        alert: "bg-alert/10 border-alert/30 text-alert",
        ghost: "bg-transparent border-transparent text-muted-foreground",
      },
    },
    defaultVariants: { variant: "default" },
  }
);

export interface BadgeProps extends React.HTMLAttributes<HTMLSpanElement>, VariantProps<typeof badgeVariants> {}

export function Badge({ className, variant, ...props }: BadgeProps) {
  return <span className={cn(badgeVariants({ variant }), className)} {...props} />;
}
