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

export interface Auth {
  enabled: boolean;
  ok: (req: Request) => boolean;
  middleware: RequestHandler;
  loginCookie: (token: string) => string;
  clearCookie: () => string;
}

// Shared-token auth. When no token is configured, auth is disabled (the
// zero-config LAN default) and everything is allowed.
export function makeAuth(token?: string): Auth {
  const enabled = !!token;
  const ok = (req: Request): boolean => {
    if (!enabled) return true;
    const t = extractToken(req);
    return t != null && tokenMatches(t, token!);
  };
  const middleware: RequestHandler = (req, res, next) => {
    if (ok(req)) return next();
    res.status(401).json({ error: 'unauthorized' });
  };
  // The cookie value IS the shared token; HttpOnly keeps it out of reach of JS
  // (so an XSS can't exfiltrate it), SameSite=Strict blocks cross-site use.
  const loginCookie = (t: string) =>
    `${COOKIE}=${encodeURIComponent(t)}; HttpOnly; SameSite=Strict; Path=/; Max-Age=604800`;
  const clearCookie = () => `${COOKIE}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0`;
  return { enabled, ok, middleware, loginCookie, clearCookie };
}
