import { useCallback, useRef, useState } from "react";
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { auth, useAuth } from "@/lib/auth";
import { Lock } from "lucide-react";
import { toast } from "sonner";

/**
 * Reusable inline auth prompt. Usage:
 *
 *   const { require, dialog } = usePasswordPrompt();
 *   const handler = async () => {
 *     const pw = await require();              // resolves with password or null
 *     if (!pw) return;                          // user cancelled
 *     await apiPostForm("/protected", { password: pw });
 *   };
 *   return <>...{dialog}</>;
 *
 * If the user is already authenticated for the session, `require()` resolves
 * synchronously with the cached password — no dialog shown.
 */
export function usePasswordPrompt() {
  const [open, setOpen] = useState(false);
  const [pw, setPw] = useState("");
  const a = useAuth();
  const resolverRef = useRef<((pw: string | null) => void) | null>(null);

  const settle = (val: string | null) => {
    const r = resolverRef.current;
    resolverRef.current = null;
    if (r) r(val);
  };

  const close = (val: string | null) => {
    setOpen(false);
    setPw("");
    settle(val);
  };

  const require = useCallback((): Promise<string | null> => {
    const existing = auth.password();
    if (existing) return Promise.resolve(existing);
    return new Promise<string | null>((resolve) => {
      resolverRef.current = resolve;
      setOpen(true);
    });
  }, []);

  const submit = async () => {
    if (!pw) return;
    const ok = await auth.verify(pw);
    if (ok) close(pw);
    else toast.error("Incorrect password");
  };

  const dialog = (
    <Dialog
      open={open}
      onOpenChange={(o) => {
        if (!o) close(null);
        else setOpen(true);
      }}
    >
      <DialogContent className="max-w-sm">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <Lock className="w-3.5 h-3.5 text-warn" /> Authentication required
          </DialogTitle>
          <DialogDescription>
            Enter the web-control password from <code className="mono text-foreground/80">station.cfg</code>.
          </DialogDescription>
        </DialogHeader>
        <form onSubmit={(e) => { e.preventDefault(); submit(); }} className="flex gap-2 items-end pt-2">
          <div className="flex-1">
            <Label htmlFor="pp-pw">Password</Label>
            <Input id="pp-pw" type="password" value={pw} onChange={(e) => setPw(e.target.value)} autoFocus />
          </div>
          <Button type="submit" variant="primary" disabled={!pw || a.verifying}>Unlock</Button>
        </form>
      </DialogContent>
    </Dialog>
  );

  return { require, dialog };
}
