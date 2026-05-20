# Web UI

React SPA in `auto_rx/web/`. Builds into `auto_rx/autorx/static/build/`,
served by the existing Flask app in `autorx/web.py` at the auto_rx web
port (5000 by default).

Stack: React 19 + TypeScript, Vite 8, Tailwind 3, Radix primitives
(copied locally into `src/components/ui/`), react-router-dom v7,
socket.io-client, Leaflet (map), uPlot (charts).

Routes (`src/App.tsx`): `/` Dashboard, `/historical` History,
`/stats` and `/status` Stats, `/config` Config.

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

- `GET /get_config` — returns the in-memory station config.
- `POST /save_config` — JSON body with edited fields plus `__password`.
  Persists to `station.cfg` via `autorx/config_writer.py`, preserving
  comments and section ordering, appending missing keys/sections, atomic
  rename. Returns `{ok, written, unknown, restart_required}`.

Mutating endpoints require `password` (form) or `__password` (JSON) to
match `web_password` in memory. The UI keeps the password in
`sessionStorage` via `lib/auth.ts`.

### Adding a config field

1. Add to `KEYS` in `autorx/config_writer.py`:
   `(section, file_key, type, restart_required)`.
2. Add to `DEFAULTS` and the right `TAB_KEYS[...]` in `src/pages/Config.tsx`.
3. Render with `<Field>` wrapping `<Input>` / `<Switch>` / `<Select>`.
4. Add a check in `validate()` if it has constraints.

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
