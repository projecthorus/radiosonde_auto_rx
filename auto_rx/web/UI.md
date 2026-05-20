# Web UI

React SPA in `auto_rx/web/`. Builds into `auto_rx/autorx/static/build/`,
served by the existing Flask app in `autorx/web.py` at the auto_rx web
port (5000 by default).

## For maintainers who don't know TypeScript

If you don't edit anything inside `auto_rx/web/src/`, you don't need
Node, npm, or any JS knowledge. The built bundle is committed to git,
your Python workflow is unchanged, and `git pull` ships any UI updates
to users automatically.

A few things worth knowing:

- **Telling UI bugs from backend bugs.** If a value looks wrong on the
  page, curl the underlying endpoint (`/get_config`, `/get_task_list`,
  `/get_telemetry_archive`, etc.). If the JSON is right, the bug is in
  the React layer — file it against the UI maintainer. If the JSON is
  wrong, it's a normal backend bug.
- **After pulling a UI change, users need a hard refresh** once
  (Ctrl+Shift+R / Cmd+Shift+R). Asset filenames are stable, so
  browsers will otherwise serve a stale cached `index.js`.
- **The Settings page is opt-in.** It stays hidden unless the operator
  sets `web_config_enabled = True` in `[web]` of station.cfg. Default
  installs behave read-only.

The rest of this document is for people editing the React tree.

Stack: React 19 + TypeScript, Vite 8, Tailwind 3, Radix primitives
(copied locally into `src/components/ui/`), react-router-dom v7,
socket.io-client, Leaflet (map), uPlot (charts).

Routes (`src/App.tsx`): `/` Dashboard, `/historical` History,
`/stats` and `/status` Stats, `/config` Config (opt-in; hidden by default
— see Backend contract).

## Building

The built bundle in `auto_rx/autorx/static/build/` is committed. End
users / testers don't need Node — just `git pull` (or check out the
branch) and restart auto_rx.

For UI development:

```bash
cd auto_rx/web
npm install        # first time only
npm run build      # outputs to ../autorx/static/build/
```

No dev server, no mock backend. auto_rx is the only source of telemetry
and scan data, and it needs an SDR. To iterate: `npm run build`, then
**hard-refresh** the browser (Ctrl+Shift+R / Cmd+Shift+R) at
`http://<host>:5000`. Flask re-reads `index.html` per request, so no
auto_rx restart is needed for static-only changes.

If you don't have an SDR locally, build locally and deploy to a real
station — see below.

Scripts: `npm run build`, `npm run lint`. For a quick typecheck without
a build: `npx tsc --noEmit`.

## Backend contract

The UI calls existing Flask endpoints in `autorx/web.py` (grep
`@app.route`). Two that matter most:

- `GET /get_config` — returns the in-memory station config. The backend
  excises 9 sensitive fields from the response before sending: the web
  password, the SondeHub contact email, and all email credentials/PII
  (`email_smtp_*`, `email_from`, `email_to`). The UI never sees them.
- `POST /save_config` — JSON body with edited fields plus `__password`.
  Persists to `station.cfg` via `autorx/config_writer.py`, preserving
  comments and section ordering, appending missing keys/sections, atomic
  rename. Returns `{ok, written, unknown, restart_required}`.

`/save_config` is gated by an opt-in flag `web_config_enabled` (default
`False` in `[web]`). When the flag is off: the Settings nav link is
hidden, the `/config` page refuses to render, and `/save_config` returns
403. Mutating endpoints additionally require `password` (form) or
`__password` (JSON) to match `web_password` in memory. The UI keeps the
password in `sessionStorage` via `lib/auth.ts`.

`config_writer.py`'s `KEYS` map is an allowlist. Credentials, PII, and
the four command paths (`sdr_fm_path`, `sdr_power_path`, `ss_iq_path`,
`ss_power_path`) are intentionally absent — they can only be edited by
hand in `station.cfg`. This is a security boundary: a writable command
path is a remote-code-execution vector if web_password is weak.

### Adding a config field

1. Add to `KEYS` in `autorx/config_writer.py`:
   `(section, file_key, type, restart_required)`.
2. Add to `DEFAULTS` and the right `TAB_KEYS[...]` in `src/pages/Config.tsx`.
3. Render with `<Field>` wrapping `<Input>` / `<Switch>` / `<Select>`.
4. Add a check in `validate()` if it has constraints.

**Do not** add a field to `KEYS` if it's a credential, email address, or
a path/command that gets exec'd. Those stay station.cfg-only — see the
security boundary note above. If `/get_config` also shouldn't surface
the value (e.g. PII), add a matching `global_config.pop("<key>")` in
`autorx/config.py` next to the existing redactions.

## Deploy

Tarball + atomic directory swap. No auto_rx restart needed for
static-only changes.

```bash
cd auto_rx/web && npm run build && cd ../..
tar czf /tmp/b.tar.gz -C auto_rx/autorx/static build/
scp /tmp/b.tar.gz user@host:/tmp/
ssh user@host '
  set -e
  cd ~/radiosonde_auto_rx/auto_rx/autorx/static
  rm -rf build.NEW build.OLD && mkdir build.NEW
  tar xzf /tmp/b.tar.gz -C build.NEW --strip-components=1
  [ -d build ] && mv build build.OLD
  mv build.NEW build
  rm -rf build.OLD /tmp/b.tar.gz
'
```

When you also changed Python files, scp them too and restart the
service (the SDR drops its current lock on restart, so batch Python
changes):

```bash
scp auto_rx/autorx/<file>.py user@host:/tmp/
ssh user@host '
  cp /tmp/<file>.py ~/radiosonde_auto_rx/auto_rx/autorx/<file>.py
  rm /tmp/<file>.py
  sudo systemctl restart auto_rx.service
'
```

## Gotchas

- Telemetry frequencies arrive as Hz on the socket but MHz from REST.
  Normalise in the component.
- `/get_scan_data` returns slightly different shapes when the scanner
  is idle vs running. See the guards in `ScanChart.tsx`.
- Leaflet plugins must be loaded before the map mounts. Await
  `leafletPlugins.ts` in `useEffect` before instantiating.
- Output filenames are fixed (no content hash) so commits stay tidy.
  Trade-off: browsers may serve a stale cached `index.js` after an
  update. Tell users to hard-refresh once after pulling.
