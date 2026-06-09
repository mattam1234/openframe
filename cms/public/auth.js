'use strict';

// Shown on every page before the page script. If the CMS requires a token and we
// don't have a valid session, overlay a login box; on success the cookie is set
// and we reload so the page's own API/WS calls succeed.
(async () => {
  let state;
  try {
    state = await (await fetch('/api/auth')).json();
  } catch {
    return; // can't reach the API — let the page surface its own error
  }
  if (!state.authRequired || state.authed) return;

  const overlay = document.createElement('div');
  overlay.className = 'login-overlay';
  overlay.innerHTML =
    '<form class="login-box">' +
    '<h2>OpenFrame CMS</h2>' +
    '<input type="password" autocomplete="current-password" placeholder="Access token" />' +
    '<button type="submit">Sign in</button>' +
    '<p class="login-err hidden">Invalid token</p>' +
    '</form>';
  document.body.appendChild(overlay);

  const form = overlay.querySelector('form');
  const input = overlay.querySelector('input');
  const err = overlay.querySelector('.login-err');
  input.focus();

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    err.classList.add('hidden');
    try {
      const res = await fetch('/api/login', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ token: input.value }),
      });
      if (res.ok) location.reload();
      else { err.classList.remove('hidden'); input.select(); }
    } catch {
      err.classList.remove('hidden');
    }
  });
})();
