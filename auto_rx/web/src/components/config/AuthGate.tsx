import { useState } from "react";
import { Lock, ShieldCheck, ShieldAlert } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { auth, useAuth } from "@/lib/auth";
import { toast } from "sonner";

interface Props {
  webControlEnabled: boolean;
  children: React.ReactNode;
}

/**
 * Gates a page (e.g. Settings) behind the web_control password.
 *
 * - If `web_control` is disabled in the cfg → can't authenticate at all.
 * - If `web_control` is enabled but no password yet → password prompt.
 * - If authenticated → renders children.
 */
export function AuthGate({ webControlEnabled, children }: Props) {
  const a = useAuth();
  const [pw, setPw] = useState("");

  if (!webControlEnabled) {
    return (
      <div className="max-w-lg mx-auto mt-12 rounded-md border border-warn/30 bg-warn/[0.05] p-5">
        <div className="flex items-center gap-2 mb-2 text-warn">
          <ShieldAlert className="w-4 h-4" />
          <h2 className="text-sm font-semibold">Settings editing is disabled</h2>
        </div>
        <p className="text-xs text-muted-foreground leading-relaxed mb-3">
          This installation has <span className="mono text-foreground/80">web_control</span> turned off, so the settings on this page can be viewed but not saved.
        </p>
        <p className="text-[11px] text-muted-foreground/80 mb-2">To re-enable editing, on the auto_rx host:</p>
        <ol className="text-xs text-muted-foreground space-y-1 list-decimal pl-5">
          <li>Edit <span className="mono text-foreground/80">station.cfg</span>.</li>
          <li>Under the <span className="mono text-foreground/80">[web]</span> section, set <span className="mono text-foreground/80">web_control = True</span> and pick a <span className="mono text-foreground/80">web_password</span>.</li>
          <li>Restart auto_rx (<span className="mono text-foreground/80">sudo systemctl restart auto_rx.service</span>).</li>
        </ol>
      </div>
    );
  }

  if (a.verified) {
    return (
      <div className="space-y-3">
        <div className="flex items-center gap-2 text-[11px] text-muted-foreground mono">
          <ShieldCheck className="w-3.5 h-3.5 text-signal" />
          <span>Authenticated</span>
          <span className="text-border">·</span>
          <button type="button" className="hover:text-foreground underline-offset-2 hover:underline" onClick={() => { auth.logout(); toast.info("Logged out"); }}>
            Log out
          </button>
        </div>
        {children}
      </div>
    );
  }

  return (
    <div className="max-w-md mx-auto mt-12 rounded-md border border-border bg-card p-5">
      <div className="flex items-center gap-2 mb-3">
        <Lock className="w-4 h-4 text-warn" />
        <h2 className="text-sm font-semibold">Authentication required</h2>
      </div>
      <p className="text-xs text-muted-foreground mb-4 leading-relaxed">
        The settings editor uses the same password as the Controls dialog (the{" "}
        <span className="mono">web_password</span> in <span className="mono">station.cfg</span>).
        Once authenticated, you stay unlocked for this browser session.
      </p>
      <form
        onSubmit={async e => {
          e.preventDefault();
          const ok = await auth.verify(pw);
          if (ok) toast.success("Authenticated"); else toast.error("Incorrect password");
        }}
        className="flex gap-2 items-end"
      >
        <div className="flex-1">
          <Label htmlFor="ag-pw">Password</Label>
          <Input id="ag-pw" type="password" autoFocus value={pw} onChange={e => setPw(e.target.value)} />
        </div>
        <Button type="submit" variant="primary" disabled={!pw || a.verifying}>Unlock</Button>
      </form>
    </div>
  );
}
