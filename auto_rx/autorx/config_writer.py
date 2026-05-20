"""
config_writer.py — Safely persist UI-edited settings back to station.cfg
without losing comments, ordering, or unrelated formatting.

Strategy: read the file line-by-line, track the current [section], and when
we hit a `key = value` line whose (section, key) matches one of the updates,
replace just the value portion of that line. Everything else is copied
verbatim. Result is written to a tmp file in the same directory and atomically
renamed over the original.

Only known keys are written — anything we don't have a mapping for is reported
back to the caller in `unknown`. Restart-required keys are flagged separately
so the UI can decide whether to prompt for a restart.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import tempfile
from typing import Any

# ---------------------------------------------------------------------------
# Mapping: ui_key -> (section, file_key, type, restart_required)
# Where `type` is one of: "str", "int", "float", "bool", "json"
# Derived from autorx/config.py's read_auto_rx_config().
# ---------------------------------------------------------------------------
KeyMap = dict[str, tuple[str, str, str, bool]]
KEYS: KeyMap = {
    # Location
    "station_lat":                ("location", "station_lat", "float", True),
    "station_lon":                ("location", "station_lon", "float", True),
    "station_alt":                ("location", "station_alt", "float", True),
    "gpsd_enabled":               ("location", "gpsd_enabled", "bool", True),
    "gpsd_host":                  ("location", "gpsd_host", "str", True),
    "gpsd_port":                  ("location", "gpsd_port", "int", True),

    # SDR (top-level)
    "sdr_type":                   ("sdr", "sdr_type", "str", True),
    "sdr_quantity":               ("sdr", "sdr_quantity", "int", True),
    "sdr_hostname":               ("sdr", "sdr_hostname", "str", True),
    "sdr_port":                   ("sdr", "sdr_port", "int", True),
    # Command paths (sdr_fm_path / sdr_power_path / ss_iq_path / ss_power_path)
    # are intentionally not writable from the web — they're exec'd on every
    # scan, so a web-editable path is a direct remote command injection vector
    # if the operator's web_password is weak. Edit station.cfg directly.

    # Search parameters / frequencies
    "min_freq":                   ("search_params", "min_freq", "float", True),
    "max_freq":                   ("search_params", "max_freq", "float", True),
    "rx_timeout":                 ("search_params", "rx_timeout", "int", True),
    "only_scan":                  ("search_params", "only_scan", "json", True),
    "never_scan":                 ("search_params", "never_scan", "json", True),
    "always_scan":                ("search_params", "always_scan", "json", True),
    "always_decode":              ("search_params", "always_decode", "json", True),

    # Habitat / Sondehub uploader identity
    "habitat_uploader_callsign":       ("habitat", "uploader_callsign", "str", True),
    "habitat_uploader_antenna":        ("habitat", "uploader_antenna", "str", True),
    "habitat_upload_listener_position":("habitat", "upload_listener_position", "bool", True),

    # Sondehub
    "sondehub_enabled":           ("sondehub", "sondehub_enabled", "bool", True),
    "sondehub_upload_rate":       ("sondehub", "sondehub_upload_rate", "int", True),
    # sondehub_contact_email is intentionally not writable from the web —
    # PII; edit station.cfg directly.

    # APRS
    "aprs_enabled":               ("aprs", "aprs_enabled", "bool", True),
    "aprs_upload_rate":           ("aprs", "upload_rate", "int", True),
    "aprs_user":                  ("aprs", "aprs_user", "str", True),
    "aprs_pass":                  ("aprs", "aprs_pass", "str", True),
    "aprs_server":                ("aprs", "aprs_server", "str", True),
    "aprs_port":                  ("aprs", "aprs_port", "int", True),
    "aprs_object_id":             ("aprs", "aprs_object_id", "str", True),
    "aprs_custom_comment":        ("aprs", "aprs_custom_comment", "str", True),
    "aprs_use_custom_object_id":  ("aprs", "aprs_use_custom_object_id", "bool", True),
    "aprs_position_report":       ("aprs", "aprs_position_report", "bool", True),
    "station_beacon_enabled":     ("aprs", "station_beacon_enabled", "bool", True),
    "station_beacon_rate":        ("aprs", "station_beacon_rate", "int", True),
    "station_beacon_comment":     ("aprs", "station_beacon_comment", "str", True),
    "station_beacon_icon":        ("aprs", "station_beacon_icon", "str", True),

    # OziPlotter / Chasemapper
    "ozi_enabled":                ("oziplotter", "ozi_enabled", "bool", True),
    "ozi_host":                   ("oziplotter", "ozi_host", "str", True),
    "ozi_port":                   ("oziplotter", "ozi_port", "int", True),
    "ozi_update_rate":            ("oziplotter", "ozi_update_rate", "int", True),
    "payload_summary_enabled":    ("oziplotter", "payload_summary_enabled", "bool", True),
    "payload_summary_host":       ("oziplotter", "payload_summary_host", "str", True),
    "payload_summary_port":       ("oziplotter", "payload_summary_port", "int", True),

    # Email
    "email_enabled":              ("email", "email_enabled", "bool", True),
    # SMTP server/port/auth/login/password and from/to are intentionally
    # not writable from the web — credentials + PII; edit station.cfg.
    "email_subject":              ("email", "subject", "str", True),
    "email_nearby_landing_subject": ("email", "nearby_landing_subject", "str", True),
    "email_error_notifications":  ("email", "error_notifications", "bool", True),
    "email_launch_notifications": ("email", "launch_notifications", "bool", True),
    "email_landing_notifications":("email", "landing_notifications", "bool", True),
    "email_landing_range_threshold":   ("email", "landing_range_threshold", "float", True),
    "email_landing_altitude_threshold":("email", "landing_altitude_threshold", "float", True),

    # Rotator
    "rotator_enabled":            ("rotator", "rotator_enabled", "bool", True),
    "rotator_update_rate":        ("rotator", "update_rate", "int", True),
    "rotator_hostname":           ("rotator", "rotator_hostname", "str", True),
    "rotator_port":               ("rotator", "rotator_port", "int", True),
    "rotator_homing_enabled":     ("rotator", "rotator_homing_enabled", "bool", True),
    "rotator_home_azimuth":       ("rotator", "rotator_home_azimuth", "float", True),
    "rotator_home_elevation":     ("rotator", "rotator_home_elevation", "float", True),
    "rotator_homing_delay":       ("rotator", "rotator_homing_delay", "int", True),
    "rotation_threshold":         ("rotator", "rotation_threshold", "float", True),
    "rotator_azimuth_only":       ("rotator", "azimuth_only", "bool", True),

    # Filtering
    "max_altitude":               ("filtering", "max_altitude", "int", True),
    "max_radius_km":              ("filtering", "max_radius_km", "int", True),
    "min_radius_km":              ("filtering", "min_radius_km", "int", True),
    "radius_temporary_block":     ("filtering", "radius_temporary_block", "bool", True),
    "enable_realtime_filter":     ("filtering", "enable_realtime_filter", "bool", True),
    "max_velocity":               ("filtering", "max_velocity", "int", True),
    "sonde_time_threshold":       ("filtering", "sonde_time_threshold", "float", True),

    # Web
    "web_host":                   ("web", "web_host", "str", True),
    "web_port":                   ("web", "web_port", "int", True),
    "web_archive_age":            ("web", "archive_age", "int", True),
    "web_control":                ("web", "web_control", "bool", True),
    # web_password is intentionally not writable from the web — credential;
    # edit station.cfg directly. (A weak password + writable rtl_fm path
    # would otherwise be a remote command injection vector.)
    "kml_refresh_rate":           ("web", "kml_refresh_rate", "int", True),

    # Debugging / logging
    "save_detection_audio":       ("debugging", "save_detection_audio", "bool", True),
    "save_decode_audio":          ("debugging", "save_decode_audio", "bool", True),
    "save_decode_iq":             ("debugging", "save_decode_iq", "bool", True),
    "save_raw_hex":               ("debugging", "save_raw_hex", "bool", True),
    "save_system_log":            ("logging", "save_system_log", "bool", True),
    "enable_debug_logging":       ("logging", "enable_debug_logging", "bool", True),
    "save_cal_data":              ("logging", "save_cal_data", "bool", True),
    "per_sonde_log":              ("logging", "per_sonde_log", "bool", True),

    # Advanced (selected)
    "search_step":                ("advanced", "search_step", "float", True),
    "snr_threshold":              ("advanced", "snr_threshold", "float", True),
    "min_distance":               ("advanced", "min_distance", "float", True),
    "dwell_time":                 ("advanced", "dwell_time", "int", True),
    "quantization":               ("advanced", "quantization", "int", True),
    "max_peaks":                  ("advanced", "max_peaks", "int", True),
    "scan_dwell_time":            ("advanced", "scan_dwell_time", "int", True),
    "detect_dwell_time":          ("advanced", "detect_dwell_time", "int", True),
    "scan_delay":                 ("advanced", "scan_delay", "int", True),
    "payload_id_valid":           ("advanced", "payload_id_valid", "int", True),
    "synchronous_upload":         ("advanced", "synchronous_upload", "bool", True),
    "max_async_scan_workers":     ("advanced", "max_async_scan_workers", "int", True),
    "temporary_block_time":       ("advanced", "temporary_block_time", "int", True),
    "decoder_spacing_limit":      ("advanced", "decoder_spacing_limit", "int", True),
    "ngp_tweak":                  ("advanced", "ngp_tweak", "bool", True),
    "wideband_sondes":            ("advanced", "wideband_sondes", "bool", True),
    "close_on_encrypted":         ("advanced", "close_on_encrypted", "bool", True),
}

# Per-SDR sub-fields live in [sdr_1], [sdr_2], … sections.
# UI sends cfg.sdrs = [{device_idx, ppm, gain, bias}, …] (0-indexed array → 1-indexed section)
SDR_SUBKEYS = ("device_idx", "ppm", "gain", "bias")


# ---------------------------------------------------------------------------
# Serialisation
# ---------------------------------------------------------------------------
def _serialize(value: Any, typ: str) -> str:
    if typ == "bool":
        return "True" if bool(value) else "False"
    if typ == "int":
        return str(int(value))
    if typ == "float":
        # Avoid scientific notation for typical lat/lon precision
        f = float(value)
        # Strip trailing zeros but keep at least one decimal if it looks float
        s = ("%.6f" % f).rstrip("0").rstrip(".")
        return s if s else "0"
    if typ == "json":
        return json.dumps(value)
    return "" if value is None else str(value)


# ---------------------------------------------------------------------------
# File rewriter
# ---------------------------------------------------------------------------
_SECTION_RE = re.compile(r"^\s*\[([^\]]+)\]\s*$")
_KV_RE = re.compile(r"^(\s*)([A-Za-z_][A-Za-z0-9_-]*)(\s*=\s*)(.*?)(\s*)$")


def _build_targets(updates: dict[str, Any]) -> tuple[dict[tuple[str, str], tuple[str, str]], list[str], list[str]]:
    """
    Returns:
      targets: {(section, file_key): (serialized_value, ui_key)} — what to write
      unknown: ui keys we have no mapping for
      restart_required: ui keys whose changes require a restart (currently all known keys)
    """
    targets: dict[tuple[str, str], tuple[str, str]] = {}
    unknown: list[str] = []
    restart_required: list[str] = []

    for ui_key, value in updates.items():
        if ui_key == "sdrs":
            # list of per-radio dicts; map to [sdr_N] sections
            if not isinstance(value, list):
                unknown.append(ui_key)
                continue
            for i, sdr in enumerate(value):
                if not isinstance(sdr, dict):
                    continue
                section = "sdr_%d" % (i + 1)
                for sk in SDR_SUBKEYS:
                    if sk in sdr:
                        typ = "bool" if sk == "bias" else ("float" if sk in ("ppm", "gain") else "str")
                        targets[(section, sk)] = (_serialize(sdr[sk], typ), f"sdrs[{i}].{sk}")
                        restart_required.append(f"sdrs[{i}].{sk}")
            continue

        m = KEYS.get(ui_key)
        if not m:
            unknown.append(ui_key)
            continue
        section, file_key, typ, restart = m
        targets[(section, file_key)] = (_serialize(value, typ), ui_key)
        if restart:
            restart_required.append(ui_key)

    return targets, unknown, restart_required


def save_config_file(path: str, updates: dict[str, Any]) -> dict:
    """
    Update station.cfg in place, preserving comments and structure.

    Returns: {ok, written: [ui_keys], unknown: [...], restart_required: [...]}
    """
    targets, unknown, restart_required = _build_targets(updates)
    if not os.path.exists(path):
        return {"ok": False, "errors": [f"Config file not found: {path}"]}

    written: list[str] = []
    matched_keys: set[tuple[str, str]] = set()

    with open(path, "r", encoding="utf-8") as fh:
        lines = fh.readlines()

    current_section: str | None = None
    out: list[str] = []

    for line in lines:
        # Track section
        sec_m = _SECTION_RE.match(line)
        if sec_m:
            current_section = sec_m.group(1).strip()
            out.append(line)
            continue

        # Try to update a key=value line in the current section
        if current_section is not None:
            kv_m = _KV_RE.match(line.rstrip("\n"))
            if kv_m:
                lead, key, eq, _old, trail = kv_m.groups()
                target = targets.get((current_section, key))
                if target is not None:
                    new_val, ui_key = target
                    matched_keys.add((current_section, key))
                    written.append(ui_key)
                    # Preserve trailing newline if original had one
                    nl = "\n" if line.endswith("\n") else ""
                    out.append(f"{lead}{key}{eq}{new_val}{trail}{nl}")
                    continue
        out.append(line)

    # Append missing keys: when an older station.cfg pre-dates a setting,
    # the line-rewriter above has no line to replace. Insert each missing
    # key at the end of its existing section, or create the section at EOF.
    missing: list[tuple[str, str]] = [
        (section, key)
        for (section, key) in targets.keys()
        if (section, key) not in matched_keys
    ]
    for section, key in missing:
        new_val, ui_key = targets[(section, key)]
        new_line = f"{key} = {new_val}\n"
        # Find last line index belonging to this section in `out`
        cur: str | None = None
        last_idx = -1
        for i, ln in enumerate(out):
            sm = _SECTION_RE.match(ln)
            if sm:
                cur = sm.group(1).strip()
                continue
            if cur == section:
                last_idx = i
        if last_idx >= 0:
            # Insert after the last non-blank line of the section so trailing
            # blank separators between sections stay intact.
            insert_at = last_idx + 1
            while insert_at - 1 > 0 and out[insert_at - 1].strip() == "":
                insert_at -= 1
            out.insert(insert_at, new_line)
        else:
            # Section absent entirely — append a fresh block at EOF
            if out and not out[-1].endswith("\n"):
                out[-1] = out[-1] + "\n"
            if out and out[-1].strip() != "":
                out.append("\n")
            out.append(f"[{section}]\n")
            out.append(new_line)
        written.append(ui_key)

    # Atomic write
    dir_ = os.path.dirname(os.path.abspath(path)) or "."
    fd, tmp_path = tempfile.mkstemp(prefix=".station.cfg.", dir=dir_)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.writelines(out)
        # Preserve original file mode (mkstemp creates 0600 by default)
        try:
            shutil.copymode(path, tmp_path)
        except OSError:
            pass
        os.replace(tmp_path, path)
    except Exception as e:
        try: os.unlink(tmp_path)
        except OSError: pass
        return {"ok": False, "errors": [f"Failed to write config: {e}"]}

    return {
        "ok": True,
        "written": written,
        "unknown": unknown,
        "restart_required": sorted(set(restart_required) & set(written + ["sdrs"])),
    }
