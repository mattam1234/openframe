'use strict';

const msg = document.getElementById('msg');
const qrWrap = document.getElementById('qr-wrap');
const urlEl = document.getElementById('url');

function val(id) { return document.getElementById(id).value.trim(); }

document.getElementById('generate').onclick = async () => {
  msg.textContent = 'generating…';
  qrWrap.innerHTML = '';
  urlEl.textContent = '—';
  try {
    const res = await fetch('/api/provision', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ssid: val('ssid'), password: document.getElementById('password').value,
        mqttHost: val('mqttHost'), mqttPort: val('mqttPort'),
        baseTopic: val('baseTopic'), apHost: val('apHost'),
      }),
    });
    const body = await res.json();
    if (!res.ok) { msg.textContent = `error: ${body.error || res.status}`; return; }
    msg.textContent = '';
    const img = document.createElement('img');
    img.alt = 'provisioning QR';
    img.src = body.qr; // trusted data: URL from our own API
    qrWrap.appendChild(img);
    urlEl.textContent = body.url;
  } catch (e) {
    msg.textContent = `error: ${e.message}`;
  }
};
