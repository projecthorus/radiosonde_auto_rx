import { useEffect, useMemo, useState } from "react";
import { Home, Cpu, Waves, Upload, Bell, Antenna as AntennaIcon, Filter as FilterIcon, Sliders, Check, RotateCcw, X, Plus, Radio, Eye, EyeOff, Sun, Moon, Monitor, MapPin, Clock, Ruler, RotateCw } from "lucide-react";
import { usePrefs, setPrefs, type ThemePref, type MarkerStyle } from "@/lib/units";
import { cn } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Checkbox } from "@/components/ui/checkbox";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import { Badge } from "@/components/ui/badge";
import { Field, FieldGrid, Section, SubPanel } from "@/components/config/Field";
import { Info } from "@/components/ui/info";
import { AuthGate } from "@/components/config/AuthGate";
import { RestartRequiredDialog } from "@/components/config/RestartRequiredDialog";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { apiGet, apiPostJSON } from "@/lib/api";
import { auth } from "@/lib/auth";
import { toast } from "sonner";

type Cfg = Record<string, any>;

const DEFAULTS: Cfg = {
  // Defaults below mirror station.cfg.example so the UI shows what a fresh
  // install actually starts with, not arbitrary values invented by the UI.
  sdr_type: "RTLSDR", sdr_quantity: 1, sdr_hostname: "localhost", sdr_port: 5555,
  min_freq: 400.05, max_freq: 403.0, rx_timeout: 180,
  only_scan: [], never_scan: [], always_scan: [], always_decode: [],
  station_lat: 0, station_lon: 0, station_alt: 0,
  gpsd_enabled: false, gpsd_host: "localhost", gpsd_port: 2947,
  habitat_uploader_callsign: "CHANGEME", habitat_uploader_antenna: "1/4 wave monopole", habitat_upload_listener_position: true,
  sondehub_enabled: true, sondehub_upload_rate: 15,
  aprs_enabled: false, aprs_upload_rate: 30, aprs_user: "N0CALL", aprs_pass: "00000",
  aprs_server: "radiosondy.info", aprs_port: 14580, aprs_object_id: "<id>",
  aprs_use_custom_object_id: false,
  aprs_custom_comment: "Clb=<vel_v> t=<temp> h=<humidity> p=<pressure> <freq> Type=<type> ser=<id> Radiosonde",
  aprs_position_report: false,
  station_beacon_enabled: false, station_beacon_rate: 30,
  station_beacon_comment: "radiosonde_auto_rx SondeGate v<version>", station_beacon_icon: "/`",
  payload_summary_enabled: true, payload_summary_host: "<broadcast>", payload_summary_port: 55673,
  ozi_enabled: false, ozi_update_rate: 5, ozi_host: "<broadcast>", ozi_port: 8942,
  email_enabled: false,
  email_subject: "<type> Sonde launch detected on <freq>: <id>",
  email_nearby_landing_subject: "Nearby Radiosonde Landing Detected - <id>",
  email_launch_notifications: true, email_landing_notifications: true,
  email_encrypted_sonde_notifications: true, email_error_notifications: false,
  email_landing_range_threshold: 30, email_landing_altitude_threshold: 1000,
  rotator_enabled: false, rotator_hostname: "127.0.0.1", rotator_port: 4533,
  rotator_update_rate: 30, rotation_threshold: 5,
  rotator_homing_enabled: false, rotator_homing_delay: 10,
  rotator_home_azimuth: 0, rotator_home_elevation: 0, rotator_azimuth_only: false,
  per_sonde_log: true, save_system_log: false, enable_debug_logging: false, save_cal_data: false,
  web_host: "0.0.0.0", web_port: 5000, web_control: false,
  web_archive_age: 120, kml_refresh_rate: 10,
  save_detection_audio: false, save_decode_audio: false, save_decode_iq: false, save_raw_hex: false,
  max_altitude: 50000, max_radius_km: 1000, min_radius_km: 0, radius_temporary_block: false,
  enable_realtime_filter: true, max_velocity: 300, sonde_time_threshold: 3,
  search_step: 800, snr_threshold: 10, min_distance: 1000, max_peaks: 10,
  scan_dwell_time: 20, detect_dwell_time: 5, scan_delay: 10, quantization: 10000,
  decoder_spacing_limit: 15000, synchronous_upload: true, payload_id_valid: 3,
  temporary_block_time: 120, max_async_scan_workers: 4,
  // RS41/RS92/M10/DFM/LMS6-400/M20 _experimental keys are no longer
  // honoured by config.py (those decoders are now always the fsk_demod
  // chain) — left out of DEFAULTS so the UI can't pretend to toggle them.
  imet54_experimental: true, meisei_experimental: true, mrz_experimental: false,
  lms6_1680_experimental: false,
  ngp_tweak: false, wideband_sondes: false, close_on_encrypted: true,
};

const TAB_KEYS: Record<string, string[]> = {
  station: ["habitat_uploader_callsign", "habitat_uploader_antenna", "habitat_upload_listener_position", "station_lat", "station_lon", "station_alt", "gpsd_enabled", "gpsd_host", "gpsd_port"],
  sdr: ["sdr_type", "sdr_quantity", "sdr_hostname", "sdr_port"],
  freq: ["min_freq", "max_freq", "rx_timeout", "only_scan", "never_scan", "always_scan", "always_decode"],
  uploaders: ["sondehub_enabled", "sondehub_upload_rate", "aprs_enabled", "aprs_upload_rate", "aprs_user", "aprs_pass", "aprs_server", "aprs_port", "aprs_object_id", "aprs_use_custom_object_id", "aprs_custom_comment", "aprs_position_report", "station_beacon_enabled", "station_beacon_rate", "station_beacon_comment", "station_beacon_icon", "payload_summary_enabled", "payload_summary_host", "payload_summary_port", "ozi_enabled", "ozi_update_rate", "ozi_host", "ozi_port"],
  notify: ["email_enabled", "email_subject", "email_nearby_landing_subject", "email_launch_notifications", "email_landing_notifications", "email_encrypted_sonde_notifications", "email_error_notifications", "email_landing_range_threshold", "email_landing_altitude_threshold"],
  rotator: ["rotator_enabled", "rotator_hostname", "rotator_port", "rotator_update_rate", "rotation_threshold", "rotator_homing_enabled", "rotator_homing_delay", "rotator_home_azimuth", "rotator_home_elevation", "rotator_azimuth_only"],
  filter: ["max_altitude", "max_radius_km", "min_radius_km", "radius_temporary_block", "enable_realtime_filter", "max_velocity", "sonde_time_threshold"],
  advanced: ["search_step", "snr_threshold", "min_distance", "max_peaks", "scan_dwell_time", "detect_dwell_time", "scan_delay", "quantization", "decoder_spacing_limit", "synchronous_upload", "payload_id_valid", "temporary_block_time", "max_async_scan_workers", "imet54_experimental", "meisei_experimental", "mrz_experimental", "lms6_1680_experimental", "ngp_tweak", "wideband_sondes", "close_on_encrypted", "save_detection_audio", "save_decode_audio", "save_decode_iq", "save_raw_hex", "per_sonde_log", "save_system_log", "enable_debug_logging", "save_cal_data", "web_host", "web_port", "web_control", "web_archive_age", "kml_refresh_rate"],
};

/** Strip CR/LF/other control chars from every string value in the cfg payload.
 *  Important for fields that get forwarded into line-oriented protocols
 *  (e.g. aprs_custom_comment → APRS-IS), where a stray newline could forge
 *  a separate packet. Leaves non-string values untouched.
 */
function sanitiseStrings(cfg: Cfg): Cfg {
  const out: Cfg = { ...cfg };
  for (const k of Object.keys(out)) {
    if (typeof out[k] === "string") {
      // eslint-disable-next-line no-control-regex
      out[k] = (out[k] as string).replace(/[\x00-\x1f\x7f]/g, "").trim();
    }
  }
  return out;
}

export function Config() {
  const [cfg, setCfg] = useState<Cfg>(DEFAULTS);
  const [original, setOriginal] = useState<Cfg>(DEFAULTS);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  // True once /get_config has succeeded. We refuse to save until then —
  // otherwise a load error would let the user overwrite the real config with
  // DEFAULTS.
  const [loaded, setLoaded] = useState(false);
  const [restartFor, setRestartFor] = useState<string[] | null>(null);
  const [tab, setTab] = useState("station");
  const [errors, setErrors] = useState<Record<string, string>>({});

  useEffect(() => {
    (async () => {
      try {
        const live = await apiGet<Cfg>("/get_config");
        const merged: Cfg = { ...DEFAULTS, ...live };
        // Hydrate per-SDR list from sdr_settings if present, else seed from quantity
        if (!Array.isArray(merged.sdrs)) {
          const fromSettings = merged.sdr_settings && typeof merged.sdr_settings === "object"
            ? Object.entries(merged.sdr_settings).map(([device_idx, s]: [string, any]) => ({
                device_idx, ppm: s.ppm ?? 0, gain: s.gain ?? -1, bias: !!s.bias,
              }))
            : [];
          const want = Math.max(1, merged.sdr_quantity || 1);
          while (fromSettings.length < want) fromSettings.push({ device_idx: String(fromSettings.length), ppm: 0, gain: -1, bias: false });
          merged.sdrs = fromSettings.slice(0, want);
        }
        setCfg(merged);
        setOriginal(JSON.parse(JSON.stringify(merged)));
        setLoaded(true);
      } catch (e: any) { toast.error("Load config failed: " + (e.message || "")); }
      setLoading(false);
    })();
  }, []);

  const set = (k: string, v: any) => setCfg(c => ({ ...c, [k]: v }));

  // Keep the sdrs[] array length in sync with sdr_quantity.
  useEffect(() => {
    const want = Math.max(1, parseInt(String(cfg.sdr_quantity)) || 1);
    if (!Array.isArray(cfg.sdrs) || cfg.sdrs.length === want) return;
    setCfg(c => {
      const arr = Array.isArray(c.sdrs) ? [...c.sdrs] : [];
      while (arr.length < want) arr.push({ device_idx: String(arr.length), ppm: 0, gain: -1, bias: false });
      arr.length = want;
      return { ...c, sdrs: arr };
    });
  }, [cfg.sdr_quantity]);

  const setSdr = (i: number, k: string, v: any) => {
    setCfg(c => {
      const arr = Array.isArray(c.sdrs) ? [...c.sdrs] : [];
      arr[i] = { ...(arr[i] || {}), [k]: v };
      return { ...c, sdrs: arr };
    });
  };

  const dirtyKeys = useMemo(() => {
    return Object.keys(cfg).filter(k => JSON.stringify(cfg[k]) !== JSON.stringify(original[k]));
  }, [cfg, original]);
  const dirty = dirtyKeys.length > 0;
  const tabDirtyCount = (t: string) => {
    const ks = TAB_KEYS[t] || [];
    return dirtyKeys.filter(k => ks.includes(k)).length;
  };

  const validate = (): Record<string, string> => {
    const e: Record<string, string> = {};
    // typeof NaN === "number", so explicit isFinite checks.
    const num = (v: any) => typeof v === "number" && isFinite(v);
    const port = (v: any) => num(v) && v > 0 && v <= 65535 && v === Math.floor(v);
    const nonNeg = (v: any) => num(v) && v >= 0;

    // --- Station / position ----------------------------------------------------
    if (!num(cfg.station_lat) || cfg.station_lat < -90 || cfg.station_lat > 90) e.station_lat = "-90 to 90";
    if (!num(cfg.station_lon) || cfg.station_lon < -180 || cfg.station_lon > 180) e.station_lon = "-180 to 180";
    if (!num(cfg.station_alt)) e.station_alt = "Must be a number";
    if (!cfg.habitat_uploader_callsign || cfg.habitat_uploader_callsign === "CHANGEME") e.habitat_uploader_callsign = "Set a unique callsign";

    // --- Frequencies -----------------------------------------------------------
    if (!num(cfg.min_freq) || cfg.min_freq <= 0) e.min_freq = "Required";
    if (!num(cfg.max_freq) || cfg.max_freq <= cfg.min_freq) e.max_freq = "Must exceed min";
    if (!nonNeg(cfg.rx_timeout)) e.rx_timeout = "Must be ≥ 0";

    // --- SDR -------------------------------------------------------------------
    if (!num(cfg.sdr_quantity) || cfg.sdr_quantity < 1 || cfg.sdr_quantity > 32) e.sdr_quantity = "1 to 32";
    if (cfg.sdr_type !== "RTLSDR") {
      if (!cfg.sdr_hostname) e.sdr_hostname = "Required";
      if (!port(cfg.sdr_port)) e.sdr_port = "1 to 65535";
    }

    // --- Uploaders -------------------------------------------------------------
    if (cfg.aprs_enabled) {
      if (!cfg.aprs_user || cfg.aprs_user === "N0CALL") e.aprs_user = "Set your callsign";
      if (!cfg.aprs_pass || cfg.aprs_pass === "00000") e.aprs_pass = "Set passcode";
      if (!port(cfg.aprs_port)) e.aprs_port = "1 to 65535";
      if (!num(cfg.aprs_upload_rate) || cfg.aprs_upload_rate < 30) e.aprs_upload_rate = "Min 30s";
    }
    if (cfg.sondehub_enabled && (!num(cfg.sondehub_upload_rate) || cfg.sondehub_upload_rate < 10)) {
      e.sondehub_upload_rate = "Min 10s";
    }
    if (cfg.ozi_enabled && !port(cfg.ozi_port)) e.ozi_port = "1 to 65535";
    if (cfg.payload_summary_enabled && !port(cfg.payload_summary_port)) e.payload_summary_port = "1 to 65535";

    // --- Notifications ---------------------------------------------------------
    // SMTP server / port / from / to / login / password live in station.cfg
    // only — no validation here.

    // --- Rotator ---------------------------------------------------------------
    if (cfg.rotator_enabled) {
      if (!cfg.rotator_hostname) e.rotator_hostname = "Required";
      if (!port(cfg.rotator_port)) e.rotator_port = "1 to 65535";
      if (!num(cfg.rotator_update_rate) || cfg.rotator_update_rate < 1) e.rotator_update_rate = "≥ 1s";
      if (!num(cfg.rotation_threshold) || cfg.rotation_threshold < 0) e.rotation_threshold = "Must be ≥ 0";
      if (cfg.rotator_homing_enabled) {
        if (!num(cfg.rotator_home_azimuth) || cfg.rotator_home_azimuth < 0 || cfg.rotator_home_azimuth > 360) e.rotator_home_azimuth = "0 to 360";
        if (!num(cfg.rotator_home_elevation) || cfg.rotator_home_elevation < 0 || cfg.rotator_home_elevation > 90) e.rotator_home_elevation = "0 to 90";
      }
    }

    // --- Filtering -------------------------------------------------------------
    if (!nonNeg(cfg.max_altitude)) e.max_altitude = "Must be ≥ 0";
    if (!nonNeg(cfg.max_radius_km)) e.max_radius_km = "Must be ≥ 0";
    if (!nonNeg(cfg.min_radius_km)) e.min_radius_km = "Must be ≥ 0";
    if (num(cfg.max_radius_km) && num(cfg.min_radius_km) && cfg.max_radius_km > 0 && cfg.min_radius_km >= cfg.max_radius_km) {
      e.min_radius_km = "Must be less than max";
    }
    if (!nonNeg(cfg.max_velocity)) e.max_velocity = "Must be ≥ 0";

    // --- GPSD ------------------------------------------------------------------
    if (cfg.gpsd_enabled && !port(cfg.gpsd_port)) e.gpsd_port = "1 to 65535";

    // --- Web -------------------------------------------------------------------
    // web_password lives in station.cfg only — not editable from this UI.
    if (!port(cfg.web_port)) e.web_port = "1 to 65535";
    if (!nonNeg(cfg.web_archive_age)) e.web_archive_age = "Must be ≥ 0";
    if (!nonNeg(cfg.kml_refresh_rate) || cfg.kml_refresh_rate < 1) e.kml_refresh_rate = "≥ 1s";

    // --- Advanced scanner tuning ----------------------------------------------
    if (!nonNeg(cfg.search_step) || cfg.search_step <= 0) e.search_step = "Must be > 0";
    if (!num(cfg.snr_threshold)) e.snr_threshold = "Must be a number";
    if (!nonNeg(cfg.dwell_time)) e.dwell_time = "Must be ≥ 0";
    if (!nonNeg(cfg.scan_dwell_time)) e.scan_dwell_time = "Must be ≥ 0";
    if (!nonNeg(cfg.detect_dwell_time)) e.detect_dwell_time = "Must be ≥ 0";
    if (!nonNeg(cfg.scan_delay)) e.scan_delay = "Must be ≥ 0";
    if (!nonNeg(cfg.max_peaks) || cfg.max_peaks < 1) e.max_peaks = "Must be ≥ 1";
    if (!nonNeg(cfg.temporary_block_time)) e.temporary_block_time = "Must be ≥ 0";

    setErrors(e);
    return e;
  };

  const save = async () => {
    if (!loaded) {
      toast.error("Configuration hasn't loaded yet — refusing to save and overwrite the real config with defaults.");
      return;
    }
    const e = validate();
    if (Object.keys(e).length > 0) {
      toast.error("Fix highlighted fields");
      const firstErrKey = Object.keys(e)[0];
      if (firstErrKey) {
        for (const t of Object.keys(TAB_KEYS)) if (TAB_KEYS[t].includes(firstErrKey)) { setTab(t); break; }
      }
      return;
    }
    setSaving(true);
    try {
      // Strip control characters (esp. newlines / CR) from any user-supplied
      // string before save. Important for things like aprs_custom_comment that
      // get forwarded verbatim into line-oriented protocols (APRS-IS) where a
      // \n would forge a separate packet.
      const sanitised = sanitiseStrings(cfg);
      const payload = { ...sanitised, __password: auth.password() };
      const res = await apiPostJSON<{ ok: boolean; errors?: string[]; written?: string[]; restart_required?: string[]; unknown?: string[] }>("/save_config", payload);
      if (res.ok) {
        const n = (res.written || []).length;
        toast.success(`Saved ${n} change${n === 1 ? "" : "s"}`);
        if ((res.unknown || []).length) console.warn("[/save_config] unknown keys (no file mapping):", res.unknown);
        setOriginal(JSON.parse(JSON.stringify(cfg)));
        if ((res.restart_required || []).length) setRestartFor(res.restart_required as string[]);
      } else { toast.error((res.errors || []).join("; ") || "Save failed"); }
    } catch (e: any) { toast.error(e.message || "Save failed"); }
    setSaving(false);
  };

  const revert = () => { setCfg(JSON.parse(JSON.stringify(original))); setErrors({}); toast.info("Reverted unsaved changes"); };

  const addFreq = (k: string) => { setCfg(c => ({ ...c, [k]: [...(c[k] || []), 0] })); };
  const updFreq = (k: string, i: number, v: any) => { setCfg(c => { const arr = [...(c[k] || [])]; arr[i] = v; return { ...c, [k]: arr }; }); };
  const rmFreq = (k: string, i: number) => { setCfg(c => { const arr = [...(c[k] || [])]; arr.splice(i, 1); return { ...c, [k]: arr }; }); };

  if (loading) return <div className="text-xs text-muted-foreground p-8 text-center">Loading configuration…</div>;

  // When the operator hasn't opted in to web config editing, only the "View"
  // tab is rendered. View prefs (theme, units, marker style, etc.) are
  // browser-side localStorage and have no security implications, so they
  // remain accessible regardless of the station-side flag.
  if (!original.web_config_enabled) {
    return (
      <div className="space-y-3">
        <ViewPrefsPanel />
        <p className="text-[11px] text-muted-foreground mt-4 max-w-md leading-relaxed">
          Editing <code className="mono">station.cfg</code> from the web is
          disabled on this station. To enable, add{" "}
          <code className="mono">web_config_enabled = True</code> under{" "}
          <code className="mono">[web]</code> in{" "}
          <code className="mono">station.cfg</code> and restart auto_rx.
        </p>
      </div>
    );
  }

  return (
    // Gate on the *original* (server-side) value, not the in-progress edit.
    // If we gated on `cfg.web_control`, toggling it off would instantly hide
    // the entire settings UI (including the Save button) before the user got
    // a chance to commit the change.
    <AuthGate webControlEnabled={!!original.web_control}>
    <div className="space-y-3">
      <Tabs value={tab} onValueChange={setTab}>
        <TabsList className="flex-wrap h-auto overflow-x-auto whitespace-nowrap w-full sm:w-auto">
          {[
            { v: "station", l: "Station", I: Home },
            { v: "view", l: "View", I: Eye },
            { v: "sdr", l: "SDR", I: Cpu },
            { v: "freq", l: "Frequencies", I: Waves },
            { v: "uploaders", l: "Uploaders", I: Upload },
            { v: "notify", l: "Notifications", I: Bell },
            { v: "rotator", l: "Rotator", I: AntennaIcon },
            { v: "filter", l: "Filtering", I: FilterIcon },
            { v: "advanced", l: "Advanced", I: Sliders },
          ].map(({ v, l, I }) => {
            const n = tabDirtyCount(v);
            return (
              <TabsTrigger key={v} value={v} className="gap-1.5">
                <I className="w-3.5 h-3.5" strokeWidth={1.75} /> {l}
                {n > 0 && <Badge variant="warn" className="ml-1 px-1 py-0">{n}</Badge>}
              </TabsTrigger>
            );
          })}
        </TabsList>

        {/* STATION */}
        <TabsContent value="station">
          <Section title="Station" description="Your callsign, location, and how you appear on public trackers.">
            <FieldGrid cols={2}>
              <Field label="Uploader callsign" htmlFor="callsign" span="full" error={errors.habitat_uploader_callsign}
                tip="A unique identifier for your station, shown on tracker.sondehub.org. Avoid 'CHANGEME'.">
                <Input id="callsign" className="mono" value={cfg.habitat_uploader_callsign} onChange={e => set("habitat_uploader_callsign", e.target.value)} />
              </Field>
              <Field label="Antenna description" htmlFor="ant" span="full"
                tip='Shown in the hover info on SondeHub. e.g., "1/4 wave vertical at 6m AGL".'>
                <Input id="ant" value={cfg.habitat_uploader_antenna} onChange={e => set("habitat_uploader_antenna", e.target.value)} />
              </Field>
              <Field label="Latitude" htmlFor="lat" error={errors.station_lat} tip="Decimal degrees. Negative south of the equator.">
                <Input id="lat" type="number" step={0.000001} className="mono" value={cfg.station_lat} onChange={e => set("station_lat", parseFloat(e.target.value))} />
              </Field>
              <Field label="Longitude" htmlFor="lon" error={errors.station_lon} tip="Decimal degrees. Negative west of the prime meridian.">
                <Input id="lon" type="number" step={0.000001} className="mono" value={cfg.station_lon} onChange={e => set("station_lon", parseFloat(e.target.value))} />
              </Field>
              <Field label="Altitude (m AMSL)" htmlFor="alt" tip="Above mean sea level, in metres.">
                <Input id="alt" type="number" step={0.1} className="mono" value={cfg.station_alt} onChange={e => set("station_alt", parseFloat(e.target.value))} />
              </Field>
              <Field label="Privacy" tip="When on, your listener position is uploaded to SondeHub and your station shows on the public map. Turn off to stay hidden.">
                <span className="flex items-center gap-2 h-7">
                  <Switch checked={cfg.habitat_upload_listener_position} onCheckedChange={v => set("habitat_upload_listener_position", v)} />
                  <span className="text-xs">Upload my position to SondeHub</span>
                </span>
              </Field>
            </FieldGrid>

            <SubPanel title="GPSD — moving / portable station" badge={<Badge variant={cfg.gpsd_enabled ? "signal" : "ghost"} className="ml-auto">{cfg.gpsd_enabled ? "Enabled" : "Off"}</Badge>}>
              <FieldGrid cols={3}>
                <Field label="Enable" tip="Read the station's position from a local GPSD daemon.">
                  <span className="flex items-center gap-2 h-7"><Switch checked={cfg.gpsd_enabled} onCheckedChange={v => set("gpsd_enabled", v)} /><span className="text-xs">{cfg.gpsd_enabled ? "On" : "Off"}</span></span>
                </Field>
                <Field label="GPSD host" htmlFor="gpsdh" tip="Hostname or IP of the gpsd daemon. 'localhost' for the same machine."><Input id="gpsdh" className="mono" value={cfg.gpsd_host} onChange={e => set("gpsd_host", e.target.value)} disabled={!cfg.gpsd_enabled} /></Field>
                <Field label="GPSD port" htmlFor="gpsdp" tip="gpsd's TCP port. Default is 2947."><Input id="gpsdp" type="number" className="mono" value={cfg.gpsd_port} onChange={e => set("gpsd_port", parseInt(e.target.value) || 0)} disabled={!cfg.gpsd_enabled} /></Field>
              </FieldGrid>
            </SubPanel>
          </Section>
        </TabsContent>

        {/* VIEW — browser-side prefs (theme, marker style, time format, units, etc).
            These don't go through the diff/save flow; they persist in localStorage. */}
        <TabsContent value="view">
          <ViewPrefsPanel />
        </TabsContent>

        {/* SDR */}
        <TabsContent value="sdr">
          <Section title="Software-defined radios" description="Hardware backend and per-radio settings.">
            <FieldGrid cols={2}>
              <Field label="Backend" htmlFor="sdrtype" tip="RTLSDR for cheap USB sticks; SpyServer or KA9Q for networked radios.">
                <Select value={cfg.sdr_type} onValueChange={v => set("sdr_type", v)}>
                  <SelectTrigger id="sdrtype"><SelectValue /></SelectTrigger>
                  <SelectContent>
                    {["RTLSDR", "SpyServer", "KA9Q"].map(t => <SelectItem key={t} value={t}>{t}</SelectItem>)}
                  </SelectContent>
                </Select>
              </Field>
              <Field label="Number of SDRs" htmlFor="sdrq" tip="How many physical radios (RTLSDR) or parallel tasks (network SDR) to run.">
                <Input id="sdrq" type="number" min={1} max={32} className="mono" value={cfg.sdr_quantity} onChange={e => set("sdr_quantity", parseInt(e.target.value) || 1)} />
              </Field>
              {cfg.sdr_type !== "RTLSDR" && (
                <>
                  <Field label="Network SDR host" htmlFor="sdrh" tip="IP or hostname of the SpyServer or KA9Q radiod machine."><Input id="sdrh" className="mono" value={cfg.sdr_hostname} onChange={e => set("sdr_hostname", e.target.value)} /></Field>
                  <Field label="Network SDR port" htmlFor="sdrp" tip="SpyServer default is 5555. KA9Q radiod varies — check your radiod.conf."><Input id="sdrp" type="number" className="mono" value={cfg.sdr_port} onChange={e => set("sdr_port", parseInt(e.target.value) || 0)} /></Field>
                </>
              )}
            </FieldGrid>

            {/* Per-SDR cards — driven by sdr_quantity. Only meaningful for
                RTLSDR. For SpyServer / KA9Q, config.py hardcodes per-radio
                ppm/gain/bias to 0 and auto-generates channel names (SPY01,
                KA9Q-01…) — typing values here would save them to station.cfg
                but the loader never reads them back. Hide the cards entirely
                for those backends rather than show controls that do nothing. */}
            <SubPanel
              title={`Radios (${(cfg.sdrs || []).length})`}
              badge={
                <Badge variant="ghost" className="ml-auto">
                  {cfg.sdr_type === "RTLSDR" ? "Local USB" : cfg.sdr_type === "KA9Q" ? "KA9Q radiod" : cfg.sdr_type === "SpyServer" ? "SpyServer" : cfg.sdr_type}
                </Badge>
              }
            >
              {cfg.sdr_type !== "RTLSDR" ? (
                <p className="text-[11px] text-muted-foreground">
                  {cfg.sdr_type === "SpyServer"
                    ? "SpyServer mode auto-generates one task per slot (SPY01, SPY02…) — there are no per-radio settings here. Set the count with 'Number of SDRs' above and the network host/port handles the rest."
                    : "KA9Q mode auto-generates one task per slot (KA9Q-01, KA9Q-02…). Per-channel gain/ppm is controlled by radiod itself, not auto_rx. Use 'Number of SDRs' above to set how many parallel tasks to run."}
                </p>
              ) : (
              <>
              <p className="text-[11px] text-muted-foreground mb-3">
                Each radio writes a [sdr_N] section in station.cfg. Use rtl_eeprom to set distinct serials for multi-SDR setups.
              </p>
              <div className="space-y-2">
                {(cfg.sdrs || []).map((s: any, i: number) => (
                  <div key={i} className="rounded-md border border-border bg-background/40 p-3">
                    <div className="flex items-center gap-2 mb-3">
                      <Radio className="w-3.5 h-3.5 text-signal" />
                      <span className="text-[10px] uppercase tracking-widest text-muted-foreground font-semibold mono">SDR <span className="text-foreground/80">{i + 1}</span></span>
                    </div>
                    <FieldGrid cols={cfg.sdr_type === "RTLSDR" ? 4 : 3}>
                      <Field
                        label={cfg.sdr_type === "RTLSDR" ? "Device serial / index" : cfg.sdr_type === "KA9Q" ? "Channel name" : "SpyServer task ID"}
                        tip={
                          cfg.sdr_type === "RTLSDR"
                            ? "rtl_eeprom serial, or '0' for a single-radio setup. Multi-SDR setups MUST use unique serials, not '0'."
                            : cfg.sdr_type === "KA9Q"
                              ? "Multicast/channel name from radiod.conf — e.g. ‘sondes-0’."
                              : "A short identifier for this SpyServer worker; arbitrary."
                        }
                      >
                        <Input className="mono" value={s.device_idx} onChange={e => setSdr(i, "device_idx", e.target.value)} />
                      </Field>
                      <Field label="PPM correction" tip="Frequency error in parts-per-million. Leave 0 if unknown.">
                        <Input type="number" className="mono" value={s.ppm} onChange={e => setSdr(i, "ppm", parseFloat(e.target.value) || 0)} />
                      </Field>
                      <Field label="Gain (dB)" tip="-1 = automatic. Typical RTLSDR values: 16.6, 19.7, 30, 38.6.">
                        <Input type="number" step={0.1} className="mono" value={s.gain} onChange={e => setSdr(i, "gain", parseFloat(e.target.value))} />
                      </Field>
                      {cfg.sdr_type === "RTLSDR" && (
                        <Field label="Bias-T" tip="Enable bias tee on RTL-SDR Blog V3 (5V on the antenna line).">
                          <span className="flex items-center gap-2 h-7">
                            <Switch checked={!!s.bias} onCheckedChange={v => setSdr(i, "bias", v)} />
                            <span className="text-xs">{s.bias ? "On" : "Off"}</span>
                          </span>
                        </Field>
                      )}
                    </FieldGrid>
                  </div>
                ))}
              </div>

              {cfg.sdr_quantity > 1 && cfg.sdr_type === "RTLSDR" && (
                <p className="text-[10px] mono text-warn mt-3">
                  ⚠ Multi-SDR with RTLSDR: every device serial above must be unique, and none can be "0". Set serials with rtl_eeprom.
                </p>
              )}
              {cfg.sdr_quantity > 1 && (cfg.aprs_enabled || cfg.ozi_enabled || cfg.rotator_enabled) && (
                <p className="text-[10px] mono text-warn mt-1">
                  ⚠ Multi-SDR with rotator / OziMux is unsupported. APRS object IDs should stay as <span className="text-foreground/80">&lt;id&gt;</span>.
                </p>
              )}
              </>
              )}
            </SubPanel>

            <SubPanel title="Decoder binary paths">
              <p className="text-[11px] text-muted-foreground">
                <code className="mono">sdr_fm_path</code>,{" "}
                <code className="mono">sdr_power_path</code>,{" "}
                <code className="mono">ss_iq_path</code>, and{" "}
                <code className="mono">ss_power_path</code> live in
                <code className="mono">station.cfg</code> under
                <code className="mono">[advanced]</code> — they're not editable
                from this UI for security (these are exec'd on every scan).
              </p>
            </SubPanel>
          </Section>
        </TabsContent>

        {/* FREQUENCIES */}
        <TabsContent value="freq">
          <Section title="Frequencies" description="What bands and channels the scanner watches.">
            <FieldGrid cols={3}>
              <Field label="Min frequency (MHz)" htmlFor="minf" error={errors.min_freq} tip="Lower edge of the scan band. Common: 400.05 or 402.0 for 400 MHz sondes."><Input id="minf" type="number" step={0.001} className="mono" value={cfg.min_freq} onChange={e => set("min_freq", parseFloat(e.target.value))} /></Field>
              <Field label="Max frequency (MHz)" htmlFor="maxf" error={errors.max_freq} tip="Upper edge of the scan band. Common: 403.0 or 406.0 depending on region."><Input id="maxf" type="number" step={0.001} className="mono" value={cfg.max_freq} onChange={e => set("max_freq", parseFloat(e.target.value))} /></Field>
              <Field label="RX timeout (s)" htmlFor="rxt" tip="Stop following a sonde after this many seconds of silence."><Input id="rxt" type="number" className="mono" value={cfg.rx_timeout} onChange={e => set("rx_timeout", parseInt(e.target.value) || 0)} /></Field>
            </FieldGrid>

            <FieldGrid cols={2}>
              {(["never_scan", "only_scan", "always_scan"] as const).map(k => (
                <SubPanel key={k} title={k.replace("_", " ")}>
                  <p className="text-[11px] text-muted-foreground mb-2">
                    {k === "never_scan" && "Block these frequencies — useful for known spurs."}
                    {k === "only_scan" && "Whitelist — if any, only these are watched."}
                    {k === "always_scan" && "Added to every scan start, even outside main range."}
                  </p>
                  <div className="space-y-1.5">
                    {(cfg[k] || []).map((f: number, i: number) => (
                      <div key={i} className="flex items-center gap-2">
                        <Input type="number" step={0.001} className="mono" value={f} onChange={e => updFreq(k, i, parseFloat(e.target.value))} placeholder="MHz" />
                        <Button size="icon-sm" variant="ghost" onClick={() => rmFreq(k, i)}><X className="w-3 h-3" /></Button>
                      </div>
                    ))}
                    <Button size="sm" variant="default" onClick={() => addFreq(k)}><Plus className="w-3 h-3" /> Add</Button>
                  </div>
                </SubPanel>
              ))}
              <SubPanel title="always decode">
                <p className="text-[11px] text-muted-foreground mb-2">Fixed-frequency decoders. Each needs its own SDR.</p>
                <div className="space-y-1.5">
                  {(cfg.always_decode || []).map((row: any[], i: number) => (
                    <div key={i} className="flex items-center gap-2">
                      <Input type="number" step={0.001} className="mono flex-1" value={row[0]} onChange={e => { const arr = [...cfg.always_decode]; arr[i] = [parseFloat(e.target.value), arr[i][1] || "RS41"]; set("always_decode", arr); }} placeholder="MHz" />
                      <Select value={row[1] || "RS41"} onValueChange={v => { const arr = [...cfg.always_decode]; arr[i] = [arr[i][0], v]; set("always_decode", arr); }}>
                        <SelectTrigger className="w-28"><SelectValue /></SelectTrigger>
                        <SelectContent>
                          {["RS41", "RS92", "DFM", "M10", "M20", "iMet", "iMet54", "MEISEI", "MRZ", "LMS6-400", "LMS6-1680"].map(t => <SelectItem key={t} value={t}>{t}</SelectItem>)}
                        </SelectContent>
                      </Select>
                      <Button size="icon-sm" variant="ghost" onClick={() => rmFreq("always_decode", i)}><X className="w-3 h-3" /></Button>
                    </div>
                  ))}
                  <Button size="sm" variant="default" onClick={() => set("always_decode", [...(cfg.always_decode || []), [0, "RS41"]])}><Plus className="w-3 h-3" /> Add</Button>
                </div>
              </SubPanel>
            </FieldGrid>
          </Section>
        </TabsContent>

        {/* UPLOADERS */}
        <TabsContent value="uploaders">
          <Section title="Uploaders" description="Push telemetry to public networks and local chase tools.">
            <SubPanel title="SondeHub" badge={<Badge variant={cfg.sondehub_enabled ? "signal" : "ghost"} className="ml-auto">{cfg.sondehub_enabled ? "Enabled" : "Off"}</Badge>}>
              <FieldGrid cols={3}>
                <Field label="Enable" tip="Upload decoded telemetry to tracker.sondehub.org. Strongly recommended — it's free and how everyone tracks sondes."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.sondehub_enabled} onCheckedChange={v => set("sondehub_enabled", v)} /><span className="text-xs">{cfg.sondehub_enabled ? "On" : "Off"}</span></span></Field>
                <Field label="Upload rate (s)" error={errors.sondehub_upload_rate} tip="Batch upload interval. Minimum 10 seconds.">
                  <Input type="number" min={10} className="mono" value={cfg.sondehub_upload_rate} onChange={e => set("sondehub_upload_rate", parseInt(e.target.value) || 0)} disabled={!cfg.sondehub_enabled} />
                </Field>
              </FieldGrid>
            </SubPanel>

            <SubPanel title="APRS-IS" badge={<Badge variant={cfg.aprs_enabled ? "signal" : "ghost"} className="ml-auto">{cfg.aprs_enabled ? "Enabled" : "Off"}</Badge>}>
              <FieldGrid cols={3}>
                <Field label="Enable" tip="Forward telemetry to the APRS-IS network. Requires an amateur radio callsign + passcode."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.aprs_enabled} onCheckedChange={v => set("aprs_enabled", v)} /><span className="text-xs">{cfg.aprs_enabled ? "On" : "Off"}</span></span></Field>
                <Field label="Callsign" error={errors.aprs_user} tip="Your amateur radio callsign with SSID, e.g. N0CALL-13."><Input className="mono" value={cfg.aprs_user} onChange={e => set("aprs_user", e.target.value)} disabled={!cfg.aprs_enabled} /></Field>
                <Field label="Passcode" error={errors.aprs_pass} tip="Generate at apps.magicbug.co.uk/passcode/"><Input className="mono" value={cfg.aprs_pass} onChange={e => set("aprs_pass", e.target.value)} disabled={!cfg.aprs_enabled} /></Field>
                <Field label="Server" tip="APRS-IS gateway. radiosondy.info and wettersonde.net are sonde-specialized; use localhost only if running an APRS-IS gateway yourself.">
                  <Select value={cfg.aprs_server} onValueChange={v => set("aprs_server", v)} disabled={!cfg.aprs_enabled}>
                    <SelectTrigger><SelectValue /></SelectTrigger>
                    <SelectContent>{["radiosondy.info", "wettersonde.net", "localhost"].map(s => <SelectItem key={s} value={s}>{s}</SelectItem>)}</SelectContent>
                  </Select>
                </Field>
                <Field label="Port" tip="14580 is the standard APRS-IS filter port; 14590 some servers use for raw.">
                  <Select value={String(cfg.aprs_port)} onValueChange={v => set("aprs_port", parseInt(v))} disabled={!cfg.aprs_enabled}>
                    <SelectTrigger><SelectValue /></SelectTrigger>
                    <SelectContent>{[14580, 14590].map(p => <SelectItem key={p} value={String(p)}>{p}</SelectItem>)}</SelectContent>
                  </Select>
                </Field>
                <Field label="Upload rate (s, ≥30)" error={errors.aprs_upload_rate} tip="How often to send updates to APRS-IS. 30 s is the network's polite minimum."><Input type="number" min={30} className="mono" value={cfg.aprs_upload_rate} onChange={e => set("aprs_upload_rate", parseInt(e.target.value) || 0)} disabled={!cfg.aprs_enabled} /></Field>
                <Field label="Custom comment template" span="full" tip="Placeholders: <freq> <type> <id> <vel_v> <temp> <humidity> <pressure> <batt>"><Input className="mono" value={cfg.aprs_custom_comment} onChange={e => set("aprs_custom_comment", e.target.value)} disabled={!cfg.aprs_enabled} /></Field>
                <Field label="Position report" tip="Send as position report instead of object report — affects aprs.fi display.">
                  <span className="flex items-center gap-2 h-7"><Switch checked={cfg.aprs_position_report} onCheckedChange={v => set("aprs_position_report", v)} disabled={!cfg.aprs_enabled} /><span className="text-xs">{cfg.aprs_position_report ? "On" : "Off"}</span></span>
                </Field>
                <Field label="Custom object ID" tip="Advanced — required for multi-SDR setups.">
                  <span className="flex items-center gap-2 h-7"><Switch checked={cfg.aprs_use_custom_object_id} onCheckedChange={v => set("aprs_use_custom_object_id", v)} disabled={!cfg.aprs_enabled} /><span className="text-xs">{cfg.aprs_use_custom_object_id ? "On" : "Off"}</span></span>
                </Field>
              </FieldGrid>

              <div className="mt-3 rounded-md border border-border bg-card/40 p-3">
                <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-2">Station beacon</div>
                <FieldGrid cols={3}>
                  <Field label="Enable" tip="Periodically broadcast your station's own position on APRS-IS so other listeners can see who's listening."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.station_beacon_enabled} onCheckedChange={v => set("station_beacon_enabled", v)} disabled={!cfg.aprs_enabled} /><span className="text-xs">{cfg.station_beacon_enabled ? "On" : "Off"}</span></span></Field>
                  <Field label="Rate (min)" tip="How often to send the station beacon. 30 minutes is the typical polite default."><Input type="number" className="mono" value={cfg.station_beacon_rate} onChange={e => set("station_beacon_rate", parseInt(e.target.value) || 0)} disabled={!cfg.station_beacon_enabled} /></Field>
                  <Field label="Icon" tip="Two-char APRS symbol code. Use /` (slash + backtick — satellite dish) so your station shows up on radiosondy.info. Other symbols won't be picked up by their sonde gateway list."><Input className="mono" maxLength={2} value={cfg.station_beacon_icon} onChange={e => set("station_beacon_icon", e.target.value)} disabled={!cfg.station_beacon_enabled} /></Field>
                  <Field label="Comment" span="full" tip="Free-form text shown next to your station on APRS-IS clients."><Input className="mono" value={cfg.station_beacon_comment} onChange={e => set("station_beacon_comment", e.target.value)} disabled={!cfg.station_beacon_enabled} /></Field>
                </FieldGrid>
              </div>
            </SubPanel>

            <SubPanel title="ChaseMapper / OziPlotter (local UDP)">
              <Field label="Payload summary broadcast" tip="Recommended JSON broadcast — works with horus-gui, ChaseMapper.">
                <span className="flex items-center gap-2 h-7"><Switch checked={cfg.payload_summary_enabled} onCheckedChange={v => set("payload_summary_enabled", v)} /><span className="text-xs">{cfg.payload_summary_enabled ? "On" : "Off"}</span></span>
              </Field>
              {cfg.payload_summary_enabled && (
                <FieldGrid cols={2} className="mt-3">
                  <Field label="Host" tip="Broadcast address. 127.0.0.1 for local-only; 255.255.255.255 for LAN-wide."><Input className="mono" value={cfg.payload_summary_host} onChange={e => set("payload_summary_host", e.target.value)} /></Field>
                  <Field label="Port" tip="UDP port for the payload summary JSON. ChaseMapper / horus-gui default is 55672."><Input type="number" className="mono" value={cfg.payload_summary_port} onChange={e => set("payload_summary_port", parseInt(e.target.value) || 0)} /></Field>
                </FieldGrid>
              )}
              <div className="my-3 border-t border-border" />
              <Field label="Legacy OziMux CSV broadcast" tip="Single-SDR only. Most users leave this off.">
                <span className="flex items-center gap-2 h-7"><Switch checked={cfg.ozi_enabled} onCheckedChange={v => set("ozi_enabled", v)} /><span className="text-xs">{cfg.ozi_enabled ? "On" : "Off"}</span></span>
              </Field>
              {cfg.ozi_enabled && (
                <FieldGrid cols={3} className="mt-3">
                  <Field label="Host" tip="UDP destination for the OziPlotter CSV stream. Usually 127.0.0.1 or 255.255.255.255."><Input className="mono" value={cfg.ozi_host} onChange={e => set("ozi_host", e.target.value)} /></Field>
                  <Field label="Port" tip="OziPlotter default UDP port is 8942."><Input type="number" className="mono" value={cfg.ozi_port} onChange={e => set("ozi_port", parseInt(e.target.value) || 0)} /></Field>
                  <Field label="Update rate (s)" tip="How often to push a CSV line. Default 5 s."><Input type="number" className="mono" value={cfg.ozi_update_rate} onChange={e => set("ozi_update_rate", parseInt(e.target.value) || 0)} /></Field>
                </FieldGrid>
              )}
            </SubPanel>
          </Section>
        </TabsContent>

        {/* NOTIFY */}
        <TabsContent value="notify">
          <Section title="E-mail notifications" description="Alerts for launches, landings, and errors.">
            <SubPanel title="E-mail" badge={<Badge variant={cfg.email_enabled ? "signal" : "ghost"} className="ml-auto">{cfg.email_enabled ? "Enabled" : "Off"}</Badge>}>
              <FieldGrid cols={1}>
                <Field label="Enable" tip="Send e-mail alerts for events like new sonde detected or nearby landing."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.email_enabled} onCheckedChange={v => set("email_enabled", v)} /><span className="text-xs">{cfg.email_enabled ? "On" : "Off"}</span></span></Field>
                <p className="text-[11px] text-muted-foreground">
                  SMTP server, port, authentication, login, password, from, and
                  recipients live in <code className="mono">station.cfg</code>
                  under <code className="mono">[email]</code> — they're not
                  editable from this UI for security.
                </p>
              </FieldGrid>
            </SubPanel>

            <SubPanel title="Triggers">
              <FieldGrid cols={2}>
                {([
                  ["email_launch_notifications", "New sonde detected (launch)", "Send an alert the first time a sonde's serial is decoded — i.e. you've started receiving a new flight."],
                  ["email_landing_notifications", "Sonde landing nearby", "Send an alert when a sonde drops below the altitude threshold AND comes within the range threshold below."],
                  ["email_encrypted_sonde_notifications", "Encrypted sonde detected", "Send an alert when an encrypted (e.g. military) RS41 is detected — these can't be decoded but you may want to know."],
                  ["email_error_notifications", "auto_rx critical errors", "Send an alert when auto_rx logs a critical error (SDR failure, decoder crash, etc.)."],
                ] as const).map(([k, label, tip]) => (
                  <span key={k} className="flex items-center gap-2">
                    <Checkbox checked={!!cfg[k]} onCheckedChange={v => set(k, !!v)} disabled={!cfg.email_enabled} />
                    <span className="text-xs">{label}</span>
                    <Info tip={tip} />
                  </span>
                ))}
              </FieldGrid>
              {cfg.email_landing_notifications && (
                <FieldGrid cols={2} className="mt-3">
                  <Field label="Landing range threshold (km)" tip="Only alert if the predicted landing is within this distance of your station."><Input type="number" step={0.5} className="mono" value={cfg.email_landing_range_threshold} onChange={e => set("email_landing_range_threshold", parseFloat(e.target.value) || 0)} disabled={!cfg.email_enabled} /></Field>
                  <Field label="Landing altitude threshold (m)" tip="Only alert once the sonde drops below this altitude — avoids early/false 'landing' triggers mid-flight."><Input type="number" className="mono" value={cfg.email_landing_altitude_threshold} onChange={e => set("email_landing_altitude_threshold", parseInt(e.target.value) || 0)} disabled={!cfg.email_enabled} /></Field>
                </FieldGrid>
              )}
            </SubPanel>

            <SubPanel title="Templates">
              <FieldGrid cols={1}>
                <Field label="Launch subject" tip="Subject line for new-sonde alerts. Placeholders: <id> <type> <freq> get replaced."><Input className="mono" value={cfg.email_subject} onChange={e => set("email_subject", e.target.value)} disabled={!cfg.email_enabled} /></Field>
                <Field label="Landing subject" tip="Subject line for nearby-landing alerts. Same placeholders as the launch subject."><Input className="mono" value={cfg.email_nearby_landing_subject} onChange={e => set("email_nearby_landing_subject", e.target.value)} disabled={!cfg.email_enabled} /></Field>
              </FieldGrid>
            </SubPanel>
          </Section>
        </TabsContent>

        {/* ROTATOR */}
        <TabsContent value="rotator">
          <Section title="Antenna rotator" description="Drive an AZ/EL rotator via rotctld.">
            <SubPanel title="Tracking" badge={<Badge variant={cfg.rotator_enabled ? "signal" : "ghost"} className="ml-auto">{cfg.rotator_enabled ? "Enabled" : "Off"}</Badge>}>
              <FieldGrid cols={3}>
                <Field label="Enable" tip="Track the live sonde with an AZ/EL antenna rotator via hamlib's rotctld."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.rotator_enabled} onCheckedChange={v => set("rotator_enabled", v)} /><span className="text-xs">{cfg.rotator_enabled ? "On" : "Off"}</span></span></Field>
                <Field label="rotctld host" tip="Hostname or IP running rotctld. 'localhost' if it's on the same machine."><Input className="mono" value={cfg.rotator_hostname} onChange={e => set("rotator_hostname", e.target.value)} disabled={!cfg.rotator_enabled} /></Field>
                <Field label="rotctld port" tip="rotctld's TCP port. Default is 4533."><Input type="number" className="mono" value={cfg.rotator_port} onChange={e => set("rotator_port", parseInt(e.target.value) || 0)} disabled={!cfg.rotator_enabled} /></Field>
                <Field label="Update rate (s)" tip="How often to recompute target az/el and command the rotator."><Input type="number" className="mono" value={cfg.rotator_update_rate} onChange={e => set("rotator_update_rate", parseInt(e.target.value) || 0)} disabled={!cfg.rotator_enabled} /></Field>
                <Field label="Rotation threshold (°)" tip="Only move the rotator if the new target differs from the current heading by more than this many degrees. Avoids constant micro-movements.">
                  <Input type="number" step={0.1} className="mono" value={cfg.rotation_threshold} onChange={e => set("rotation_threshold", parseFloat(e.target.value) || 0)} disabled={!cfg.rotator_enabled} />
                </Field>
                <Field label="Azimuth-only rotator" tip="Check if your rotator only swings horizontally (no elevation axis). Skips elevation commands."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.rotator_azimuth_only} onCheckedChange={v => set("rotator_azimuth_only", v)} disabled={!cfg.rotator_enabled} /><span className="text-xs">Yes</span></span></Field>
              </FieldGrid>
            </SubPanel>

            <SubPanel title="Homing">
              <FieldGrid cols={3}>
                <Field label="Return to home when idle" tip="When no sonde is being tracked, return the rotator to a parking position after a delay."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.rotator_homing_enabled} onCheckedChange={v => set("rotator_homing_enabled", v)} disabled={!cfg.rotator_enabled} /><span className="text-xs">{cfg.rotator_homing_enabled ? "On" : "Off"}</span></span></Field>
                <Field label="Home AZ (°)" tip="Parking azimuth — where the rotator returns to. 0° = north."><Input type="number" step={0.1} className="mono" value={cfg.rotator_home_azimuth} onChange={e => set("rotator_home_azimuth", parseFloat(e.target.value) || 0)} disabled={!cfg.rotator_homing_enabled} /></Field>
                <Field label="Home EL (°)" tip="Parking elevation. 0° = horizon; 90° = straight up."><Input type="number" step={0.1} className="mono" value={cfg.rotator_home_elevation} onChange={e => set("rotator_home_elevation", parseFloat(e.target.value) || 0)} disabled={!cfg.rotator_homing_enabled} /></Field>
                <Field label="Delay (min)" tip="Idle time before the rotator returns home. Avoids parking mid-flight if a sonde briefly drops out."><Input type="number" className="mono" value={cfg.rotator_homing_delay} onChange={e => set("rotator_homing_delay", parseInt(e.target.value) || 0)} disabled={!cfg.rotator_homing_enabled} /></Field>
              </FieldGrid>
            </SubPanel>
          </Section>
        </TabsContent>

        {/* FILTER */}
        <TabsContent value="filter">
          <Section title="Position filtering" description="Discard physically impossible telemetry.">
            <FieldGrid cols={3}>
              <Field label="Max altitude (m)" tip="Reject positions above this height."><Input type="number" className="mono" value={cfg.max_altitude} onChange={e => set("max_altitude", parseInt(e.target.value) || 0)} /></Field>
              <Field label="Max range (km)" tip="Reject positions farther than this from station."><Input type="number" className="mono" value={cfg.max_radius_km} onChange={e => set("max_radius_km", parseFloat(e.target.value) || 0)} /></Field>
              <Field label="Min range (km)" tip="Reject positions closer than this to the station — useful to filter out spoofed 'zero' coordinates."><Input type="number" className="mono" value={cfg.min_radius_km} onChange={e => set("min_radius_km", parseFloat(e.target.value) || 0)} /></Field>
            </FieldGrid>
            <SubPanel title="Behaviour">
              <FieldGrid cols={2}>
                <Field label="Realtime velocity filter" tip="Drop telemetry packets where the reported velocity exceeds the cap below — guards against decoded glitches that report supersonic motion."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.enable_realtime_filter} onCheckedChange={v => set("enable_realtime_filter", v)} /><span className="text-xs">{cfg.enable_realtime_filter ? "On" : "Off"}</span></span></Field>
                <Field label="Temp-block radius violations" tip="Caution — can false-trigger on flaky DFM/RS92."><span className="flex items-center gap-2 h-7"><Switch checked={cfg.radius_temporary_block} onCheckedChange={v => set("radius_temporary_block", v)} /><span className="text-xs">{cfg.radius_temporary_block ? "On" : "Off"}</span></span></Field>
                {cfg.enable_realtime_filter && (
                  <>
                    <Field label="Max velocity (m/s)" tip="Hard cap on horizontal velocity. 200 m/s (~450 mph) covers any real sonde with margin."><Input type="number" className="mono" value={cfg.max_velocity} onChange={e => set("max_velocity", parseInt(e.target.value) || 0)} /></Field>
                    <Field label="Time desync threshold (h)" tip="Reject if sonde's clock is way off."><Input type="number" step={0.1} className="mono" value={cfg.sonde_time_threshold} onChange={e => set("sonde_time_threshold", parseFloat(e.target.value) || 0)} /></Field>
                  </>
                )}
              </FieldGrid>
            </SubPanel>
          </Section>
        </TabsContent>

        {/* ADVANCED */}
        <TabsContent value="advanced">
          <Section title="Advanced" description="Decoder tuning, debug output, web server. Most users should leave these alone.">
            <SubPanel title="Web interface">
              <FieldGrid cols={3}>
                <Field label="Host" tip="Address the web server binds to. 0.0.0.0 = all interfaces; 127.0.0.1 = local-only."><Input className="mono" value={cfg.web_host} onChange={e => set("web_host", e.target.value)} /></Field>
                <Field label="Port" tip="TCP port for the web UI. Default 5000."><Input type="number" className="mono" value={cfg.web_port} onChange={e => set("web_port", parseInt(e.target.value) || 0)} /></Field>
                <Field label="Archive retention (min)" tip="How long the live dashboard keeps a sonde in its in-memory store after the last packet. Doesn't affect the on-disk log archive (which is unlimited)."><Input type="number" className="mono" value={cfg.web_archive_age} onChange={e => set("web_archive_age", parseInt(e.target.value) || 0)} /></Field>
                <Field label="KML refresh (s)" tip="How often the /rs.kml feed updates. Lower = fresher in Google Earth, but more frequent regeneration."><Input type="number" className="mono" value={cfg.kml_refresh_rate} onChange={e => set("kml_refresh_rate", parseInt(e.target.value) || 0)} /></Field>
                <Field label="Enable web control" tip="Allow scanner/decoder/rotator control from the UI.">
                  <span className="flex items-center gap-2 h-7">
                    <Switch
                      checked={cfg.web_control}
                      onCheckedChange={v => {
                        // Warn before disabling: once off + restarted, the
                        // /save_config endpoint refuses writes, so the user
                        // can't re-enable it from this UI — they have to edit
                        // station.cfg directly. confirm() is enough here; a
                        // full AlertDialog felt heavy for a single setting.
                        if (!v && cfg.web_control) {
                          const ok = confirm(
                            "Disable web control?\n\n" +
                            "After saving this and restarting auto_rx, you will no longer be able to " +
                            "edit the config from this UI or start/stop decoders, scanner, or the rotator. " +
                            "You'll have to edit station.cfg directly to turn it back on."
                          );
                          if (!ok) return;
                        }
                        set("web_control", v);
                      }}
                    />
                    <span className="text-xs">{cfg.web_control ? "On" : "Off"}</span>
                  </span>
                </Field>
                {cfg.web_control && (
                  <p className="text-[11px] text-muted-foreground col-span-full">
                    Set <code className="mono">web_password</code> in
                    <code className="mono">station.cfg</code> under
                    <code className="mono">[web]</code> — not editable from
                    this UI for security.
                  </p>
                )}
              </FieldGrid>
            </SubPanel>

            <SubPanel title="Scanner tuning">
              <FieldGrid cols={3}>
                {([
                  ["snr_threshold", "SNR threshold (dB)", 0.1, "Minimum signal-to-noise ratio above the measured noise floor for a peak to be considered a candidate. Lower picks up weaker sondes but increases false detections."],
                  ["min_distance", "Min peak distance (Hz)", 1, "Reject peaks closer together than this — most sondes are spaced ≥ 100 kHz apart."],
                  ["max_peaks", "Max peaks per scan", 1, "Stop adding candidates once this many peaks have been found. Caps the work per scan cycle."],
                  ["search_step", "Search step (Hz)", 1, "rtl_power bin width for the spectrum sweep. Smaller = finer freq resolution but slower scan."],
                  ["scan_dwell_time", "Scan dwell (s)", 1, "How long rtl_power integrates power per scan sweep. Longer = more sensitive but slower."],
                  ["detect_dwell_time", "Detect dwell (s)", 1, "How long each candidate freq is sampled for sonde-detection. Increase if sondes occasionally slip through."],
                  ["scan_delay", "Scan delay (s)", 1, "Pause between scan attempts when no peaks are found. Reduces CPU/USB churn when the sky is quiet."],
                  ["quantization", "Quantization (Hz)", 1, "Snap detected frequencies to this grid (typical sonde channelization is 5 kHz)."],
                  ["decoder_spacing_limit", "Decoder spacing limit (Hz)", 1, "When two peaks are closer than this, only the stronger gets a decoder. Avoids dual-allocating SDRs to the same sonde."],
                  ["temporary_block_time", "Block time on encrypted (min)", 1, "How long to temporarily blocklist a frequency after detecting an encrypted (e.g. military) RS41."],
                  ["payload_id_valid", "Payload ID confirmation", 1, "Number of consecutive frames a serial must be seen to be accepted. Higher = fewer garbled IDs, slower lock-on."],
                  ["max_async_scan_workers", "Max async workers (KA9Q)", 1, "Parallel detect tasks the KA9Q scanner can spin up. Only matters when SDR backend = KA9Q."],
                ] as [string, string, number, string][]).map(([k, l, step, tip]) => (
                  <Field key={k} label={l} tip={tip}><Input type="number" step={step} className="mono" value={cfg[k]} onChange={e => set(k, parseFloat(e.target.value) || 0)} /></Field>
                ))}
              </FieldGrid>
            </SubPanel>

            <SubPanel title="Decoder algorithms">
              <p className="text-[11px] text-muted-foreground mb-2">Toggle experimental fsk_demod-based decoders. Most should stay on.</p>
              <FieldGrid cols={3}>
                {([
                  // Note: RS41 / RS92 / M10 / DFM / LMS6-400 / M20 toggles
                  // were removed — config.py no longer reads those keys
                  // (commented out as of v1.6+: the fsk_demod chain is the
                  // permanent decoder path for those types). Showing the
                  // checkboxes would be lying.
                  ["imet54_experimental", "iMet-54", "Use the fsk_demod-based iMet-54 decoder.", true],
                  ["meisei_experimental", "MEISEI", "Use the fsk_demod-based Meisei decoder.", true],
                  ["mrz_experimental", "MRZ", "Use the fsk_demod-based MRZ decoder.", false],
                  ["lms6_1680_experimental", "LMS6-1680", "Use the fsk_demod-based LMS6 1680 MHz decoder.", false],
                  ["ngp_tweak", "RS92-NGP 1680 MHz tweak", "Special handling for the NGP variant of the RS92 sonde on 1680 MHz.", false],
                  ["wideband_sondes", "Wideband sonde detection", "Allow wideband (e.g. ~16 kHz BW) sondes like the LMS6-1680 to be detected.", false],
                  ["close_on_encrypted", "Close decoder on encrypted RS41", "Free up the SDR immediately when an encrypted (military) RS41 is detected — otherwise the decoder sits idle.", true],
                  ["synchronous_upload", "Synchronous upload", "Send uploads (APRS/SondeHub) synchronously after each frame instead of batching. station.cfg.example ships this on, so most installs have it enabled.", true],
                ] as [string, string, string, boolean][]).map(([k, l, tip, dflt]) => (
                  <span key={k} className="flex items-center gap-2">
                    <Checkbox checked={!!cfg[k]} onCheckedChange={v => set(k, !!v)} />
                    <span className="text-xs">{l}</span>
                    <span className="text-[10px] mono text-muted-foreground/60">(default {dflt ? "on" : "off"})</span>
                    <Info tip={tip} />
                  </span>
                ))}
              </FieldGrid>
            </SubPanel>

            <SubPanel title="Logging & debug">
              <FieldGrid cols={2}>
                {([
                  ["per_sonde_log", "Per-sonde telemetry log", "Write a separate log file per decoded sonde, used by History page and Stats."],
                  ["save_system_log", "Save system log to disk", "Persist auto_rx's stdout log to a dated file in log/. Useful for post-mortem; disabled by default since systemd already captures stdout."],
                  ["enable_debug_logging", "Verbose debug logging", "Log everything at DEBUG level. Very chatty — only enable when troubleshooting."],
                  ["save_cal_data", "Save RS41 calibration data", "Dump the RS41 calibration subframe block to disk. Useful for reverse engineering / research."],
                ] as [string, string, string][]).map(([k, l, tip]) => (
                  <span key={k} className="flex items-center gap-2">
                    <Checkbox checked={!!cfg[k]} onCheckedChange={v => set(k, !!v)} />
                    <span className="text-xs">{l}</span>
                    <Info tip={tip} />
                  </span>
                ))}
              </FieldGrid>
              <p className="text-[11px] text-warn mt-3">⚠ The options below write large amounts of disk data — diagnostics only.</p>
              <FieldGrid cols={2} className="mt-1">
                {([
                  ["save_detection_audio", "Save detection audio", "Capture the audio buffer used during initial sonde detection. Small files (a few seconds each)."],
                  ["save_decode_audio", "Save decoded audio", "Save the audio stream during decoding. Larger files (entire flight duration)."],
                  ["save_decode_iq", "Save decoded IQ (very high I/O)", "Save raw IQ samples from the decoder. Multi-GB per flight. Only enable if you need bit-exact reproducibility."],
                  ["save_raw_hex", "Save raw hex frames", "Persist every decoded frame as raw hex. Useful for forensic decoding."],
                ] as [string, string, string][]).map(([k, l, tip]) => (
                  <span key={k} className="flex items-center gap-2">
                    <Checkbox checked={!!cfg[k]} onCheckedChange={v => set(k, !!v)} />
                    <span className="text-xs">{l}</span>
                    <Info tip={tip} />
                  </span>
                ))}
              </FieldGrid>
            </SubPanel>
          </Section>
        </TabsContent>
      </Tabs>

      {/* Save bar */}
      {dirty && (
        <div className={cn(
          "sticky bottom-3 z-30 flex items-center gap-3 rounded-md border border-border bg-card/95 backdrop-blur p-2.5 shadow-lg animate-slide-up"
        )}>
          <span className={`pip pip-warn`} aria-hidden />
          <span className="text-xs mono text-muted-foreground">
            {saving ? "Saving…" : `${dirtyKeys.length} unsaved change${dirtyKeys.length === 1 ? "" : "s"}`}
          </span>
          <div className="flex-1" />
          <Button variant="ghost" size="sm" onClick={revert} disabled={saving}><RotateCcw className="w-3 h-3" /> Discard</Button>
          <Button variant="primary" size="sm" onClick={save} disabled={saving}><Check className="w-3 h-3" /> Save</Button>
        </div>
      )}
    </div>
    <RestartRequiredDialog
      keys={restartFor}
      onClose={() => setRestartFor(null)}
    />
    </AuthGate>
  );
}

// ---- View tab: browser-side prefs (theme, marker style, units, etc.) -------
function ChoiceGroup<T extends string>({
  label, value, options, onChange,
}: {
  label: string;
  value: T;
  options: { v: T; l: string; icon?: React.ReactNode; tip?: string }[];
  onChange: (v: T) => void;
}) {
  return (
    <div>
      <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-1.5">{label}</div>
      <div className="inline-flex items-center rounded-md bg-secondary p-0.5" role="group" aria-label={label}>
        {options.map(o => (
          <button
            key={o.v}
            type="button"
            onClick={() => onChange(o.v)}
            title={o.tip}
            aria-pressed={value === o.v}
            className={cn(
              "h-7 px-3 text-xs font-medium rounded-sm transition-colors inline-flex items-center gap-1.5",
              value === o.v ? "bg-accent text-foreground" : "text-muted-foreground hover:text-foreground"
            )}
          >
            {o.icon}{o.l}
          </button>
        ))}
      </div>
    </div>
  );
}

function ViewPrefsPanel() {
  const prefs = usePrefs();
  const resetAll = () => {
    if (!confirm("Reset all in-browser UI preferences (column visibility, theme, units, follow…) and reload?")) return;
    try {
      for (let i = localStorage.length - 1; i >= 0; i--) {
        const k = localStorage.key(i);
        if (k && k.startsWith("obs.")) localStorage.removeItem(k);
      }
    } catch {}
    location.reload();
  };
  return (
    <Section title="View" description="Browser-side display preferences. Saved on this device — not synced to station.cfg.">
      <div className="flex flex-col gap-5">
        <ChoiceGroup<ThemePref>
          label="Theme"
          value={prefs.theme}
          onChange={v => setPrefs({ theme: v })}
          options={[
            { v: "system", l: "System", icon: <Monitor className="w-3 h-3" />, tip: "Follow the OS dark/light setting." },
            { v: "light",  l: "Light",  icon: <Sun     className="w-3 h-3" /> },
            { v: "dark",   l: "Dark",   icon: <Moon    className="w-3 h-3" /> },
          ]}
        />
        <ChoiceGroup<"triangle" | "balloon">
          label="Map markers"
          value={prefs.markerStyle}
          onChange={v => setPrefs({ markerStyle: v as MarkerStyle })}
          options={[
            { v: "triangle", l: "Triangle", icon: <MapPin className="w-3 h-3" />, tip: "Direction-aware arrow: points up while ascending, down while descending." },
            { v: "balloon",  l: "Balloon",  icon: <span className="text-[13px] leading-none">●</span>, tip: "Classic balloon while ascending, parachute while descending." },
          ]}
        />
        <ChoiceGroup<"utc" | "local">
          label="Time zone"
          value={prefs.utc ? "utc" : "local"}
          onChange={v => setPrefs({ utc: v === "utc" })}
          options={[
            { v: "utc",   l: "UTC",   icon: <Clock className="w-3 h-3" />, tip: "Display timestamps in UTC (used by sondes and APRS)." },
            { v: "local", l: "Local", tip: "Display timestamps in your browser's local time zone." },
          ]}
        />
        <ChoiceGroup<"24" | "12">
          label="Time format"
          value={prefs.hour12 ? "12" : "24"}
          onChange={v => setPrefs({ hour12: v === "12" })}
          options={[
            { v: "24", l: "24-hour", tip: "HH:MM:SS" },
            { v: "12", l: "12-hour", tip: "HH:MM:SS AM/PM" },
          ]}
        />
        <ChoiceGroup<"metric" | "imperial">
          label="Distance units"
          value={prefs.metric ? "metric" : "imperial"}
          onChange={v => setPrefs({ metric: v === "metric" })}
          options={[
            { v: "metric",   l: "Metric (m, km, m/s, °C)", icon: <Ruler className="w-3 h-3" /> },
            { v: "imperial", l: "Imperial (ft, mi, mph, °F)" },
          ]}
        />
        <div>
          <div className="text-[10px] uppercase tracking-wider text-muted-foreground font-semibold mb-1.5">Display</div>
          <button
            type="button"
            onClick={() => setPrefs({ showVersion: !prefs.showVersion })}
            className="inline-flex items-center gap-2 text-xs h-7 px-3 rounded-md bg-secondary hover:bg-accent transition-colors"
          >
            {prefs.showVersion ? <Eye className="w-3.5 h-3.5" /> : <EyeOff className="w-3.5 h-3.5" />}
            <span>Software version chip next to STATION</span>
            <span className="text-[10px] text-muted-foreground ml-1">{prefs.showVersion ? "On" : "Off"}</span>
          </button>
        </div>
        <div className="pt-2 border-t border-border">
          <Button variant="outline" size="sm" onClick={resetAll} className="text-destructive border-destructive/40 hover:bg-destructive/10">
            <RotateCw className="w-3 h-3" /> Reset all UI preferences
          </Button>
          <div className="text-[10px] text-muted-foreground mt-1">Clears column visibility, follow-target, theme, units, and other localStorage prefs for this site.</div>
        </div>
      </div>
    </Section>
  );
}
