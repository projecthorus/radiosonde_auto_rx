import { NavLink, Outlet, useLocation } from "react-router-dom";
import { useEffect, useState } from "react";
import { Activity, History, BarChart3, Sliders, Sun, Moon, Menu, X, AlertTriangle } from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { usePrefs, setPrefs } from "@/lib/units";
import { Toaster } from "sonner";
import { apiGet } from "@/lib/api";

const NAV = [
  { to: "/", label: "Live", icon: Activity, exact: true },
  { to: "/historical", label: "History", icon: History },
  { to: "/stats", label: "Stats", icon: BarChart3 },
  { to: "/config", label: "Settings", icon: Sliders },
];

function Clocks() {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 1000);
    return () => clearInterval(t);
  }, []);
  const prefs = usePrefs();
  // The user's chosen TZ gets the large/prominent clock; the other TZ rides
  // along underneath as a smaller secondary readout. Both honor hour12.
  const fmt = (utc: boolean) => {
    const h = utc ? now.getUTCHours() : now.getHours();
    const m = utc ? now.getUTCMinutes() : now.getMinutes();
    const s = utc ? now.getUTCSeconds() : now.getSeconds();
    const mm = String(m).padStart(2, "0");
    const ss = String(s).padStart(2, "0");
    if (!prefs.hour12) return `${String(h).padStart(2, "0")}:${mm}:${ss}`;
    const ampm = h >= 12 ? "PM" : "AM";
    const h12 = ((h + 11) % 12) + 1;
    return `${String(h12).padStart(2, "0")}:${mm}:${ss} ${ampm}`;
  };
  const primaryIsUtc = prefs.utc;
  return (
    <div className="hidden md:flex flex-col items-end leading-tight mono pr-3 mr-1 border-r border-border">
      <span className="text-foreground text-[12px] font-semibold">
        {fmt(primaryIsUtc)} <span className="text-muted-foreground">{primaryIsUtc ? "UTC" : "Local"}</span>
      </span>
      <span className="text-[10px] text-muted-foreground/80">
        {fmt(!primaryIsUtc)} <span className="text-muted-foreground/60">{primaryIsUtc ? "Local" : "UTC"}</span>
      </span>
    </div>
  );
}

/**
 * Shows a banner when /get_version reports a newer release than the running
 * code. Dismissable per-version: hiding the banner for v1.8.3 won't suppress
 * the banner when v1.8.4 lands.
 */
function UpdateBanner() {
  const [info, setInfo] = useState<{ current: string; latest: string } | null>(null);
  const [dismissed, setDismissed] = useState<string | null>(() => {
    try { return localStorage.getItem("obs.updateDismissed"); } catch { return null; }
  });

  useEffect(() => {
    apiGet<any>("/get_version")
      .then(r => {
        if (!r || typeof r !== "object") return;
        const current = String(r.current || "");
        const latest = String(r.latest || "");
        if (!current || !latest) return;
        // `latest` is either "Latest" (up to date), "Unknown" (check failed),
        // or a real version string newer than `current`.
        if (latest === "Latest" || latest === "Unknown" || latest === current) return;
        setInfo({ current, latest });
      })
      .catch(() => {});
  }, []);

  if (!info) return null;
  if (dismissed === info.latest) return null;

  return (
    <div className="border-b border-warn/30 bg-warn/[0.08] text-warn">
      <div className="max-w-[1920px] mx-auto px-3 md:px-5 py-1.5 flex items-center gap-3 text-[11px] mono">
        <AlertTriangle className="w-3.5 h-3.5 shrink-0" />
        <span className="flex-1">
          Update available — running <b>v{info.current}</b>, latest is <b>v{info.latest}</b>.{" "}
          <a
            href="https://github.com/projecthorus/radiosonde_auto_rx/releases"
            target="_blank" rel="noreferrer"
            className="underline underline-offset-2 hover:text-foreground"
          >Release notes ↗</a>
        </span>
        <button
          type="button"
          onClick={() => {
            setDismissed(info.latest);
            try { localStorage.setItem("obs.updateDismissed", info.latest); } catch {}
          }}
          className="text-warn/70 hover:text-warn"
          aria-label="Dismiss update notification"
        >Dismiss</button>
      </div>
    </div>
  );
}

function VersionChip() {
  const prefs = usePrefs();
  // Seed from localStorage so the chip paints with the right value on the
  // first render after navigation — avoids the "appears late" flicker.
  const [ver, setVer] = useState<string | null>(() => {
    try { return localStorage.getItem("obs.version"); } catch { return null; }
  });
  useEffect(() => {
    if (!prefs.showVersion) return;
    apiGet<any>("/get_version")
      .then(r => {
        // Accept {current,latest} (production), bare strings, or nothing.
        let v: string | null = null;
        if (r && typeof r === "object" && typeof r.current === "string") v = r.current;
        else if (typeof r === "string") v = r;
        if (!v || /[<>]/.test(v)) v = null; // guard against HTML fall-through
        const next = v ? v.trim() : null;
        setVer(next);
        try {
          if (next) localStorage.setItem("obs.version", next);
          else localStorage.removeItem("obs.version");
        } catch {}
      })
      .catch(() => {});
  }, [prefs.showVersion]);
  if (!prefs.showVersion || !ver) return null;
  return <span className="ml-1 text-muted-foreground/60">v{ver}</span>;
}

function ThemeToggle() {
  const prefs = usePrefs();
  // Quick toggle: flip between light/dark using the *effective* theme as the
  // starting point. If the user is on "system", clicking commits to the
  // opposite of whatever the OS currently shows them. They can still pick
  // "system" explicitly from Settings → View.
  const effective: "light" | "dark" = prefs.theme === "system"
    ? (typeof window !== "undefined" && window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark")
    : prefs.theme;
  return (
    <Button
      variant="ghost"
      size="icon"
      aria-label="Toggle theme"
      title={prefs.theme === "system" ? `System (${effective})` : effective}
      onClick={() => setPrefs({
        theme: effective === "dark" ? "light" : "dark",
        // The intent of this quick-toggle is "make the UI match this theme",
        // which includes the map tile. Reset to Auto so the new theme picks
        // its matching tile (Voyager / Dark Matter). If the user wants a
        // specific tile they can pin one from the map header dropdown.
        tile: "Auto",
      })}
    >
      {effective === "dark" ? <Sun className="h-3.5 w-3.5" /> : <Moon className="h-3.5 w-3.5" />}
    </Button>
  );
}

export function AppShell({ stationCallsign }: { stationCallsign?: string }) {
  const [drawerOpen, setDrawerOpen] = useState(false);
  // Fetch the station callsign from auto_rx config. Falls back to "STATION"
  // when unset or still at the default "CHANGEME" placeholder. Also reads
  // web_config_enabled — the Settings page only renders when the operator
  // has opted in via station.cfg.
  // Seed callsign from localStorage so the station chip renders with the
  // real value immediately on navigation, instead of flickering "STATION"
  // while /get_config is in flight.
  const [callsign, setCallsign] = useState<string>(() => {
    if (stationCallsign) return stationCallsign;
    try { return localStorage.getItem("obs.callsign") || "STATION"; } catch { return "STATION"; }
  });
  useEffect(() => {
    if (stationCallsign) return;
    apiGet<any>("/get_config")
      .then(cfg => {
        const c = (cfg?.habitat_uploader_callsign || "").trim();
        if (c && c !== "CHANGEME") {
          setCallsign(c);
          try { localStorage.setItem("obs.callsign", c); } catch {}
        }
      })
      .catch(() => {});
  }, [stationCallsign]);
  // Settings link is always visible — when web_config_enabled is off, the
  // /config page still renders the "View" tab (browser-side prefs like theme,
  // units, marker style). Editing station.cfg is what's gated.
  const visibleNav = NAV;

  // Clicking the nav item for the page you're already on does nothing by
  // default (react-router sees no path change). Treat a same-page click as
  // "refresh" and force a full reload — matches what users expect when they
  // re-click a tab.
  const location = useLocation();
  const reloadIfSame = (to: string, exact?: boolean) => (e: React.MouseEvent) => {
    const same = exact ? location.pathname === to : location.pathname.startsWith(to);
    if (same) { e.preventDefault(); window.location.reload(); }
  };

  return (
    <div className="min-h-full flex flex-col">
      {/* TOP BAR */}
      <header className="sticky top-0 z-[900] border-b border-border bg-background/75 backdrop-blur supports-[backdrop-filter]:bg-background/60">
        <div className="max-w-[1920px] mx-auto px-3 md:px-5 h-12 flex items-center gap-4">
          {/* Brand */}
          <NavLink to="/" className="flex items-center gap-2.5 leading-none" aria-label="radiosonde_auto_rx home">
            <img
              src="/static/img/autorx_logo.png"
              alt="radiosonde auto_rx"
              className="h-9 w-auto select-none"
              draggable={false}
            />
          </NavLink>
          <span className="inline-flex items-center gap-1.5 text-[11px] mono text-muted-foreground/80 pl-2 ml-1 border-l border-border self-stretch py-0 min-w-0">
            <span className="pip pip-signal flex-shrink-0" aria-hidden />
            <span className="truncate">{callsign}</span>
            <VersionChip />
          </span>

          {/* Primary nav */}
          <nav className="hidden md:flex items-center gap-px ml-3" aria-label="Primary">
            {visibleNav.map(({ to, label, icon: Icon, exact }) => (
              <NavLink
                key={to}
                to={to}
                end={exact}
                onClick={reloadIfSame(to, exact)}
                className={({ isActive }) =>
                  cn(
                    "relative inline-flex items-center gap-1.5 px-2.5 py-1.5 rounded-md text-[12px] font-medium text-muted-foreground hover:text-foreground hover:bg-accent transition-colors",
                    isActive && "text-foreground"
                  )
                }
              >
                {({ isActive }) => (
                  <>
                    <Icon className="w-3.5 h-3.5" strokeWidth={1.75} />
                    {label}
                    {isActive && <span className="absolute left-2 right-2 -bottom-[7px] h-0.5 bg-signal rounded-full shadow-[0_0_8px_hsl(var(--signal)/0.6)]" />}
                  </>
                )}
              </NavLink>
            ))}
          </nav>

          <div className="flex-1" />

          {/* Right utilities — quick-access only. Less-frequently-changed
              prefs (units, theme = system, marker style, time format, etc.)
              live under Settings → View. */}
          <Clocks />
          <ThemeToggle />
          <Button variant="ghost" size="icon" className="md:hidden" onClick={() => setDrawerOpen(o => !o)} aria-label="Menu" aria-expanded={drawerOpen}>
            {drawerOpen ? <X className="h-4 w-4" /> : <Menu className="h-4 w-4" />}
          </Button>
        </div>
        {/* Mobile drawer — must be fully opaque so the map's leaflet controls
            don't show through. The sticky header already has z-[900] so the
            drawer (a child of the header) inherits the same stacking layer. */}
        {drawerOpen && (
          <nav className="md:hidden border-t border-border bg-background relative z-[900]">
            <div className="flex flex-col p-2 gap-px">
              {visibleNav.map(({ to, label, icon: Icon, exact }) => (
                <NavLink
                  key={to}
                  to={to}
                  end={exact}
                  onClick={(e) => { setDrawerOpen(false); reloadIfSame(to, exact)(e); }}
                  className={({ isActive }) =>
                    cn(
                      "flex items-center gap-2 px-3 py-2 rounded-md text-sm text-muted-foreground hover:bg-accent",
                      isActive && "bg-accent text-foreground"
                    )
                  }
                >
                  <Icon className="w-4 h-4" />
                  {label}
                </NavLink>
              ))}
            </div>
          </nav>
        )}
      </header>

      <UpdateBanner />

      <main className="flex-1 max-w-[1920px] w-full mx-auto px-3 md:px-5 py-3 md:py-4">
        <Outlet />
      </main>

      <Toaster
        theme="dark"
        position="bottom-right"
        toastOptions={{
          className: "!bg-card !border-border !text-foreground",
        }}
      />
    </div>
  );
}
