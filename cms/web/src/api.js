// Minimal REST client for the CMS API (same origin — the app is served at /app).
// Token auth: the CMS uses an HttpOnly login cookie, so the browser sends it
// automatically; no Authorization header juggling needed here.
async function request(method, path, body) {
  const opts = { method, headers: {} }
  if (body !== undefined) {
    opts.headers['Content-Type'] = 'application/json'
    opts.body = JSON.stringify(body)
  }
  const res = await fetch(path, opts)
  const ct = res.headers.get('content-type') || ''
  const data = ct.includes('application/json') ? await res.json() : await res.text()
  if (!res.ok) throw new Error((data && data.error) || `${method} ${path} → ${res.status}`)
  return data
}

export default {
  get: (p) => request('GET', p),
  post: (p, body) => request('POST', p, body),
  put: (p, body) => request('PUT', p, body),
}
