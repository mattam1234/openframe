<template>
  <div class="layout-designer">
    <!-- Toolbar -->
    <div class="ld-toolbar mb-4">
      <div class="ld-title">
        <v-icon size="28" class="mr-2">mdi-developer-board</v-icon>
        <div>
          <h1 class="text-h5 mb-0">Layout Designer</h1>
          <div class="text-caption text-medium-emphasis">
            Map the device's pins to inputs, outputs, sensors, and displays
          </div>
        </div>
      </div>
      <div class="ld-actions">
        <span v-if="boardLabel" class="ld-board">
          <v-icon size="16" class="mr-1">mdi-memory</v-icon>{{ boardLabel }}
        </span>
        <v-btn icon="mdi-refresh" variant="text" :loading="loading" aria-label="Reload" @click="refresh" />
      </div>
    </div>

    <v-alert
      v-if="statusMessage"
      :type="statusMessage.type"
      variant="tonal"
      class="mb-4"
      closable
      @click:close="statusMessage = null"
    >
      {{ statusMessage.text }}
    </v-alert>

    <v-tabs v-model="activeTab" class="ld-tabs mb-4" density="comfortable">
      <v-tab v-for="t in tabs" :key="t.key" :value="t.key" :prepend-icon="t.icon">
        {{ t.label }}
        <v-chip size="x-small" class="ml-2" :color="t.color" variant="flat">{{ t.count }}</v-chip>
      </v-tab>
    </v-tabs>

    <v-window v-model="activeTab">
      <!-- Inputs -->
      <v-window-item value="inputs">
        <div class="ld-grid">
          <v-card v-for="(inp, idx) in inputs" :key="idx" class="ld-item ld-item--input" variant="flat">
            <div class="ld-item-head">
              <v-icon color="primary">mdi-gesture-tap</v-icon>
              <v-text-field v-model="inp.id" density="compact" variant="plain" hide-details class="ld-id" placeholder="id" />
              <v-btn icon="mdi-close" size="x-small" variant="text" color="error" aria-label="Remove input" @click="removeInput(idx)" />
            </div>
            <div class="ld-fields">
              <v-select
                v-model="inp.subtype"
                :items="['digital', 'analog', 'encoder']"
                label="Type"
                density="compact"
                variant="outlined"
                hide-details
              />
              <template v-if="inp.subtype === 'encoder'">
                <v-select v-model="inp.pin_a" :items="ioPins" label="Pin A" density="compact" variant="outlined" hide-details />
                <v-select v-model="inp.pin_b" :items="ioPins" label="Pin B" density="compact" variant="outlined" hide-details />
              </template>
              <v-select
                v-else
                v-model="inp.pin"
                :items="inp.subtype === 'analog' ? adcPins : ioPins"
                label="Pin"
                density="compact"
                variant="outlined"
                hide-details
              />
              <v-checkbox
                v-if="inp.subtype !== 'analog' && inp.subtype !== 'encoder'"
                v-model="inp.touch"
                label="Capacitive touch"
                density="compact"
                hide-details
                title="Read this pin as a capacitive touch pad (ESP32)"
              />
            </div>
            <!-- Every field the firmware's InputManager parses for this type. Blank
                 number fields fall back to the firmware default (the placeholder).
                 Inputs hot-reload, so these apply live on save. -->
            <v-expansion-panels variant="accordion" class="ld-advanced">
              <v-expansion-panel>
                <v-expansion-panel-title>Advanced</v-expansion-panel-title>
                <v-expansion-panel-text>
                  <div v-if="inp.subtype === 'analog'" class="ld-fields">
                    <v-text-field v-model.number="inp.poll_interval_ms" label="Poll interval (ms)" placeholder="30" type="number" min="1" density="compact" variant="outlined" hide-details />
                    <v-text-field v-model.number="inp.min_delta" label="Min change to report (counts)" placeholder="8" type="number" min="0" density="compact" variant="outlined" hide-details title="Noise smoothing: value events only fire when the reading moves at least this many ADC counts" />
                    <v-checkbox v-model="inp.inverted" label="Inverted" density="compact" hide-details title="Invert the raw reading" />
                    <v-switch v-model="inp.threshold_enabled" label="Threshold events" density="compact" color="primary" hide-details inset title="Emit above/below events when the value crosses a threshold" />
                    <template v-if="inp.threshold_enabled">
                      <v-text-field v-model.number="inp.threshold" label="Threshold (counts)" placeholder="2048" type="number" min="0" density="compact" variant="outlined" hide-details />
                      <v-text-field v-model.number="inp.threshold_hysteresis" label="Hysteresis (counts)" placeholder="16" type="number" min="0" density="compact" variant="outlined" hide-details title="Dead band around the threshold to stop event flapping" />
                    </template>
                    <v-switch v-model="inp.range_enabled" label="Range events" density="compact" color="primary" hide-details inset title="Emit enter/leave events for a value range" />
                    <template v-if="inp.range_enabled">
                      <v-text-field v-model.number="inp.range_min" label="Range min (counts)" placeholder="0" type="number" min="0" density="compact" variant="outlined" hide-details />
                      <v-text-field v-model.number="inp.range_max" label="Range max (counts)" placeholder="4095" type="number" min="0" density="compact" variant="outlined" hide-details />
                    </template>
                  </div>
                  <div v-else-if="inp.subtype === 'encoder'" class="ld-fields">
                    <v-select v-model="inp.pin_button" :items="ioPins" label="Button pin (blank = none)" density="compact" variant="outlined" hide-details clearable />
                    <v-checkbox v-model="inp.pullup" label="Internal pull-ups" density="compact" hide-details />
                    <v-text-field v-model.number="inp.debounce_ms" label="Button debounce (ms)" placeholder="30" type="number" min="0" density="compact" variant="outlined" hide-details />
                  </div>
                  <div v-else class="ld-fields">
                    <v-checkbox v-model="inp.pullup" label="Internal pull-up" density="compact" hide-details />
                    <v-checkbox v-model="inp.pulldown" label="Internal pull-down (ESP32)" density="compact" hide-details />
                    <v-checkbox v-model="inp.inverted" label="Inverted" density="compact" hide-details title="Invert the raw pin reading" />
                    <v-text-field v-if="inp.touch" v-model.number="inp.touch_threshold" label="Touch threshold" placeholder="40" type="number" min="0" density="compact" variant="outlined" hide-details title="Counts as pressed while touchRead() is below this value" />
                    <v-text-field v-model.number="inp.debounce_ms" label="Debounce (ms)" placeholder="30" type="number" min="0" density="compact" variant="outlined" hide-details />
                    <v-text-field v-model.number="inp.hold_ms" label="Hold event after (ms)" placeholder="500" type="number" min="0" density="compact" variant="outlined" hide-details />
                    <v-text-field v-model.number="inp.long_press_ms" label="Long press (ms)" placeholder="1200" type="number" min="0" density="compact" variant="outlined" hide-details />
                    <v-text-field v-model.number="inp.multi_press_window_ms" label="Multi-press window (ms)" placeholder="350" type="number" min="0" density="compact" variant="outlined" hide-details title="Max gap between presses that still counts as a double/triple press" />
                    <v-text-field v-model.number="inp.repeat_start_ms" label="Repeat starts after (ms)" placeholder="800" type="number" min="0" density="compact" variant="outlined" hide-details />
                    <v-text-field v-model.number="inp.repeat_interval_ms" label="Repeat interval (ms)" placeholder="250" type="number" min="0" density="compact" variant="outlined" hide-details />
                  </div>
                  <div class="text-caption text-medium-emphasis mt-1">
                    Blank fields use the firmware default shown as the placeholder.
                  </div>
                </v-expansion-panel-text>
              </v-expansion-panel>
            </v-expansion-panels>
          </v-card>
          <button class="ld-add" @click="addInput">
            <v-icon size="28">mdi-plus</v-icon>
            <span>Add input</span>
          </button>
        </div>
        <div class="ld-savebar">
          <v-btn color="primary" :loading="savingInputs" :disabled="!loaded.inputs" prepend-icon="mdi-content-save" @click="saveInputs">
            Save inputs
          </v-btn>
        </div>
      </v-window-item>

      <!-- Outputs -->
      <v-window-item value="outputs">
        <div class="ld-grid">
          <v-card v-for="(out, idx) in outputs" :key="idx" class="ld-item ld-item--output" variant="flat">
            <div class="ld-item-head">
              <v-icon color="amber-darken-2">mdi-led-on</v-icon>
              <v-text-field v-model="out.id" density="compact" variant="plain" hide-details class="ld-id" placeholder="id" />
              <v-btn icon="mdi-close" size="x-small" variant="text" color="error" aria-label="Remove output" @click="removeOutput(idx)" />
            </div>
            <div class="ld-fields">
              <v-select
                v-model="out.type"
                :items="['led', 'rgb', 'ws2812', 'relay', 'buzzer', 'servo', 'stepper']"
                label="Type"
                density="compact"
                variant="outlined"
                hide-details
              />
              <v-select v-model="out.pin" :items="ioPins" :label="out.type === 'stepper' ? 'STEP pin' : 'Pin'" density="compact" variant="outlined" hide-details />
              <!-- WS2812 strips: addressable LED count (applied on restart). -->
              <v-text-field
                v-if="out.type === 'ws2812'"
                v-model.number="out.led_count"
                label="LED count"
                type="number"
                min="1"
                density="compact"
                variant="outlined"
                hide-details
              />
              <!-- Steppers: the DIR pin (STEP uses the main pin). -->
              <v-select
                v-else-if="out.type === 'stepper'"
                v-model="out.pin_dir"
                :items="ioPins"
                label="DIR pin"
                density="compact"
                variant="outlined"
                hide-details
              />
              <!-- LED/RGB: opt-in gamma for perceptually-linear dimming. -->
              <v-checkbox
                v-else-if="['led', 'rgb'].includes(out.type)"
                v-model="out.gamma"
                label="Gamma correction"
                density="compact"
                hide-details
                title="Perceptually-linear dimming"
              />
            </div>
            <!-- Every field the firmware's OutputManager parses for this type. Blank
                 number fields fall back to the firmware default (the placeholder).
                 Output config still applies on restart. -->
            <v-expansion-panels variant="accordion" class="ld-advanced">
              <v-expansion-panel>
                <v-expansion-panel-title>Advanced</v-expansion-panel-title>
                <v-expansion-panel-text>
                  <div class="ld-fields">
                    <v-checkbox v-model="out.inverted" label="Inverted (active-low)" density="compact" hide-details title="Drive the pin LOW to turn the output on" />
                    <template v-if="out.type === 'led'">
                      <v-checkbox v-model="out.pwm" label="PWM dimming" density="compact" hide-details title="Enable brightness control on this pin" />
                      <template v-if="out.pwm">
                        <v-text-field v-model.number="out.pwm_channel" label="PWM channel (ESP32)" placeholder="0" type="number" min="0" max="5" density="compact" variant="outlined" hide-details title="LEDC channel 0-5; channels 6-7 are reserved for the display backlight" />
                        <v-text-field v-model.number="out.pwm_frequency" label="PWM frequency (Hz)" placeholder="5000" type="number" min="1" density="compact" variant="outlined" hide-details />
                        <v-text-field v-model.number="out.pwm_resolution" label="PWM resolution (bits)" placeholder="8" type="number" min="1" max="16" density="compact" variant="outlined" hide-details />
                      </template>
                    </template>
                    <template v-else-if="out.type === 'rgb'">
                      <v-select v-model="out.pin_r" :items="ioPins" label="R pin" density="compact" variant="outlined" hide-details clearable />
                      <v-select v-model="out.pin_g" :items="ioPins" label="G pin" density="compact" variant="outlined" hide-details clearable />
                      <v-select v-model="out.pin_b" :items="ioPins" label="B pin" density="compact" variant="outlined" hide-details clearable />
                      <v-text-field v-model.number="out.channel_r" label="PWM channel R (ESP32)" placeholder="0" type="number" min="0" max="5" density="compact" variant="outlined" hide-details title="LEDC channel 0-5; channels 6-7 are reserved for the display backlight" />
                      <v-text-field v-model.number="out.channel_g" label="PWM channel G (ESP32)" placeholder="1" type="number" min="0" max="5" density="compact" variant="outlined" hide-details title="LEDC channel 0-5; channels 6-7 are reserved for the display backlight" />
                      <v-text-field v-model.number="out.channel_b" label="PWM channel B (ESP32)" placeholder="2" type="number" min="0" max="5" density="compact" variant="outlined" hide-details title="LEDC channel 0-5; channels 6-7 are reserved for the display backlight" />
                      <v-text-field v-model.number="out.pwm_frequency" label="PWM frequency (Hz)" placeholder="5000" type="number" min="1" density="compact" variant="outlined" hide-details />
                      <v-text-field v-model.number="out.pwm_resolution" label="PWM resolution (bits)" placeholder="8" type="number" min="1" max="16" density="compact" variant="outlined" hide-details />
                    </template>
                    <template v-else-if="out.type === 'ws2812'">
                      <v-slider :model-value="out.brightness ?? 255" label="Brightness" :min="0" :max="255" :step="1" thumb-label density="compact" hide-details color="amber-darken-2" @update:model-value="out.brightness = $event" />
                    </template>
                    <template v-else-if="out.type === 'servo'">
                      <v-text-field v-model.number="out.servo_min_us" label="Min pulse (µs)" placeholder="500" type="number" min="0" density="compact" variant="outlined" hide-details title="Pulse width at 0°" />
                      <v-text-field v-model.number="out.servo_max_us" label="Max pulse (µs)" placeholder="2500" type="number" min="0" density="compact" variant="outlined" hide-details title="Pulse width at 180°" />
                      <v-text-field v-model.number="out.pwm_frequency" label="PWM frequency (Hz)" placeholder="50" type="number" min="1" density="compact" variant="outlined" hide-details />
                      <v-text-field v-model.number="out.pwm_resolution" label="PWM resolution (bits)" placeholder="16" type="number" min="1" max="16" density="compact" variant="outlined" hide-details />
                    </template>
                    <template v-else-if="out.type === 'stepper'">
                      <v-select v-model="out.pin_enable" :items="ioPins" label="Enable pin (blank = none)" density="compact" variant="outlined" hide-details clearable />
                      <v-text-field v-model.number="out.steps_per_rev" label="Steps per revolution" placeholder="200" type="number" min="1" density="compact" variant="outlined" hide-details />
                      <v-text-field v-model.number="out.max_step_hz" label="Max step rate (Hz)" placeholder="1000" type="number" min="1" density="compact" variant="outlined" hide-details />
                    </template>
                    <template v-else-if="out.type === 'buzzer'">
                      <v-text-field v-model.number="out.pwm_channel" label="PWM channel (ESP32)" placeholder="0" type="number" min="0" max="5" density="compact" variant="outlined" hide-details title="LEDC channel used for tone generation (0-5; 6-7 are reserved for the display backlight)" />
                    </template>
                  </div>
                  <div class="text-caption text-medium-emphasis mt-1">
                    Blank fields use the firmware default shown as the placeholder.
                    <template v-if="out.type === 'rgb'">RGB drives the R/G/B pins above — the main Pin field is unused.</template>
                  </div>
                </v-expansion-panel-text>
              </v-expansion-panel>
            </v-expansion-panels>
          </v-card>
          <button class="ld-add" @click="addOutput">
            <v-icon size="28">mdi-plus</v-icon>
            <span>Add output</span>
          </button>
        </div>
        <div class="ld-savebar">
          <v-btn color="amber-darken-2" :loading="savingOutputs" :disabled="!loaded.outputs" prepend-icon="mdi-content-save" @click="saveOutputs">
            Save outputs
          </v-btn>
        </div>
      </v-window-item>

      <!-- Sensors -->
      <v-window-item value="sensors">
        <div class="ld-grid">
          <v-card v-for="(sensor, idx) in sensors" :key="idx" class="ld-item ld-item--sensor" variant="flat">
            <div class="ld-item-head">
              <v-icon color="teal">mdi-chip</v-icon>
              <v-text-field v-model="sensor.id" density="compact" variant="plain" hide-details class="ld-id" placeholder="id" />
              <v-btn icon="mdi-close" size="x-small" variant="text" color="error" aria-label="Remove sensor" @click="removeSensor(idx)" />
            </div>
            <div class="ld-fields">
              <v-select v-model="sensor.type" :items="sensorTypes" label="Type" density="compact" variant="outlined" hide-details />
              <v-text-field v-model="sensor.address" label="Address / pin" density="compact" variant="outlined" hide-details />
              <v-text-field v-model.number="sensor.poll_interval_ms" label="Poll (ms)" type="number" density="compact" variant="outlined" hide-details />
              <v-switch v-model="sensor.enabled" label="Enabled" density="compact" color="teal" hide-details inset />
            </div>
          </v-card>
          <button class="ld-add" @click="addSensor">
            <v-icon size="28">mdi-plus</v-icon>
            <span>Add sensor</span>
          </button>
        </div>
        <div class="ld-savebar">
          <v-btn color="teal" :loading="savingSensors" :disabled="!loaded.sensors" prepend-icon="mdi-content-save" @click="saveSensors">
            Save sensors
          </v-btn>
        </div>
      </v-window-item>

      <!-- Displays -->
      <v-window-item value="displays">
        <v-alert v-if="displaysSeeded" type="info" variant="tonal" density="compact" class="mb-3">
          This board has a built-in panel, shown below with its default wiring. It's
          working already — press <strong>Save</strong> to make the config editable and
          persistent.
        </v-alert>
        <div class="ld-grid">
          <v-card v-for="(disp, idx) in displays" :key="idx" class="ld-item ld-item--display" variant="flat">
            <div class="ld-item-head">
              <v-icon color="indigo">mdi-monitor</v-icon>
              <v-text-field v-model="disp.id" density="compact" variant="plain" hide-details class="ld-id" placeholder="id" />
              <v-btn icon="mdi-close" size="x-small" variant="text" color="error" aria-label="Remove display" @click="removeDisplay(idx)" />
            </div>
            <div class="ld-fields">
              <v-select v-model="disp.type" :items="displayTypes" label="Type" density="compact" variant="outlined" hide-details />
              <!-- I²C displays (SSD1306, SH1106, etc.) -->
              <v-text-field v-if="isI2cDisplay(disp.type)" v-model="disp.address" label="Address" placeholder="0x3C" density="compact" variant="outlined" hide-details />
              <!-- Common fields -->
              <v-text-field v-model.number="disp.width" label="Width (px)" type="number" density="compact" variant="outlined" hide-details />
              <v-text-field v-model.number="disp.height" label="Height (px)" type="number" density="compact" variant="outlined" hide-details />
              <!-- I²C pins -->
              <template v-if="isI2cDisplay(disp.type)">
                <v-select v-model="disp.sda_pin" :items="ioPins" label="SDA pin" density="compact" variant="outlined" hide-details clearable />
                <v-select v-model="disp.scl_pin" :items="ioPins" label="SCL pin" density="compact" variant="outlined" hide-details clearable />
              </template>
              <!-- UART pins (Nextion) -->
              <template v-if="isUartDisplay(disp.type)">
                <v-select v-model="disp.rx_pin" :items="ioPins" label="RX pin" density="compact" variant="outlined" hide-details clearable />
                <v-select v-model="disp.tx_pin" :items="ioPins" label="TX pin" density="compact" variant="outlined" hide-details clearable />
                <v-text-field v-model.number="disp.baud_rate" label="Baud rate" placeholder="9600" type="number" density="compact" variant="outlined" hide-details />
              </template>
              <v-switch v-model="disp.enabled" label="Enabled" density="compact" color="indigo" hide-details inset />
            </div>
            <!-- F4: cycle through this display's pages on a timer. Page order comes
                 from the Screen Designer; without an explicit order, pages rotate in
                 load order. -->
            <div class="ld-fields">
              <v-switch v-model="disp.rotation_enabled" label="Auto-rotate screens" density="compact" color="indigo" hide-details inset />
              <v-text-field v-if="disp.rotation_enabled" v-model.number="disp.rotation_interval_ms"
                label="Rotate every (ms)" type="number" min="1000" placeholder="8000"
                density="compact" variant="outlined" hide-details />
            </div>
            <v-expansion-panels variant="accordion" class="ld-advanced">
              <!-- Panel brightness + scheduled night dimming. Times are shown as
                   HH:MM but stored as minutes past midnight (night_start_min /
                   night_end_min); untouched controls keep the firmware defaults. -->
              <v-expansion-panel>
                <v-expansion-panel-title>Brightness &amp; night mode</v-expansion-panel-title>
                <v-expansion-panel-text>
                  <div class="ld-fields">
                    <v-slider :model-value="disp.brightness ?? 255" label="Brightness" :min="0" :max="255" :step="1" thumb-label density="compact" hide-details color="indigo" title="An untouched slider follows the panel's tuned Contrast setting" @update:model-value="disp.brightness = $event" />
                    <div class="text-caption text-medium-emphasis">Untouched, the panel keeps its tuned contrast (Brightness follows Contrast when unset).</div>
                    <v-switch v-model="disp.night_enabled" label="Night mode" density="compact" color="indigo" hide-details inset />
                    <template v-if="disp.night_enabled">
                      <v-text-field :model-value="minutesToHhmm(disp.night_start_min, 1320)" label="Night starts" type="time" density="compact" variant="outlined" hide-details @update:model-value="disp.night_start_min = hhmmToMinutes($event)" />
                      <v-text-field :model-value="minutesToHhmm(disp.night_end_min, 420)" label="Night ends" type="time" density="compact" variant="outlined" hide-details @update:model-value="disp.night_end_min = hhmmToMinutes($event)" />
                      <v-slider :model-value="disp.night_brightness ?? 10" label="Night brightness" :min="0" :max="255" :step="1" thumb-label density="compact" hide-details color="indigo" @update:model-value="disp.night_brightness = $event" />
                      <v-switch v-model="disp.night_blank" label="Blank screen at night" density="compact" color="indigo" hide-details inset title="Turn the panel fully off at night instead of dimming" />
                    </template>
                  </div>
                  <div class="text-caption text-medium-emphasis mt-1">
                    Dims the panel between the start and end times — a window that
                    crosses midnight (e.g. 22:00 → 07:00) works. Clearing a time falls
                    back to the default shown.
                  </div>
                </v-expansion-panel-text>
              </v-expansion-panel>
              <v-expansion-panel>
                <v-expansion-panel-title>Advanced (pins &amp; settings)</v-expansion-panel-title>
                <v-expansion-panel-text>
                  <div class="ld-fields">
                    <!-- OLED sub-window settings (SSD1306 only) -->
                    <template v-if="isOledDisplay(disp.type)">
                      <v-text-field v-model.number="disp.col_offset" label="Col offset" placeholder="auto" type="number" density="compact" variant="outlined" hide-details />
                      <v-text-field v-model.number="disp.page_offset" label="Page offset" placeholder="auto" type="number" density="compact" variant="outlined" hide-details />
                      <v-text-field v-model.number="disp.com_pins" label="COM pins (e.g. 18 = 0x12)" placeholder="auto" type="number" density="compact" variant="outlined" hide-details />
                    </template>
                    <!-- SPI display pins (TFT + Nokia 5110) -->
                    <template v-if="isSpiDisplay(disp.type)">
                      <v-select v-model="disp.reset_pin" :items="ioPins" label="Reset pin" density="compact" variant="outlined" hide-details clearable />
                      <v-select v-model="disp.cs_pin" :items="ioPins" label="CS pin (SPI)" density="compact" variant="outlined" hide-details clearable />
                      <v-select v-model="disp.dc_pin" :items="ioPins" label="DC pin (SPI)" density="compact" variant="outlined" hide-details clearable />
                      <v-select v-model="disp.mosi_pin" :items="ioPins" label="MOSI pin (SPI, blank = default bus)" density="compact" variant="outlined" hide-details clearable />
                      <v-select v-model="disp.sck_pin" :items="ioPins" label="SCK pin (SPI, blank = default bus)" density="compact" variant="outlined" hide-details clearable />
                    </template>
                    <!-- TFT backlight pin -->
                    <template v-if="isTftDisplay(disp.type)">
                      <v-select v-model="disp.bl_pin" :items="ioPins" label="Backlight pin" density="compact" variant="outlined" hide-details clearable />
                    </template>
                    <!-- SPI frequency (SPI displays) -->
                    <template v-if="isSpiDisplay(disp.type)">
                      <v-text-field v-model.number="disp.spi_frequency" label="SPI frequency (Hz)" placeholder="27000000" type="number" density="compact" variant="outlined" hide-details />
                    </template>
                    <!-- Contrast (OLED) -->
                    <template v-if="isOledDisplay(disp.type)">
                      <v-text-field v-model.number="disp.contrast" label="Contrast (0-255)" placeholder="255" type="number" density="compact" variant="outlined" hide-details />
                    </template>
                    <!-- Rotation (not for Nextion) -->
                    <template v-if="!isUartDisplay(disp.type)">
                      <v-select v-model.number="disp.rotation" :items="rotationItems" label="Rotation" density="compact" variant="outlined" hide-details />
                    </template>
                  </div>
                  <div class="text-caption text-medium-emphasis mt-1">
                    <template v-if="isOledDisplay(disp.type)">
                      Leave offsets blank for auto. The 0.42" 72×40 panel auto-derives col offset 28 / COM pins 0x12 from its geometry.
                    </template>
                    <template v-if="isSpiDisplay(disp.type)">
                      For SPI TFTs (ST7789/ILI9341), set CS/DC and the backlight pin; leave MOSI/SCK blank to use the board's default SPI bus.
                      The 1.14" 240×135 ST7789 uses width 240, height 135, rotation 90°.
                    </template>
                  </div>
                </v-expansion-panel-text>
              </v-expansion-panel>
            </v-expansion-panels>
          </v-card>
          <button class="ld-add" @click="addDisplay">
            <v-icon size="28">mdi-plus</v-icon>
            <span>Add display</span>
          </button>
        </div>
        <div class="ld-savebar">
          <v-btn color="indigo" :loading="savingDisplays" :disabled="!loaded.displays" prepend-icon="mdi-content-save" @click="saveDisplays">
            Save displays
          </v-btn>
        </div>
      </v-window-item>
    </v-window>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import api from '../api/client'
import { minutesToHhmm, hhmmToMinutes } from '../lib/timeFormat'
import { pinItems } from '../utils/boardPins'

const loading = ref(false)
const statusMessage = ref(null)
const savingInputs = ref(false)
const savingOutputs = ref(false)
const savingSensors = ref(false)
const savingDisplays = ref(false)
const activeTab = ref('inputs')

const inputs = ref([])
// Keypad configs have no card UI (rows/cols/keys matrices); they're loaded from
// /api/inputs and posted back verbatim so saving inputs never wipes them.
const keypadsRaw = ref([])
const outputs = ref([])
const sensors = ref([])
const displays = ref([])
// True when the displays list came from the board's built-in panel default rather
// than a saved config (see /api/displays `seeded`). Cleared once the user saves.
const displaysSeeded = ref(false)

// Per-section load success. A section that failed to load must not be saveable —
// otherwise an empty form (from a failed GET, e.g. while the device reboots after
// a previous save) would overwrite the device's real config with an empty file.
const loaded = ref({ inputs: false, outputs: false, sensors: false, displays: false })

const boardType = ref('')
const boardLabel = computed(() => boardType.value || '')
const ioPins = computed(() => pinItems(boardType.value, 'io'))
const adcPins = computed(() => pinItems(boardType.value, 'adc'))

const sensorTypes = [
  'bme280', 'bmp280', 'sht31', 'aht20', 'bh1750', 'scd4x', 'sgp30', 'ccs811',
  'dht22', 'dht11', 'ds18b20', 'max6675',
  'mpu6050', 'vl53l0x', 'ultrasonic',
  'ina219', 'ads1115', 'analog', 'hx711',
  'mhz19', 'pms5003',
  'i2c_generic',
]
const displayTypes = [
  { title: 'SSD1306 (128×64 etc.)', value: 'ssd1306' },
  { title: 'SSD1306 72×40 — 0.42" OLED (U8g2)', value: 'ssd1306_72x40' },
  { title: 'SH1106', value: 'sh1106' },
  { title: 'SH1107 128×128 OLED (U8g2)', value: 'sh1107' },
  { title: 'SSD1309 128×64 OLED (U8g2)', value: 'ssd1309' },
  { title: 'Nokia 5110 / PCD8544 — 84×48 (SPI)', value: 'nokia5110' },
  { title: 'ST7789 TFT — incl. 1.14" 240×135 (SPI)', value: 'st7789' },
  { title: 'ILI9341 / ILI9342 TFT — 320×240 (SPI)', value: 'ili9341' },
  { title: 'Nextion (UART)', value: 'nextion' },
]
const rotationItems = [
  { title: '0°', value: 0 },
  { title: '90°', value: 1 },
  { title: '180°', value: 2 },
  { title: '270°', value: 3 },
]

const tabs = computed(() => [
  { key: 'inputs', label: 'Inputs', icon: 'mdi-gesture-tap', color: 'primary', count: inputs.value.length },
  { key: 'outputs', label: 'Outputs', icon: 'mdi-led-on', color: 'amber-darken-2', count: outputs.value.length },
  { key: 'sensors', label: 'Sensors', icon: 'mdi-chip', color: 'teal', count: sensors.value.length },
  { key: 'displays', label: 'Displays', icon: 'mdi-monitor', color: 'indigo', count: displays.value.length },
])

function addInput() {
  inputs.value.push({ id: `btn${inputs.value.length + 1}`, subtype: 'digital', pin: 0, pullup: true, inverted: false, touch: false })
}

function removeInput(idx) {
  inputs.value.splice(idx, 1)
}

function addOutput() {
  // led_count/brightness only apply to ws2812 but are always sent so the device
  // persists them when the type is switched to an addressable strip.
  outputs.value.push({ id: `led${outputs.value.length + 1}`, type: 'led', pin: 0, inverted: false, led_count: 1, brightness: 255, pin_dir: 0, gamma: false })
}

function removeOutput(idx) {
  outputs.value.splice(idx, 1)
}

function addSensor() {
  sensors.value.push({ id: `sensor${sensors.value.length + 1}`, type: 'bme280', address: '0x76', poll_interval_ms: 5000, enabled: true })
}

function removeSensor(idx) {
  sensors.value.splice(idx, 1)
}

function addDisplay() {
  // C3 boards ship with the 0.42" 72×40 OLED on GPIO5/6 — prefill those so it
  // works out of the box; other boards default to a common 128×64 panel.
  const isC3 = (boardType.value || '').toUpperCase().includes('C3')
  displays.value.push(isC3
    ? { id: `display${displays.value.length + 1}`, type: 'ssd1306_72x40', address: '0x3C', width: 72, height: 40, sda_pin: 5, scl_pin: 6, enabled: true }
    : { id: `display${displays.value.length + 1}`, type: 'ssd1306', address: '0x3C', width: 128, height: 64, enabled: true })
}

function removeDisplay(idx) {
  displays.value.splice(idx, 1)
}

async function saveInputs() {
  if (!loaded.value.inputs) {
    statusMessage.value = { type: 'error', text: 'Inputs not loaded — refresh before saving.' }
    return
  }
  savingInputs.value = true
  statusMessage.value = null
  try {
    // Strip only the UI-only `subtype`; keep every other field (debounce, hold,
    // thresholds, ranges…) so saving here never drops advanced settings. Keypads
    // have no UI and are round-tripped verbatim (the device rebuilds inputs.json
    // from this POST, so omitting them would wipe them).
    const digital = inputs.value.filter(i => !i.subtype || i.subtype === 'digital')
      .map(({ subtype, ...rest }) => ({ pullup: true, inverted: false, ...rest }))
    const analog = inputs.value.filter(i => i.subtype === 'analog')
      .map(({ subtype, ...rest }) => ({ inverted: false, ...rest }))
    const encoders = inputs.value.filter(i => i.subtype === 'encoder')
      .map(({ subtype, ...rest }) => ({ pullup: true, ...rest }))
    const res = await api.post('/api/inputs', { digital, analog, encoders, keypads: keypadsRaw.value })
    statusMessage.value = { type: 'success', text: res?.message || (res?.restartRequired ? 'Inputs saved. Restart device to apply.' : 'Inputs saved and applied.') }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingInputs.value = false
  }
}

async function saveOutputs() {
  if (!loaded.value.outputs) {
    statusMessage.value = { type: 'error', text: 'Outputs not loaded — refresh before saving.' }
    return
  }
  savingOutputs.value = true
  statusMessage.value = null
  try {
    const res = await api.post('/api/outputs', { outputs: outputs.value })
    statusMessage.value = { type: 'success', text: res?.message || 'Outputs saved. Restart device to apply.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingOutputs.value = false
  }
}

async function saveSensors() {
  if (!loaded.value.sensors) {
    statusMessage.value = { type: 'error', text: 'Sensors not loaded — refresh before saving.' }
    return
  }
  savingSensors.value = true
  statusMessage.value = null
  try {
    const res = await api.post('/api/sensors', { sensors: sensors.value })
    statusMessage.value = { type: 'success', text: res?.message || (res?.restartRequired ? 'Sensors saved. Restart device to apply.' : 'Sensors saved and applied.') }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingSensors.value = false
  }
}

async function saveDisplays() {
  if (!loaded.value.displays) {
    statusMessage.value = { type: 'error', text: 'Displays not loaded — refresh before saving.' }
    return
  }
  savingDisplays.value = true
  statusMessage.value = null
  try {
    const res = await api.post('/api/displays', { displays: displays.value })
    displaysSeeded.value = false  // now persisted — no longer a board default
    statusMessage.value = { type: 'success', text: res?.message || (res?.restartRequired ? 'Displays saved. Restart device to apply.' : 'Displays saved and applied.') }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingDisplays.value = false
  }
}

async function refresh() {
  loading.value = true
  statusMessage.value = null

  // Board type drives the pin dropdown lists. Non-fatal if it fails.
  try {
    const hw = await api.get('/api/hardware')
    boardType.value = hw?.board?.board_type || ''
  } catch (_) { /* keep previous / fallback list */ }

  // Load each section independently. On failure, leave the existing data untouched
  // and mark the section unloaded so its Save button stays disabled — never let a
  // failed load turn into an empty overwrite.
  await Promise.all([
    api.get('/api/inputs').then((d) => {
      // Normalise booleans whose firmware default is true (pullup) so the
      // Advanced checkboxes show the effective value for configs that omit them.
      const dd = (d.digital || []).map(i => ({ ...i, subtype: 'digital', pullup: i.pullup ?? true, inverted: i.inverted ?? false }))
      const aa = (d.analog || []).map(i => ({ ...i, subtype: 'analog' }))
      const ee = (d.encoders || []).map(i => ({ ...i, subtype: 'encoder', pullup: i.pullup ?? true }))
      inputs.value = [...dd, ...aa, ...ee]
      keypadsRaw.value = d.keypads || []
      loaded.value.inputs = true
    }).catch(() => { loaded.value.inputs = false }),
    api.get('/api/outputs').then((d) => { outputs.value = d.outputs || []; loaded.value.outputs = true })
      .catch(() => { loaded.value.outputs = false }),
    api.get('/api/sensors').then((d) => { sensors.value = d.sensors || []; loaded.value.sensors = true })
      .catch(() => { loaded.value.sensors = false }),
    api.get('/api/displays').then((d) => {
      // The device stores the I²C address as a number (e.g. 60); show it as hex
      // ("0x3C") so it's editable in the familiar form. The save path accepts both.
      displays.value = (d.displays || []).map((disp) => ({
        ...disp,
        address: typeof disp.address === 'number'
          ? '0x' + disp.address.toString(16).toUpperCase()
          : (disp.address || '0x3C'),
      }))
      displaysSeeded.value = !!d.seeded
      loaded.value.displays = true
    })
      .catch(() => { loaded.value.displays = false }),
  ])

  const failed = Object.entries(loaded.value).filter(([, ok]) => !ok).map(([k]) => k)
  if (failed.length) {
    statusMessage.value = {
      type: 'error',
      text: `Could not load: ${failed.join(', ')}. Saving is disabled for those sections until a successful reload, so the device config can't be overwritten by mistake. Press Refresh once the device is back online.`,
    }
  }
  loading.value = false
}

// Helper functions to determine which pins/settings are shown for each display type
function isI2cDisplay(type) {
  return ['ssd1306', 'ssd1306_72x40', 'sh1106', 'sh1107', 'ssd1309'].includes(type)
}

function isSpiDisplay(type) {
  return ['nokia5110', 'st7789', 'ili9341'].includes(type)
}

function isTftDisplay(type) {
  return ['st7789', 'ili9341'].includes(type)
}

function isOledDisplay(type) {
  return ['ssd1306', 'ssd1306_72x40', 'sh1106', 'sh1107', 'ssd1309'].includes(type)
}

function isUartDisplay(type) {
  return type === 'nextion'
}

onMounted(() => {
  refresh()
})
</script>

<style scoped>
.ld-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
  flex-wrap: wrap;
}
.ld-title { display: flex; align-items: center; }
.ld-actions { display: flex; align-items: center; gap: 0.75rem; }
.ld-board {
  display: inline-flex;
  align-items: center;
  font-size: 0.8rem;
  font-family: 'Roboto Mono', monospace;
  color: rgb(var(--v-theme-on-surface));
  opacity: 0.7;
  border: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
  border-radius: 999px;
  padding: 0.15rem 0.6rem;
}

.ld-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
  gap: 1rem;
}

/* Each device is a card with a category-coloured left accent. */
.ld-item {
  border: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
  border-left-width: 4px;
  border-radius: 12px;
  padding: 0.75rem 1rem 1rem;
}
.ld-item--input { border-left-color: rgb(var(--v-theme-primary)); }
.ld-item--output { border-left-color: #f59e0b; }
.ld-item--sensor { border-left-color: #14b8a6; }
.ld-item--display { border-left-color: #6366f1; }

.ld-item-head {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 0.5rem;
}
.ld-id :deep(input) { font-weight: 600; font-size: 0.95rem; }

.ld-fields { display: flex; flex-direction: column; gap: 0.6rem; }

/* "Add" ghost tile matches a card's footprint. */
.ld-add {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 0.35rem;
  min-height: 120px;
  border: 1px dashed rgba(var(--v-border-color), 0.6);
  border-radius: 12px;
  color: rgb(var(--v-theme-on-surface));
  opacity: 0.6;
  background: transparent;
  cursor: pointer;
  font-size: 0.85rem;
  transition: opacity 0.15s, border-color 0.15s, background 0.15s;
}
.ld-add:hover {
  opacity: 1;
  border-color: rgb(var(--v-theme-primary));
  background: rgba(var(--v-theme-primary), 0.05);
}

.ld-savebar {
  display: flex;
  justify-content: flex-end;
  margin-top: 1.25rem;
}
</style>
