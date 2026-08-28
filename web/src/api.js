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

export async function health() {
  try {
    const r = await fetch("/healthz");
    return r.ok;
  } catch {
    return false;
  }
}
