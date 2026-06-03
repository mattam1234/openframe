# OpenFrame Project Specification

## Project Overview

OpenFrame is a modular ESP32-based hardware automation and control platform.

The goal is to provide a configurable framework that allows users to connect sensors, buttons, displays, touchscreens, and expansion modules while integrating with OpenDeck, Home Assistant, MQTT, and external systems.

Users must be able to configure the entire device through a web interface without modifying firmware.

OpenFrame should support both simple control panels and large modular automation systems.

The project must be designed from the beginning to be modular, extensible, and plugin-oriented.

## Primary Goals

- Support ESP32 and ESP32-S3 devices.
- Support displays, touchscreens, sensors, and outputs.
- Support OpenDeck integration.
- Support Home Assistant integration.
- Support MQTT integration.
- Support OTA firmware updates.
- Support expansion modules.
- Support visual configuration through a web UI.
- Support device templates.
- Support multiple profiles.
- Support future plugin development.
- Support USB HID on compatible ESP32-S3 devices.

## Core Architecture

The firmware should be split into independent modules.

### Core System

- Configuration Manager
- Logger
- Event Bus
- Variable Manager
- Action Engine
- Storage Manager
- OTA Manager
- WiFi Manager
- MQTT Manager
- Home Assistant Manager

### Hardware Layer

- Input Manager
- Output Manager
- Sensor Manager
- Display Manager
- Touch Manager
- Module Manager

### User Interface Layer

- Dashboard
- Device Layout Designer
- Screen Designer
- Action Editor
- Sensor Manager
- Module Manager
- Logs Viewer
- Settings

### Communication Layer

- REST API
- WebSocket API
- MQTT
- Home Assistant Discovery

### Storage Layer

- LittleFS
- JSON configuration files

## Supported Input Devices

### Digital Inputs

- Push buttons
- Toggle switches
- Keypads
- Rotary encoder buttons

Supported events:

- Press
- Release
- Hold
- Long press
- Double press
- Triple press
- Repeat press

### Analog Inputs

- Potentiometers
- Sliders
- Joysticks
- LDR sensors
- Hall sensors
- Generic analog sensors

Supported events:

- Value changed
- Threshold reached
- Range entered
- Range exited

### Environmental Sensors

Initial support should be designed for:

- BME280
- BMP280
- DHT22
- DS18B20
- SHT31
- BH1750
- INA219
- MPU6050

Sensor support must be extensible.

## Supported Output Devices

- LEDs
- RGB LEDs
- WS2812 LEDs
- Relays
- Buzzers

## Display Support

Display support must be abstracted through a display provider system.

### Initial Display Support

- SSD1306
- SH1106
- SSD1327

### TFT Support

- ST7789
- ILI9341
- ILI9488

### Smart Display Support

- Nextion Basic
- Nextion Enhanced
- Nextion Intelligent

The NX4827T043_011 Nextion Enhanced display should be considered a primary supported device.

Display features:

- Text
- Icons
- Buttons
- Gauges
- Progress bars
- Status indicators
- Sensor values
- Graphs
- Images
- Multiple pages

Displays must be configurable through the web UI.

No display layouts should be hardcoded.

### Nextion Integration

Nextion displays must be treated as smart displays.

Features:

- UART communication
- Page synchronization
- Component binding
- Touch event support
- Live component updates
- Debug logging

Future features:

- Nextion project import
- Nextion simulator

## Touchscreen Support

Touchscreens must support:

- Button widgets
- Sliders
- Page navigation
- Gesture support
- Dynamic content

Touch actions must be able to trigger:

- OpenDeck actions
- Home Assistant services
- MQTT messages
- HTTP requests
- Internal actions
- Macros

## Variable System

OpenFrame must contain a global variable system.

Variables can be:

- Integer
- Float
- Boolean
- String

Variables can be read and written by:

- Sensors
- Actions
- Displays
- Home Assistant
- MQTT
- Internal logic

Example use cases:

- Current page
- Volume level
- Temperature
- Device state

## Action Engine

The action engine is one of the most important systems.

Supported actions:

- Keyboard shortcuts
- Media controls
- Mouse actions
- Text entry
- Delays
- HTTP requests
- MQTT publish
- Variable operations
- Home Assistant services
- Page changes
- Notifications

Actions must support chaining.

Actions must support conditions.

Examples:

- If temperature is greater than 25 then switch to cooling page.
- If button is pressed then execute macro.

## Macro System

Users must be able to create macros.

A macro contains multiple actions executed in sequence.

Future feature:

- Macro recorder

## Home Assistant Integration

OpenFrame must support Home Assistant MQTT Discovery.

Supported entity types:

- Sensors
- Binary sensors
- Buttons
- Switches
- Select entities
- Number entities
- Text entities

Home Assistant must be able to:

- Read sensor values
- Trigger actions
- Change pages
- Control outputs
- Update variables

Home Assistant entities must be available as data sources for displays.

## MQTT Integration

Support:

- Publishing sensor values
- Receiving commands
- Triggering actions
- Variable synchronization
- Home Assistant discovery

## USB HID Support

For ESP32-S3 devices.

Future support for:

- Keyboard
- Mouse
- Media controller
- Game controller

This allows OpenFrame devices to function without OpenDeck when connected directly to a PC.

## Device Layout Designer

The web UI must allow users to create a visual representation of their physical device.

Users should be able to place:

- Displays
- Buttons
- Potentiometers
- Encoders
- Sensors
- LEDs
- Expansion modules

The layout should match the real-world hardware layout.

The layout should update in real time.

Selecting an item should display:

- Current state
- Current value
- Configuration
- Diagnostics

## Screen Designer

The web UI must include a screen designer.

Users should be able to create display pages visually.

Supported widgets:

- Text
- Icons
- Buttons
- Progress bars
- Gauges
- Graphs
- Sensor values
- Home Assistant values

Widgets must be bindable to variables and sensors.

## Screen Simulator

The web UI should include a simulator.

Users should be able to preview displays without uploading firmware.

The simulator should support:

- OLED displays
- TFT displays
- Nextion displays

## Sensor Dashboard

The web UI must provide a sensor dashboard.

Features:

- Live sensor values
- Historical graphs
- Min and max values
- Export capability

Updates should use WebSocket communication.

## Module System

Expansion modules communicate primarily over I2C.

Future expansion support:

- UART
- RS485
- CAN Bus

Modules must support auto-discovery.

Supported module types:

- Buttons
- Potentiometers
- Encoders
- Displays
- Touchscreens
- Sensor packs
- Relay boards
- LED controllers

The module system must be extensible.

## Plugin Architecture

The project should be designed so new functionality can be added through plugins.

Future plugin categories:

- Sensors
- Displays
- Actions
- Communication protocols
- Expansion modules

Core systems should expose registration interfaces for future plugin support.

## Device Templates

Users must be able to save and load complete device templates.

Templates include:

- Device layout
- Display pages
- Actions
- Variables
- Sensor mappings
- Home Assistant mappings

Templates should be importable and exportable.

## Profiles

Users must be able to create multiple profiles.

Examples:

- Gaming
- Productivity
- Home Automation

Profiles should allow switching configurations without reflashing firmware.

## Logging System

Logging must be available from the beginning.

Log levels:

- Trace
- Debug
- Info
- Warning
- Error
- Fatal

Logs must be available through:

- Serial
- Web UI
- Action History

The system must maintain a history of actions and events.

Examples:

- Button presses
- Sensor triggers
- Home Assistant calls
- Display events
- Macros

Store at least the latest 1000 events in a ring buffer.

## Device Health Monitoring

Monitor:

- Heap usage
- Memory usage
- CPU load
- WiFi signal strength
- I2C errors
- Sensor failures
- Reboot reason
- Uptime

Display health information in the dashboard.

## Notification System

Support notifications displayed on supported screens.

Examples:

- Firmware update available
- Sensor disconnected
- WiFi disconnected
- Home Assistant disconnected
- MQTT disconnected

## WiFi Management

On first boot:

- Start Access Point mode
- Launch captive portal

Default SSID format:

- OpenFrame-XXXX

Normal operation:

- Connect to configured WiFi
- Retry on failure
- Fall back to Access Point mode if connection fails

## OTA Updates

Support:

- Local firmware upload
- Remote update checks
- GitHub release integration
- Rollback support

Display update progress in the web UI.

## Storage

Use LittleFS.

Store:

- Settings
- Layouts
- Profiles
- Display pages
- Templates
- Variables
- Logs
- Action history

Use JSON for configuration storage.

## Web UI

### Dashboard

- Device status
- Memory usage
- CPU usage
- Uptime
- Connected modules
- Firmware version
- WiFi status

### Device Layout Designer

- Visual hardware editor
- Real-time state updates

### Screen Designer

- Visual display editor
- Widget configuration

### Sensor Dashboard

- Live values
- Graphs

### Action Manager

- Action creation
- Macro management
- Conditions

### Module Manager

- Module discovery
- Diagnostics

### Home Assistant Manager

- Discovery configuration
- Entity mapping

### Logs Viewer

- Debug logs
- Action history

### Settings

- WiFi
- MQTT
- OTA
- System configuration

## Recommended Technology Stack

### Firmware

- ESP32
- ESP32-S3
- PlatformIO
- Arduino Framework
- FreeRTOS
- LittleFS
- ArduinoJson
- AsyncTCP
- ESPAsyncWebServer
- ElegantOTA

### Frontend

- Vue 3
- Pinia
- Vuetify

### Communication

- REST API
- WebSocket API
- MQTT

## Version 1.0 Minimum Viable Product

Version 1.0 must include:

- ESP32 support
- ESP32-S3 support
- WiFi configuration
- Captive portal
- OTA updates
- SSD1306 support
- Nextion NX4827T043_011 support
- Buttons
- Potentiometers
- LDR sensors
- Variable system
- Action engine
- Conditional actions
- Macro support
- Home Assistant MQTT Discovery
- MQTT integration
- Device Layout Designer
- Screen Designer
- Sensor Dashboard
- Action History
- Debug Logging
- I2C Module Support
- Device Templates
- Device Health Monitoring
- WebSocket live updates
- JSON configuration storage
- LittleFS storage

The architecture should prioritize modularity, maintainability, and future expansion over rapid implementation. Every major subsystem should be designed with clear interfaces so additional sensors, displays, actions, protocols, and modules can be added without modifying the core framework.
