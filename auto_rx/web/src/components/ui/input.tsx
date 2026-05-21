import * as React from "react";
import { cn } from "@/lib/utils";

export type InputProps = React.InputHTMLAttributes<HTMLInputElement>;

/**
 * Drop-in <input>. For number inputs, also renders chunky +/- buttons that
 * call the standard stepUp/stepDown — the native spinner arrows are tiny on
 * desktop (~14px each, stacked) and basically invisible on touch screens, so
 * we hide them and provide larger click targets instead. Pass-through props
 * (step, min, max, etc.) work normally.
 */
const Input = React.forwardRef<HTMLInputElement, InputProps>(
  ({ className, type, disabled, ...props }, ref) => {
    const isNumber = type === "number";
    const internalRef = React.useRef<HTMLInputElement>(null);
    React.useImperativeHandle(ref, () => internalRef.current as HTMLInputElement);

    const fireStep = (dir: 1 | -1) => {
      const el = internalRef.current;
      if (!el || disabled) return;
      // stepUp/stepDown both honor the input's step/min/max and dispatch a
      // change event so React's onChange handler runs. We also have to fire
      // an input event manually since React listens for that on number
      // inputs to register controlled-value updates.
      if (dir === 1) el.stepUp();
      else el.stepDown();
      el.dispatchEvent(new Event("input", { bubbles: true }));
    };

    const inputEl = (
      <input
        type={type}
        ref={internalRef}
        disabled={disabled}
        className={cn(
          "h-7 w-full rounded-md border border-input bg-input/60 px-2.5 py-1 text-xs leading-tight transition-colors",
          "placeholder:text-muted-foreground/70",
          "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring/60 focus-visible:bg-input",
          "disabled:opacity-50 disabled:cursor-not-allowed",
          "[&[type=number]]:font-mono [&[type=search]]:pl-7",
          // Suppress the tiny native spinner arrows in WebKit and Firefox —
          // we render our own next to the input.
          isNumber && "[appearance:textfield] [&::-webkit-outer-spin-button]:[-webkit-appearance:none] [&::-webkit-inner-spin-button]:[-webkit-appearance:none] [&::-webkit-inner-spin-button]:m-0 pr-7",
          className
        )}
        {...props}
      />
    );

    if (!isNumber) return inputEl;

    return (
      <div className="relative w-full">
        {inputEl}
        <div className="absolute right-0.5 top-0.5 bottom-0.5 flex flex-col w-5">
          <button
            type="button"
            tabIndex={-1}
            disabled={disabled}
            onMouseDown={(e) => { e.preventDefault(); fireStep(1); }}
            className="flex-1 flex items-center justify-center rounded-t-sm text-muted-foreground hover:text-foreground hover:bg-accent/60 disabled:opacity-30 transition-colors"
            aria-label="Increase"
          >
            <svg width="10" height="6" viewBox="0 0 10 6" fill="none" aria-hidden><path d="M1 5L5 1L9 5" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" /></svg>
          </button>
          <button
            type="button"
            tabIndex={-1}
            disabled={disabled}
            onMouseDown={(e) => { e.preventDefault(); fireStep(-1); }}
            className="flex-1 flex items-center justify-center rounded-b-sm text-muted-foreground hover:text-foreground hover:bg-accent/60 disabled:opacity-30 transition-colors"
            aria-label="Decrease"
          >
            <svg width="10" height="6" viewBox="0 0 10 6" fill="none" aria-hidden><path d="M1 1L5 5L9 1" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" /></svg>
          </button>
        </div>
      </div>
    );
  }
);
Input.displayName = "Input";

export { Input };
