import { useState } from "react";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription, DialogTrigger } from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Joystick, Lock, Power, Compass, Home as HomeIcon, Trash2, Ban } from "lucide-react";
import { apiPostForm } from "@/lib/api";
import { auth, useAuth } from "@/lib/auth";
import { toast } from "sonner";
import type { TaskList } from "@/lib/types";

const SONDE_TYPES = ["RS41", "RS92", "DFM", "M10", "M20", "iMet", "iMet54", "MEISEI", "MRZ", "LMS6", "LMS6-1680"];

interface Props {
  rotatorEnabled?: boolean;
  scannerActive?: boolean;
  tasks?: TaskList;
  onAfter?: () => void;
}

export function ControlsDialog({ rotatorEnabled, scannerActive, tasks, onAfter }: Props) {
  const [open, setOpen] = useState(false);
  const [pw, setPw] = useState("");
  const a = useAuth();
  const [busy, setBusy] = useState(false);

  // form state
  const [startFreq, setStartFreq] = useState("");
  const [startType, setStartType] = useState("RS41");
  const [az, setAz] = useState("");
  const [el, setEl] = useState("");

  const decoders = Object.entries(tasks || {})
    .filter(([, t]) => t.task && t.task.indexOf("Decoding") === 0)
    .map(([sdrId, t]) => ({ sdrId, freq: t.freq, type: t.type }));

  const verify = async () => {
    const ok = await auth.verify(pw);
    if (ok) toast.success("Authenticated");
    else toast.error("Incorrect password");
  };
  const pwCurrent = () => a.password || pw;

  const run = async (label: string, fn: () => Promise<any>) => {
    setBusy(true);
    try { await fn(); toast.success(label); onAfter?.(); }
    catch (e: any) { toast.error(e.message || "Failed"); }
    finally { setBusy(false); }
  };

  const fmtFreq = (hz: number) => (hz / 1e6).toFixed(3);

  return (
    <Dialog open={open} onOpenChange={setOpen}>
      <DialogTrigger asChild>
        <Button variant="outline" size="sm">
          <Joystick className="w-3 h-3" /> Controls
        </Button>
      </DialogTrigger>
      <DialogContent className="max-w-md">
        <DialogHeader>
          {/* Reserve right padding for the dialog's built-in X close button
              (positioned absolute right-3). */}
          <DialogTitle className="flex items-center gap-2 pr-7">
            <Lock className="w-3.5 h-3.5 text-warn" /> Advanced controls
          </DialogTitle>
          <DialogDescription>Start / stop decoders, scanner, rotator. Requires the web-control password from station.cfg.</DialogDescription>
        </DialogHeader>

        {!a.verified ? (
          <form onSubmit={e => { e.preventDefault(); verify(); }} className="flex gap-2 items-end pt-2">
            <div className="flex-1">
              <Label htmlFor="cw-pw">Password</Label>
              <Input id="cw-pw" type="password" value={pw} onChange={e => setPw(e.target.value)} autoFocus />
            </div>
            <Button type="submit" variant="primary" disabled={!pw || a.verifying}>Unlock</Button>
          </form>
        ) : (
          <div className="space-y-4 pt-2">
            {/* Start decoder */}
            <section className="rounded-md border border-border p-3 bg-background/30">
              <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-2">Start decoder</div>
              <div className="grid grid-cols-[1fr_8rem_auto] gap-2 items-end">
                <div>
                  <Label htmlFor="cw-start-freq">Frequency (MHz)</Label>
                  <Input id="cw-start-freq" type="number" step="0.001" inputMode="decimal" placeholder="401.500"
                    value={startFreq} onChange={e => setStartFreq(e.target.value)} className="mono" />
                </div>
                <div>
                  <Label htmlFor="cw-start-type">Type</Label>
                  <Select value={startType} onValueChange={setStartType}>
                    <SelectTrigger><SelectValue /></SelectTrigger>
                    <SelectContent>{SONDE_TYPES.map(t => <SelectItem key={t} value={t}>{t}</SelectItem>)}</SelectContent>
                  </Select>
                </div>
                <Button variant="primary" disabled={busy || !startFreq} onClick={() => run(
                  `Started ${startFreq} MHz ${startType}`,
                  () => apiPostForm("/start_decoder", { freq: Math.round(parseFloat(startFreq) * 1e6), type: startType, password: pwCurrent() })
                )}><Power className="w-3 h-3" /> Start</Button>
              </div>
            </section>

            {/* Running decoders */}
            <section className="rounded-md border border-border p-3 bg-background/30">
              <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-2">Running decoders</div>
              {decoders.length === 0 ? (
                <div className="text-xs text-muted-foreground italic py-1">No active decoders.</div>
              ) : (
                <ul className="space-y-1.5">
                  {decoders.map(d => (
                    <li key={d.sdrId} className="flex items-center gap-2 text-sm">
                      <span className="mono text-[11px] px-1.5 py-0.5 rounded bg-muted/30 text-muted-foreground">SDR {d.sdrId}</span>
                      <span className="mono tabular-nums">{fmtFreq(d.freq)} MHz</span>
                      {d.type && <span className="mono text-[11px] text-muted-foreground">{d.type}</span>}
                      <span className="flex-1" />
                      <Button
                        size="sm" variant="ghost" disabled={busy}
                        title={`Stop ${fmtFreq(d.freq)} MHz`}
                        aria-label={`Stop decoder ${fmtFreq(d.freq)} MHz`}
                        onClick={() => run(
                          `Stopped ${fmtFreq(d.freq)} MHz`,
                          () => apiPostForm("/stop_decoder", { freq: d.freq, password: pwCurrent() })
                        )}
                      ><Trash2 className="w-3.5 h-3.5" /></Button>
                      <Button
                        size="sm" variant="ghost" disabled={busy}
                        title={`Stop and temporarily block ${fmtFreq(d.freq)} MHz`}
                        aria-label={`Stop and block ${fmtFreq(d.freq)} MHz`}
                        onClick={() => run(
                          `Blocked ${fmtFreq(d.freq)} MHz`,
                          () => apiPostForm("/stop_decoder", { freq: d.freq, lockout: 1, password: pwCurrent() })
                        )}
                      ><Ban className="w-3.5 h-3.5 text-warn" /></Button>
                    </li>
                  ))}
                </ul>
              )}
            </section>

            {/* Scanner */}
            <section className="rounded-md border border-border p-3 bg-background/30 flex items-center gap-2">
              <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold flex-1">Scanner</div>
              <div className="text-[11px] mono text-muted-foreground">
                {scannerActive ? <span className="text-signal">running</span> : <span>stopped</span>}
              </div>
              {scannerActive ? (
                <Button variant="default" disabled={busy} onClick={() => run(
                  "Scanner disabled",
                  () => apiPostForm("/disable_scanner", { password: pwCurrent() })
                )}>Disable</Button>
              ) : (
                <Button variant="primary" disabled={busy} onClick={() => run(
                  "Scanner enabled",
                  () => apiPostForm("/enable_scanner", { password: pwCurrent() })
                )}>Enable</Button>
              )}
            </section>

            {/* Rotator */}
            {rotatorEnabled && (
              <section className="rounded-md border border-border p-3 bg-background/30">
                <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-2">Rotator</div>
                <div className="grid grid-cols-[1fr_1fr_auto_auto] gap-2 items-end">
                  <div>
                    <Label htmlFor="cw-az">Azimuth (°)</Label>
                    <Input id="cw-az" type="number" min={0} max={360} step={1} value={az} onChange={e => setAz(e.target.value)} className="mono" />
                  </div>
                  <div>
                    <Label htmlFor="cw-el">Elevation (°)</Label>
                    <Input id="cw-el" type="number" min={0} max={90} step={1} value={el} onChange={e => setEl(e.target.value)} className="mono" />
                  </div>
                  <Button variant="primary" disabled={busy || !az || !el} onClick={() => run(
                    `Rotator → AZ ${az}° EL ${el}°`,
                    // Production /move_rotator reads `az` / `el` form keys.
                    () => apiPostForm("/move_rotator", { az, el, password: pwCurrent() })
                  )}><Compass className="w-3 h-3" /> Go</Button>
                  <Button variant="default" disabled={busy} onClick={() => run(
                    "Rotator → home",
                    () => apiPostForm("/home_rotator", { password: pwCurrent() })
                  )}><HomeIcon className="w-3 h-3" /> Home</Button>
                </div>
              </section>
            )}
          </div>
        )}
      </DialogContent>
    </Dialog>
  );
}
