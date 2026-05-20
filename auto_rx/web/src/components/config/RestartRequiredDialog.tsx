import { AlertTriangle } from "lucide-react";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription } from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";

interface Props {
  keys: string[] | null;
  onClose: () => void;
}

/**
 * Shown after a successful Settings save. We don't try to bounce auto_rx
 * ourselves — different deployments use different supervisors (systemd,
 * screen, plain shell, docker), and SIGINT-ing the process from the web layer
 * either works silently or leaves the operator with a dead service. Far safer
 * to ask the operator to restart it themselves.
 */
export function RestartRequiredDialog({ keys, onClose }: Props) {
  const open = !!keys;
  const list = keys || [];

  return (
    <Dialog open={open} onOpenChange={v => { if (!v) onClose(); }}>
      <DialogContent className="max-w-md">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <AlertTriangle className="w-4 h-4 text-warn" /> Restart required
          </DialogTitle>
          <DialogDescription>
            Your changes are saved to <span className="mono">station.cfg</span>, but
            auto_rx only reads its configuration at startup. Restart the process
            yourself for the changes to take effect.
          </DialogDescription>
        </DialogHeader>

        {list.length > 0 && (
          <div className="rounded-md border border-border bg-background/30 p-3 max-h-40 overflow-auto">
            <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-1.5">
              Changed settings ({list.length})
            </div>
            <ul className="text-xs mono leading-5 columns-2 gap-3">
              {list.map(k => <li key={k} className="break-all">{k}</li>)}
            </ul>
          </div>
        )}

        <div className="rounded-md border border-border bg-background/40 p-3 text-[11px] mono leading-relaxed text-muted-foreground space-y-1">
          <div><span className="text-foreground/80">systemd:</span> <span className="text-foreground">sudo systemctl restart auto_rx</span></div>
          <div><span className="text-foreground/80">docker:</span>  <span className="text-foreground">docker restart auto_rx</span></div>
          <div><span className="text-foreground/80">manual:</span>  <span className="text-foreground">Ctrl-C the process, then re-run <span className="text-foreground/80">./auto_rx.py</span></span></div>
        </div>

        <div className="flex justify-end gap-2 pt-1">
          <Button variant="primary" size="sm" onClick={onClose}>Got it</Button>
        </div>
      </DialogContent>
    </Dialog>
  );
}
