/**
 * Auth store for the web_control password. Shared between the Controls dialog
 * and the Settings page so the user only authenticates once per session.
 *
 * Persisted in sessionStorage so reloads and in-tab navigation keep you logged
 * in; closing the tab clears it. The web_control password is already sent in
 * the clear over HTTP on every protected POST, so storing it for the session
 * doesn't widen the threat surface.
 */
import { useSyncExternalStore } from "react";
import { apiPostForm } from "@/lib/api";

type State = { password: string | null; verified: boolean; verifying: boolean };

const KEY = "autorx.auth.pw";

function load(): State {
  try {
    const pw = sessionStorage.getItem(KEY);
    if (pw) return { password: pw, verified: true, verifying: false };
  } catch {}
  return { password: null, verified: false, verifying: false };
}

let state: State = load();
const listeners = new Set<() => void>();

function notify() { listeners.forEach(l => l()); }
function set(p: Partial<State>) {
  state = { ...state, ...p };
  try {
    if (state.verified && state.password) sessionStorage.setItem(KEY, state.password);
    else sessionStorage.removeItem(KEY);
  } catch {}
  notify();
}

export const auth = {
  async verify(password: string): Promise<boolean> {
    set({ verifying: true });
    try {
      const r = await apiPostForm("/check_password", { password });
      if (r.trim() === "OK") { set({ password, verified: true, verifying: false }); return true; }
    } catch {}
    set({ password: null, verified: false, verifying: false });
    return false;
  },
  logout() { set({ password: null, verified: false }); },
  password() { return state.password; },
};

export function useAuth() {
  return useSyncExternalStore(
    l => { listeners.add(l); return () => { listeners.delete(l); }; },
    () => state,
    () => state
  );
}
