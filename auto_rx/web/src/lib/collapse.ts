import { useCallback, useState } from "react";

/**
 * Persisted open/closed state for a UI section. Mirrors the OG dashboard's
 * cookie-backed `<details>` panels — close a section, refresh the page, it
 * stays closed.
 *
 * Stored under "obs.open." + key as "0" or "1". `useCollapse("map")` returns
 * `[open, toggle]`.
 */
export function useCollapse(key: string, defaultOpen = true): [boolean, () => void] {
  const storageKey = `obs.open.${key}`;
  const [open, setOpen] = useState<boolean>(() => {
    try {
      const v = localStorage.getItem(storageKey);
      if (v === "0") return false;
      if (v === "1") return true;
    } catch {}
    return defaultOpen;
  });
  const toggle = useCallback(() => {
    setOpen(prev => {
      const next = !prev;
      try { localStorage.setItem(storageKey, next ? "1" : "0"); } catch {}
      return next;
    });
  }, [storageKey]);
  return [open, toggle];
}
