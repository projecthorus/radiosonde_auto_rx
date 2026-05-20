export interface SondeTelemetry {
  id: string;
  type: string;
  freq: number;
  frame: number;
  lat: number;
  lon: number;
  alt: number;
  vel_h?: number;
  vel_v?: number;
  heading?: number;
  temp?: number;
  humidity?: number;
  pressure?: number;
  snr?: number;
  datetime?: string;
  server_time?: number;
  /* sonde-type specifics surfaced into the "other" column */
  rs41_mainboard?: string;
  bt?: number;
  batt?: number;
  encrypted?: boolean;
  aprsid?: string;
  /* derived client-side */
  ts?: number;
  firstSeen?: number;
  color?: string;
  path?: [number, number][];
  /** Authoritative [lat, lon, alt] for the first decoded position — sourced
   *  from /get_log_by_serial on hydration. Lets the map plant a first-heard
   *  marker at the actual launch position even when state.path was clipped. */
  first_pos?: [number, number, number];
  /** Same shape for the apex / burst point. Only present when the log has
   *  recorded a burst (i.e. the sonde has clearly descended). */
  burst_pos?: [number, number, number];
  first_time?: string;
  burst_time?: string;
}

export interface SDRTask { task: string; freq: number; type?: string; }
export type TaskList = Record<string, SDRTask>;

export interface ScanData {
  freq: number[];
  power: number[];
  peak_freq: number[];
  peak_lvl: number[];
  timestamp?: string;
  threshold: number;
}

export interface LogEvent {
  level: "DEBUG" | "INFO" | "WARNING" | "ERROR" | "CRITICAL";
  ts?: string;
  msg: string;
}

export interface RotatorStatus {
  enabled: boolean;
  az: number;
  el: number;
  target_az?: number | null;
  target_el?: number | null;
  mode: string;
  target_id?: string | null;
  last_move?: string | null;
}

export interface BlockEntry {
  freq: number;
  reason: string;
  until?: number;
}

/**
 * Shape of one row in the /get_log_list response. Mirrors what
 * autorx.log_files.list_log_files() emits with quicklook=True:
 *  - freq is in MHz (not Hz)
 *  - lines is the log file line count (≈ frames received)
 *  - max_range / last_range / min_height come from the quicklook stats
 *  - first / last carry the launch and last-RX positions
 */
export interface HistoricalSonde {
  serial: string;
  type: string;
  freq: number;        // MHz
  datetime: string;
  lines?: number;
  max_range?: number;
  last_range?: number;
  min_height?: number; // altitude of the last received fix
  first?: { lat: number; lon: number; alt: number; range_km: number; bearing: number; datetime?: string };
  last?:  { lat: number; lon: number; alt: number; range_km: number; bearing: number; datetime?: string };
}
