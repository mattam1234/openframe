import { timingSafeEqual } from 'crypto';
import type { Request, RequestHandler } from 'express';

// Constant-time token comparison (avoids leaking the token via timing). Differing
// lengths short-circuit — the length itself isn't secret.
export function tokenMatches(provided: string, expected: string): boolean {
  const a = Buffer.from(provided);
  const b = Buffer.from(expected);
  if (a.length !== b.length) return false;
  return timingSafeEqual(a, b);
}

const COOKIE = 'of_token';

export function extractToken(req: Request): string | null {
  const auth = req.headers['authorization'];
  if (typeof auth === 'string' && auth.startsWith('Bearer ')) return auth.slice(7);
  const cookie = req.headers['cookie'];
  if (typeof cookie === 'string') {
    for (const part of cookie.split(';')) {
      const eq = part.indexOf('=');
      if (eq < 0) continue;
      if (part.slice(0, eq).trim() === COOKIE) return decodeURIComponent(part.slice(eq + 1).trim());
    }
  }
  return null;
}

export type Role = 'admin' | 'viewer';

export interface Auth {
  enabled: boolean;
  ok: (req: Request) => boolean;
  role: (req: Request) => Role | null;
  middleware: RequestHandler;
  loginCookie: (token: string) => string;
  clearCookie: () => string;
}

// Read-only HTTP methods a viewer may use; anything else needs admin.
function isReadMethod(method: string): boolean {
  return method === 'GET' || method === 'HEAD' || method === 'OPTIONS';
}

// Token auth with two roles. `adminToken` grants full access; the optional
// `viewerToken` grants read-only access (GET/HEAD only) so you can hand out a
// dashboard-only credential. When neither is configured, auth is disabled (the
// zero-config LAN default) and everything is allowed as admin.
export function makeAuth(adminToken?: string, viewerToken?: string): Auth {
  const enabled = !!adminToken || !!viewerToken;

  const role = (req: Request): Role | null => {
    if (!enabled) return 'admin';
    const t = extractToken(req);
    if (t == null) return null;
    if (adminToken && tokenMatches(t, adminToken)) return 'admin';
    if (viewerToken && tokenMatches(t, viewerToken)) return 'viewer';
    return null;
  };
  const ok = (req: Request): boolean => role(req) !== null;

  const middleware: RequestHandler = (req, res, next) => {
    const r = role(req);
    if (r == null) { res.status(401).json({ error: 'unauthorized' }); return; }
    if (r === 'viewer' && !isReadMethod(req.method)) {
      res.status(403).json({ error: 'forbidden: read-only (viewer) credential' });
      return;
    }
    (req as unknown as { role: Role }).role = r;
    next();
  };
  // The cookie value IS the shared token; HttpOnly keeps it out of reach of JS
  // (so an XSS can't exfiltrate it), SameSite=Strict blocks cross-site use.
  const loginCookie = (t: string) =>
    `${COOKIE}=${encodeURIComponent(t)}; HttpOnly; SameSite=Strict; Path=/; Max-Age=604800`;
  const clearCookie = () => `${COOKIE}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0`;
  return { enabled, ok, role, middleware, loginCookie, clearCookie };
}
