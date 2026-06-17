# Plugin Authoring Guide

A practical, worked walkthrough of extending OpenFrame. There are two paths:

| Path | When | Reflash? |
|------|------|----------|
| **Config plugins** (JSON) | Add an I²C sensor or wire up automation logic | No |
| **C++ plugins** (registries) | New driver families, displays, custom action types | Yes |

For the C++ registry interfaces (`SensorRegistry`, `DisplayRegistry`, `ActionRegistry`…)
see [`plugin-architecture.md`](plugin-architecture.md). This guide focuses on the
**no-reflash** path and ends with where to graduate to C++.

---

## 1. Add an I²C sensor without writing code — `i2c_generic`

The `i2c_generic` sensor type reads any chip whose datasheet gives you register
addresses, by describing the register map in `sensors.json` (edit it in the
**Filesystem** browser, or POST to `/api/sensors`). No driver, no library, no flash.

### Worked example — a chip with a 16-bit temperature register

Say the datasheet says: *temperature is a signed 16-bit big-endian value at register
`0x00`, in units of 1/128 °C.*

```jsonc
{
  "sensors": [
    {
      "id": "roomtemp",
      "type": "i2c_generic",
      "address": 72,                // 0x48
      "poll_interval_ms": 2000,
      "variable_prefix": "room",    // → variable "room_temp"
      "registers": [
        {
          "metric": "temp",
          "reg": 0,                 // 0x00
          "bytes": 2,
          "big_endian": true,
          "signed": true,
          "scale": 0.0078125,       // 1/128
          "offset": 0.0
        }
      ]
    }
  ]
}
```

Each `registers[]` entry maps to one metric. The value the device stores is:

```
value = raw * scale + offset
```

| Field | Default | Meaning |
|-------|---------|---------|
| `metric` | — | metric key (becomes a variable + HA sensor) |
| `reg` | 0 | starting register address |
| `bytes` | 2 | 1 or 2 bytes to read |
| `big_endian` | true | byte order for 2-byte reads |
| `signed` | false | interpret as two's-complement |
| `scale` | 1.0 | multiply the raw value |
| `offset` | 0.0 | add after scaling |

Save, reboot, and `roomtemp`'s `temp` metric appears as a variable, a chart on the
Sensor dashboard, and (if Home Assistant is enabled) an HA sensor entity.

> Tip: run the **Dashboard → Run self-test** I²C scan to confirm the chip's address
> before configuring it.

---

## 2. Wire up behaviour — actions, macros, scenes

No code needed for logic either:

- **Actions** (Action Manager): trigger → conditions → ordered steps. Steps can drive
  outputs, set/await variables (`wait_until`), branch (`if`/`repeat`), restore a
  **scene**, call HTTP/MQTT/Home Assistant, or run an action on another node.
- **Macros**: run several actions in order, with **parameters** — variables set before
  the actions run, so one macro drives many outcomes via `{{variable}}` templating.
- **Scenes**: snapshot current outputs + variables and restore them in one step.

### Worked example — "reading light" macro

1. Define an RGB/WS2812 output (Layout Designer) called `lamp`.
2. Create an **action** `set-lamp` with one step: *Control Output → `lamp` → rgb*,
   colour `{{lamp_r}},{{lamp_g}},{{lamp_b}}` (templated).
3. Create a **macro** `reading-light` that runs `set-lamp`, with **parameters**
   `lamp_r=255`, `lamp_g=180`, `lamp_b=80`.
4. Bind a trigger (a button `Press`, a schedule, or HA) to the macro.

Now `reading-light` is one reusable, parameterised control. Use **Dry-run** (the flask
icon) to preview what an action would do before it drives hardware.

---

## 3. Graduate to a C++ plugin

Reach for the C++ registries in [`plugin-architecture.md`](plugin-architecture.md) when
you need something config can't express:

- a **sensor** with a non-trivial protocol or a vendor library (SPI, multi-step I²C);
- a new **display** type or render mode;
- a custom **action executor** (a new step `type`);
- a **module handler** for an add-on board.

Each is a small class implementing a registry interface, registered in
`registerPlugins()` — the core firmware stays untouched.
