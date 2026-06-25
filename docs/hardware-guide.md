# Hardware Guide — Supported Parts & Wiring

What OpenFrame can drive, and how to wire it. Pin numbers below are GPIO numbers;
choose pins that are free on your board (avoid strapping/flash pins).

---

## Boards

| Board | Target (`pio run -e`) | Notes |
|-------|-----------------------|-------|
| ESP32 DevKit | `esp32dev` | I²C touch, lots of GPIO |
| ESP32-S3 DevKitC | `esp32s3dev` | 16 MB flash layout, large LittleFS |
| ESP32-S3 (GEEK / BOX variants) | `esp32s3geek`, `esp32s3box` | board-specific display wiring |
| ESP8266 (D1 mini) | `esp8266dev` | no touch, single ADC, tighter flash |

---

## Buses & shared wiring

- **I²C** (sensors, RTC, OLED): default `SDA`/`SCL` for your board, `3V3`, `GND`.
  Multiple I²C devices share the bus — just give each a unique address. Run
  **Dashboard → Run self-test** to scan the bus and list addresses.
- **Power**: most sensors/OLEDs run on `3V3`. Servos, WS2812 strips, and steppers
  need a separate supply sized to their current — common the grounds with the ESP.
- **Level shifting**: WS2812 data and 5 V peripherals may need a level shifter from
  the 3.3 V GPIO.

---

## Outputs (Layout Designer → Outputs)

| Type | Wiring | Notes |
|------|--------|-------|
| `led` | GPIO → resistor → LED → GND | set `pwm` for brightness; `gamma` for linear dimming |
| `rgb` | 3 GPIO (R/G/B) → common-anode/cathode LED | brightness + colour; `gamma` supported |
| `ws2812` | GPIO → DIN (level-shift recommended), 5 V + GND | set LED count; animations incl. fire |
| `relay` | GPIO → relay module IN | use `inverted` for active-low modules |
| `buzzer` | GPIO → piezo (PWM tone) | |
| `servo` | GPIO → signal; servo 5 V + common GND | tune `servo_min_us`/`servo_max_us` per servo |
| `stepper` | STEP=`pin`, DIR=`pin_dir`, optional `pin_enable` (A4988/DRV8825/TMC2209) | non-blocking; `max_step_hz` |

```
ESP32 GPIO ──▶ A4988 STEP        ESP32 GPIO 13 ──▶ WS2812 DIN
ESP32 GPIO ──▶ A4988 DIR         5V ───────────▶ WS2812 5V
GND ─────────▶ A4988 GND ◀────── motor PSU GND  GND ─────────▶ WS2812 GND
```

---

## Inputs (Layout Designer → Inputs)

| Type | Wiring | Events |
|------|--------|--------|
| digital | GPIO → button → GND (use `pullup`) | Press/Release/Long/Double/Triple/Hold/Repeat |
| digital + **touch** (ESP32) | bare wire/pad on a touch-capable GPIO | same events (read via `touchRead`) |
| analog | GPIO (ADC pin) → pot/sensor wiper | value + threshold/range events |
| **encoder** (`encoders[]`) | A=`pin_a`, B=`pin_b`, optional button `pin_button` | RotateCW/RotateCCW + Press/Release |
| **keypad** (`keypads[]`) | `rows[]` (driven) × `cols[]` (pull-up) matrix | KeyPress/KeyRelease (per-key label) |

Encoders and keypads are configured in `inputs.json` (Filesystem browser) — see the
[Plugin Authoring Guide](plugin-authoring-guide.md).

---

## Sensors (Layout Designer → Sensors)

| Type | Bus | Metrics |
|------|-----|---------|
| `bme280` / `bmp280` | I²C | temperature, pressure, (humidity), altitude |
| `sht31` | I²C | temperature, humidity |
| `bh1750` | I²C | lux |
| `ina219` | I²C | bus/shunt voltage, current, power |
| `mpu6050` | I²C | accel/gyro |
| `sgp30` | I²C | eCO₂, TVOC (air quality) |
| `scd4x` | I²C | CO₂, temperature, humidity (SCD40/41) |
| `vl53l0x` | I²C | distance (time-of-flight) |
| `max6675` | SPI (`clock_pin`=SCK, `cs_pin`=CS, `pin`=SO) | thermocouple temperature |
| `dht22` | 1-wire (`pin`) | temperature, humidity |
| `ds18b20` | 1-wire (`pin`) | temperature |
| `hx711` | bit-bang (`pin`=DT, `clock_pin`=SCK) | raw, scaled weight |
| `i2c_generic` | I²C | **any** chip via a JSON register map — see the authoring guide |

---

## Displays (Layout Designer → Displays)

| Type | Bus | Notes |
|------|-----|-------|
| `ssd1306` / `sh1106` | I²C | 128×64 / 128×32 OLED |
| `ssd1306_72x40` | I²C | 0.42" 72×40 OLED (U8g2; auto col-offset 28 / COM 0x12) |
| `sh1107` / `ssd1309` | I²C | 128×128 / 128×64 OLED (U8g2) |
| `nokia5110` | SPI | 84×48 PCD8544 LCD |
| `st7789` | SPI | colour TFT incl. the 1.14" 240×135 IPS panel |
| `ili9341` | SPI | 320×240 colour TFT (ILI9342-compatible) |
| `nextion` | UART | smart display (own GUI) |

**SPI TFT wiring**: set `cs_pin` + `dc_pin`, an optional `reset_pin`, and the
**`bl_pin`** (backlight) — without the backlight pin driven the panel inits but
stays black. Leave `mosi_pin`/`sck_pin` blank to use the board's default hardware
SPI bus (fast); set them only when the panel is wired to other pins (the ESP32
routes any GPIO to the SPI peripheral). For the 1.14" 240×135 ST7789 use
`width` 240, `height` 135, `rotation` 90° — the driver re-derives the panel's RAM
offsets from the native portrait geometry.

**Integrated-panel boards auto-configure.** With *no* saved display config, these
boards seed their built-in panel at boot (zero setup); saving any display in the
UI overrides it:

| Board | Panel | LCD pins (CS/DC/RST/MOSI/SCK/BL) |
|-------|-------|----------------------------------|
| `esp32s3geek` (Waveshare S3-GEEK) | 1.14" ST7789 240×135 | 10 / 8 / 9 / 11* / 12* / 7 |
| `esp32s3box` (Espressif S3-BOX) | 2.4" ILI9341 320×240 | 5 / 4 / 48 / 6 / 7 / 45 |

\* The GEEK's LCD is on the default SPI bus, so MOSI/SCK are left at the bus default.

**microSD** (ESP32-S3-BOX only): a card is probed at boot on the board's default
SPI bus (SCK 12 / MISO 13 / MOSI 11 / CS 10) and reported by **Dashboard → Run
self-test** (`sd` section). Detection is best-effort — "no card" is a normal,
passing state.

---

## Choosing pins (quick rules)

- Prefer plain GPIO for buttons/relays/LEDs.
- ESP32 ADC2 pins conflict with WiFi — use ADC1 pins for analog inputs.
- Touch is only on ESP32 touch-capable pins (T0–T9).
- Keep WS2812/servo/stepper power off the ESP's 3V3 regulator.

> This matrix reflects the built-in drivers; add more via the
> [Plugin Authoring Guide](plugin-authoring-guide.md).
