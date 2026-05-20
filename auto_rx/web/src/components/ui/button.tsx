import * as React from "react";
import { Slot } from "@radix-ui/react-slot";
import { cva, type VariantProps } from "class-variance-authority";
import { cn } from "@/lib/utils";

const buttonVariants = cva(
  "inline-flex items-center justify-center gap-1.5 whitespace-nowrap rounded-md text-xs font-medium transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-1 focus-visible:ring-offset-background disabled:pointer-events-none disabled:opacity-50 leading-none",
  {
    variants: {
      variant: {
        default: "bg-secondary border border-border text-foreground hover:bg-accent",
        primary: "bg-primary text-primary-foreground hover:bg-primary/90 font-semibold",
        outline: "border border-border bg-transparent hover:bg-accent",
        ghost: "hover:bg-accent text-muted-foreground hover:text-foreground",
        destructive: "bg-secondary text-alert border border-alert/30 hover:bg-alert/10",
        link: "text-primary underline-offset-4 hover:underline",
      },
      size: {
        default: "h-7 px-3",
        sm: "h-6 px-2 text-[11px]",
        lg: "h-8 px-4 text-sm",
        icon: "h-7 w-7",
        "icon-sm": "h-6 w-6",
      },
    },
    defaultVariants: { variant: "default", size: "default" },
  }
);

export interface ButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement>, VariantProps<typeof buttonVariants> {
  asChild?: boolean;
}

const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, asChild = false, ...props }, ref) => {
    const Comp = asChild ? Slot : "button";
    return <Comp className={cn(buttonVariants({ variant, size, className }))} ref={ref} {...props} />;
  }
);
Button.displayName = "Button";

export { Button, buttonVariants };
