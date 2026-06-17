/**
 * Lightweight REST client.
 * All methods return parsed JSON or throw on HTTP error.
 */
const BASE = ''  // relative — proxied by Vite in dev, served from same origin in production

// Optional device API token (#75). Stored per-browser; when set, it's sent as a
// Bearer header on every request. Empty/unset → no header, so the LAN-trusted
// default (no token configured on the device) is unaffected.
const TOKEN_KEY = 'of_api_token'
export function getApiToken() {
  try { return localStorage.getItem(TOKEN_KEY) || '' } catch { return '' }
}
export function setApiToken(token) {
  try { token ? localStorage.setItem(TOKEN_KEY, token) : localStorage.removeItem(TOKEN_KEY) } catch { /* ignore */ }
}
function authHeaders(extra = {}) {
  const t = getApiToken()
  return t ? { ...extra, Authorization: `Bearer ${t}` } : { ...extra }
}

async function request(method, path, body) {
  const opts = {
    method,
    headers: authHeaders({ 'Content-Type': 'application/json' }),
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
  selftest: () => request('GET', '/api/fs/selftest'),
  list: (path = '/') => request('GET', `/api/fs/list?path=${encodeURIComponent(path)}`),

  // Fetch a file with the auth header and trigger a browser download via a blob
  // URL. Avoids putting the token in the URL (query strings leak via logs /
  // history / Referer) and works whether or not a token is configured.
  async download(path) {
    const res = await fetch(`${BASE}/api/fs/download?path=${encodeURIComponent(path)}`, { headers: authHeaders() })
    if (!res.ok) throw new Error(`download ${path} → ${res.status}: ${await res.text()}`)
    const blob = await res.blob()
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = path.split('/').pop() || 'download'
    document.body.appendChild(a)
    a.click()
    a.remove()
    URL.revokeObjectURL(url)
  },

  async read(path) {
    const res = await fetch(`${BASE}/api/fs/download?path=${encodeURIComponent(path)}`, { headers: authHeaders() })
    if (!res.ok) throw new Error(`read ${path} → ${res.status}: ${await res.text()}`)
    return res.text()
  },

  async upload(path, content) {
    const res = await fetch(`${BASE}/api/fs?path=${encodeURIComponent(path)}`, {
      method: 'POST',
      headers: authHeaders({ 'Content-Type': 'application/octet-stream' }),
      body: content,
    })
    if (!res.ok) throw new Error(`upload ${path} → ${res.status}: ${await res.text()}`)
    return res.json()
  },

  async remove(path) {
    const res = await fetch(`${BASE}/api/fs?path=${encodeURIComponent(path)}`, { method: 'DELETE', headers: authHeaders() })
    if (!res.ok) throw new Error(`delete ${path} → ${res.status}: ${await res.text()}`)
    return res.json()
  },

  async mkdir(path) {
    const res = await fetch(`${BASE}/api/fs/mkdir?path=${encodeURIComponent(path)}`, { method: 'POST', headers: authHeaders() })
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
