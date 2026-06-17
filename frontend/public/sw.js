// Minimal service worker for PWA installability (#50).
//
// It intentionally does NOT cache app assets. The UI is served from the device's
// LittleFS and re-flashed independently; a stale cached JS chunk silently breaks
// lazy-loaded routes (clicks "do nothing"). Network passthrough keeps the app
// installable without that staleness risk — offline support would need cache
// versioning tied to the build hash, a deliberate follow-up.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', () => {
  // No respondWith() → the browser performs its normal network fetch.
});
