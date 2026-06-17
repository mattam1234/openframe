# Your First Automation — OpenFrame Quick Start

This walkthrough takes a brand-new device from power-on to a working automation:
**press a button → toggle an LED, and flash a notification on screen**. It should
take about 15 minutes and needs only an ESP32/ESP8266, an LED, and a push button.

> New to the project? Skim the [README](../README.md) first for the big picture.
> Everything below is done from the **web UI** — no reflashing per change.

---

## 0. What you'll need

- A flashed OpenFrame device (see [README → Build and Run](../README.md#build-and-run)).
- An LED on a GPIO pin (with a resistor), and a momentary push button on another GPIO.
- A phone or laptop on the same network.

If you haven't flashed yet: build the firmware, then upload the filesystem image
so the web UI is present:

```bash
cd firmware
python -m platformio run -e esp32dev -t upload      # firmware
python -m platformio run -e esp32dev -t uploadfs     # web UI assets (LittleFS)
```

---

## 1. First boot & Wi-Fi (the Setup Wizard)

1. On first boot the device starts an access point named **`OpenFrame-XXXX`**.
   Connect to it and open **http://192.168.4.1**.
2. The dashboard shows a **"Finish setup"** banner — click **Run the setup wizard**
   (or open **Setup Wizard** in the nav). Step through:
   - **Device** — give it a friendly name.
   - **Wi-Fi** — pick your network (hit the scan button) and enter the password.
   - **MQTT** — skip for now (optional; needed later for Home Assistant / fleet).
   - **Review → Apply & Reboot.**
3. The device reboots and joins your Wi-Fi. Find it again at its new IP, or — if
   your computer supports mDNS — at **`http://openframe-xxxx.local`** (the
   hostname is shown on the dashboard).

---

## 2. Define the hardware

### The LED (an output)

1. Open **Layout Designer**.
2. Under **Outputs**, click **Add**, then set:
   - **id**: `led1`
   - **type**: `led`
   - **pin**: your LED's GPIO (e.g. `2`)
3. **Save**, then reboot when prompted (new hardware initialises on boot).

You can now toggle it live under **Outputs** to confirm the wiring.

### The button (a digital input)

1. In **Layout Designer**, under **Inputs**, click **Add**:
   - **id**: `button1`
   - **pin**: your button's GPIO (e.g. `4`)
   - **pullup**: on (so the pin idles HIGH and reads LOW when pressed)
2. **Save** and reboot.

---

## 3. Create the automation

Open **Actions** → **Add Action**.

1. **Name**: `Toggle light`.
2. **When (trigger)**: choose **When an input fires**, then **Input** = `button1`,
   **Event** = `Press`.
3. **Steps** — click **Add Step** twice:
   - Step 1 — type **Output Control**: **Output** = `led1`, command **On / Off**,
     and toggle it **On**. *(For a true toggle, see the tip below.)*
   - Step 2 — type **Notification**: **Message** = `Button pressed!`
     *(supports `{{variable}}` templating — try `Light is on at {{button1}}`).*
4. **Save**.

Press the physical button: the LED turns on and a notification appears on the
dashboard (and on any attached display).

> **Make it a real toggle.** Create a boolean variable `light_on` (in
> **Settings → Variables** or via the API), then use two steps with **Conditions**,
> or the **If / Else** control-flow steps in the action editor:
> `If light_on == true → turn led1 off; Else → turn led1 on`, then
> `Toggle` the `light_on` variable. The action editor has `if` / `else` / `endif`
> step types for exactly this.

---

## 4. Try the other trigger types

The same action can be driven by more than a button:

- **On a schedule** — set the trigger to **On a schedule**, mode **Every N seconds**
  (no clock needed) or **Daily at time** (needs NTP/MQTT time). Great for a
  "blink every 30s" heartbeat or a "turn on at 18:00" lamp.
- **Manual** — trigger it by name from the dashboard or over the API/WebSocket.
- **From another node** — with NodeLink/ESP-NOW enabled, one device can trigger an
  action on another (see the [fleet & mesh roadmap](fleet-and-mesh-roadmap.md)).

---

## 5. Where to go next

- **Sensors** — add a BME280/DHT22 etc. in the Sensor Dashboard; the live values
  become `{{sensor.*}}` variables you can use in conditions and notifications, with
  real-time sparklines.
- **Screens** — design a page in the Screen Designer that shows a variable.
- **Webhooks / MQTT** — use an **HTTP Request** step (templated URL + JSON body) to
  call Slack/IFTTT/Home Assistant when something happens.
- **Fleet** — point the device at an MQTT broker and run the
  [CMS](../cms/README.md) to manage many devices at once (incl. a no-hardware
  simulator: `npm run simulate`).
- **Lock it down** — set an **API token** in Settings to require auth for the
  device's API (see the README/security notes).

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Can't reach the UI after Wi-Fi setup | Check the dashboard IP / `openframe-xxxx.local`; if the device fell back to AP mode, re-run the wizard. |
| Clicking a nav item "does nothing" | The device has a stale web UI — re-run `uploadfs` to refresh `/www`. |
| LED/button does nothing | Re-check the GPIO pin numbers and that you rebooted after saving hardware config; test the output live under **Outputs**. |
| Device keeps rebooting | After repeated crashes it enters **Safe Mode** (hardware/automation disabled) so you can fix config — the dashboard shows a red banner. |
