// The tablet only ever talks to the Go service.

export async function fetchShots(limit = 100) {
  const r = await fetch(`/api/v1/shots?limit=${limit}`);
  if (!r.ok) throw new Error(`shots: ${r.status}`);
  return r.json();
}

const sampleCache = new Map(); // shot id -> [{t, w}]; shots are immutable

export async function fetchCurve(id) {
  if (sampleCache.has(id)) return sampleCache.get(id);
  const r = await fetch(`/api/v1/shots/${id}`);
  if (!r.ok) throw new Error(`shot ${id}: ${r.status}`);
  const { samples } = await r.json();
  const curve = samples.map((s) => ({ t: s.t_ms / 1000, w: s.weight_mg / 1000 }));
  sampleCache.set(id, curve);
  return curve;
}

async function send(method, path, body) {
  const r = await fetch(path, {
    method,
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  if (!r.ok) throw new Error(`${method} ${path}: ${r.status} ${await r.text()}`);
  return r.status === 204 ? null : r.json();
}

export const patchShot = (id, fields) => send("PATCH", `/api/v1/shots/${id}`, fields);
export const deleteShot = (id) => send("DELETE", `/api/v1/shots/${id}`);
export const postTasting = (id, fields) => send("POST", `/api/v1/shots/${id}/tasting`, fields);
export const postBean = (fields) => send("POST", "/api/v1/beans", fields);
export const patchBean = (id, fields) => send("PATCH", `/api/v1/beans/${id}`, fields);
export const loadBean = (id) => send("POST", `/api/v1/beans/${id}/load`);

export async function fetchBeans() {
  const r = await fetch("/api/v1/beans");
  if (!r.ok) throw new Error(`beans: ${r.status}`);
  return r.json();
}

export async function health() {
  try {
    const r = await fetch("/healthz");
    return r.ok;
  } catch {
    return false;
  }
}
