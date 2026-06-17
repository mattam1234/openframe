# CMS Deployment & Security Reference

The CMS ships LAN-trusted (anonymous MQTT, no dashboard auth). This is a concrete
reference for exposing it more widely: TLS via a reverse proxy, dashboard auth, and
MQTT broker authentication. (#81)

---

## 1. Dashboard auth

Set a shared token; the API + dashboard then require it. Add a read-only viewer token
for dashboards that shouldn't change anything:

```sh
CMS_AUTH_TOKEN=$(openssl rand -hex 24)      # full admin
CMS_VIEWER_TOKEN=$(openssl rand -hex 24)    # read-only (GET/HEAD only)
```

Auth is sent as `Authorization: Bearer <token>` or an HttpOnly login cookie
(`POST /api/login`). Failed logins are rate-limited and lock out after repeated
failures. `GET /api/health`, `/metrics`, and `/firmware/:name` stay public (devices
and scrapers have no token).

## 2. HTTPS via a reverse proxy

Terminate TLS at a proxy in front of the CMS (which listens plain on `:8080`).

### Caddy (auto HTTPS)

```caddyfile
cms.example.com {
    reverse_proxy 127.0.0.1:8080
}
```

### nginx

```nginx
server {
    listen 443 ssl;
    server_name cms.example.com;
    ssl_certificate     /etc/letsencrypt/live/cms.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/cms.example.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        # WebSocket upgrade for the live dashboard stream:
        proxy_set_header Upgrade    $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host       $host;
    }
}
```

Bind the CMS to localhost (or a private interface) so it's only reachable through the
proxy, and set `CMS_PUBLIC_URL=https://cms.example.com` (also required for OTA so the
firmware download URL isn't derived from a spoofable `Host` header).

## 3. MQTT broker authentication (mosquitto)

Anonymous MQTT is fine on a trusted LAN; require credentials for wider exposure.

```conf
# mosquitto.conf
listener 8883
allow_anonymous false
password_file /mosquitto/config/passwd

# TLS
cafile   /mosquitto/certs/ca.crt
certfile /mosquitto/certs/server.crt
keyfile  /mosquitto/certs/server.key
```

```sh
mosquitto_passwd -c /mosquitto/config/passwd openframe   # then enter a password
```

Point both the CMS and the devices at the authenticated broker:

```sh
# CMS
CMS_MQTT_URL=mqtts://broker.example.com:8883
CMS_MQTT_USERNAME=openframe
CMS_MQTT_PASSWORD=…
```

On devices, set the MQTT host/port/user/password in **Settings → MQTT** and enable TLS
(upload the broker CA, or use the insecure toggle only for self-signed test brokers —
see firmware item #28).

## 4. Checklist for public exposure

- [ ] `CMS_AUTH_TOKEN` set (and `CMS_VIEWER_TOKEN` for read-only viewers)
- [ ] CMS bound to localhost; TLS terminated at the proxy
- [ ] `CMS_PUBLIC_URL` = the public HTTPS URL
- [ ] Broker requires auth (`allow_anonymous false`) and uses TLS
- [ ] Devices configured with broker credentials + CA
