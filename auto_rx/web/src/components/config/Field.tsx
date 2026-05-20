import * as React from "react";
import { Label } from "@/components/ui/label";
import { Info } from "@/components/ui/info";
import { cn } from "@/lib/utils";

interface FieldProps {
  label: string;
  tip?: string;
  htmlFor?: string;
  error?: string;
  className?: string;
  span?: 1 | 2 | "full";
  children: React.ReactNode;
}

export function Field({ label, tip, htmlFor, error, className, span = 1, children }: FieldProps) {
  return (
    <div className={cn(
      "flex flex-col gap-1 min-w-0",
      span === 2 && "md:col-span-2",
      span === "full" && "col-span-full",
      className
    )}>
      <Label htmlFor={htmlFor}>
        <span>{label}</span>
        {tip && <Info tip={tip} />}
      </Label>
      {children}
      {error && <span className="text-[10px] mono text-alert">{error}</span>}
    </div>
  );
}

export function Section({ title, description, children, className }: { title: string; description?: string; children: React.ReactNode; className?: string }) {
  return (
    <section className={cn("space-y-3", className)}>
      <header className="border-b border-border pb-2">
        <h2 className="text-sm font-semibold">{title}</h2>
        {description && <p className="text-xs text-muted-foreground mt-0.5">{description}</p>}
      </header>
      <div className="space-y-4">{children}</div>
    </section>
  );
}

export function SubPanel({ title, children, badge, className }: { title: string; children: React.ReactNode; badge?: React.ReactNode; className?: string }) {
  return (
    <div className={cn("rounded-md border border-border bg-card/40", className)}>
      <div className="flex items-center gap-2 px-3 h-8 border-b border-border bg-gradient-to-b from-background/30 to-transparent">
        <h3 className="text-[10px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">{title}</h3>
        {badge}
      </div>
      <div className="p-3">{children}</div>
    </div>
  );
}

export function FieldGrid({ cols = 2, children, className }: { cols?: 1 | 2 | 3 | 4; children: React.ReactNode; className?: string }) {
  return (
    <div className={cn(
      "grid gap-x-3 gap-y-3",
      cols === 1 && "grid-cols-1",
      cols === 2 && "grid-cols-1 md:grid-cols-2",
      cols === 3 && "grid-cols-1 md:grid-cols-2 xl:grid-cols-3",
      cols === 4 && "grid-cols-1 md:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4",
      className
    )}>
      {children}
    </div>
  );
}
