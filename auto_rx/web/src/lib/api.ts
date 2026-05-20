export async function apiGet<T = any>(url: string): Promise<T> {
  const r = await fetch(url, { credentials: "same-origin" });
  if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
  const ct = r.headers.get("content-type") || "";
  return ct.includes("json") ? r.json() : (r.text() as any);
}

export async function apiPostJSON<T = any>(url: string, body: any): Promise<T> {
  const r = await fetch(url, {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const ct = r.headers.get("content-type") || "";
  const data = ct.includes("json") ? await r.json() : await r.text();
  if (!r.ok) throw new Error(typeof data === "string" ? data : data?.error || `${r.status}`);
  return data as T;
}

export async function apiPostForm(url: string, fields: Record<string, any>): Promise<string> {
  const fd = new FormData();
  for (const k of Object.keys(fields)) fd.append(k, String(fields[k]));
  const r = await fetch(url, { method: "POST", credentials: "same-origin", body: fd });
  const text = await r.text();
  if (!r.ok) throw new Error(text || `${r.status}`);
  return text;
}
