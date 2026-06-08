/**
 * Lightweight REST client.
 * All methods return parsed JSON or throw on HTTP error.
 */
const BASE = ''  // relative — proxied by Vite in dev, served from same origin in production

async function request(method, path, body) {
  const opts = {
    method,
    headers: { 'Content-Type': 'application/json' },
  }
  if (body !== undefined) opts.body = JSON.stringify(body)

  const res = await fetch(BASE + path, opts)
  if (!res.ok) {
    const text = await res.text()
    throw new Error(`${method} ${path} → ${res.status}: ${text}`)
  }
  const ct = res.headers.get('content-type') || ''
  if (ct.includes('application/json')) return res.json()
  return res.text()
}

/**
 * Filesystem-browser helpers. These talk to the device's /api/fs/* endpoints,
 * which exchange raw file bytes (not the JSON envelope used elsewhere).
 */
const fs = {
  stat: () => request('GET', '/api/fs/stat'),
  list: (path = '/') => request('GET', `/api/fs/list?path=${encodeURIComponent(path)}`),

  // A plain URL the browser can hit directly to download/inspect a file.
  downloadUrl: (path) => `${BASE}/api/fs/download?path=${encodeURIComponent(path)}`,

  async read(path) {
    const res = await fetch(`${BASE}/api/fs/download?path=${encodeURIComponent(path)}`)
    if (!res.ok) throw new Error(`read ${path} → ${res.status}: ${await res.text()}`)
    return res.text()
  },

  async upload(path, content) {
    const res = await fetch(`${BASE}/api/fs?path=${encodeURIComponent(path)}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: content,
    })
    if (!res.ok) throw new Error(`upload ${path} → ${res.status}: ${await res.text()}`)
    return res.json()
  },

  async remove(path) {
    const res = await fetch(`${BASE}/api/fs?path=${encodeURIComponent(path)}`, { method: 'DELETE' })
    if (!res.ok) throw new Error(`delete ${path} → ${res.status}: ${await res.text()}`)
    return res.json()
  },

  async mkdir(path) {
    const res = await fetch(`${BASE}/api/fs/mkdir?path=${encodeURIComponent(path)}`, { method: 'POST' })
    if (!res.ok) throw new Error(`mkdir ${path} → ${res.status}: ${await res.text()}`)
    return res.json()
  },

  rename: (from, to) => request('POST', '/api/fs/rename', { from, to }),
}

export default {
  get:    (path)         => request('GET',    path),
  post:   (path, body)   => request('POST',   path, body),
  put:    (path, body)   => request('PUT',    path, body),
  delete: (path)         => request('DELETE', path),
  patch:  (path, body)   => request('PATCH',  path, body),
  fs,
}
