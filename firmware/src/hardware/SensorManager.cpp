#include "SensorManager.h"
#include "../OpenFrameConfig.h"
#include "../core/PlatformCompat.h"  // of_i2c_begin — shared I²C bus owner
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SHT31.h>
// Optional/heavy drivers — compiled in only when enabled for this board (see
// OpenFrameConfig.h). Gating the #include strips the library's flash footprint.
#if OF_ENABLE_SENSOR_MPU6050
#include <Adafruit_MPU6050.h>
#endif
#if OF_ENABLE_SENSOR_SGP30
#include <Adafruit_SGP30.h>
#endif
#if OF_ENABLE_SENSOR_VL53L0X
#include <Adafruit_VL53L0X.h>
#endif
#if OF_ENABLE_SENSOR_MAX6675
#include <max6675.h>
#endif
#if OF_ENABLE_SENSOR_SCD4X
#include <SensirionI2CScd4x.h>
#endif
#include <math.h>

namespace {

class Bme280SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_sensor.begin(config.address)) {
            error = "BME280 not found at 0x" + String(config.address, HEX);
            error.toUpperCase();
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "humidity_pct", "pressure_hpa", "altitude_m" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();

        const float temperatureC = _sensor.readTemperature() + _config.temperatureOffsetC;
        const float humidityPct = _sensor.readHumidity();
        const float pressureHpa = _sensor.readPressure() / 100.0f;
        const float altitudeM = _sensor.readAltitude(_config.seaLevelPressureHpa);

        if (isnan(temperatureC) || isnan(humidityPct) || isnan(pressureHpa) || isnan(altitudeM)) {
            error = "BME280 returned invalid readings";
            return false;
        }

        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "humidity_pct", humidityPct });
        values.push_back(SensorMetricValue{ "pressure_hpa", pressureHpa });
        values.push_back(SensorMetricValue{ "altitude_m", altitudeM });
        return true;
    }

private:
    Adafruit_BME280 _sensor;
    SensorConfig    _config;
};

class Bmp280SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_sensor.begin(config.address)) {
            error = "BMP280 not found at 0x" + String(config.address, HEX);
            error.toUpperCase();
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "pressure_hpa", "altitude_m" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();

        const float temperatureC = _sensor.readTemperature() + _config.temperatureOffsetC;
        const float pressureHpa = _sensor.readPressure() / 100.0f;
        const float altitudeM = _sensor.readAltitude(_config.seaLevelPressureHpa);

        if (isnan(temperatureC) || isnan(pressureHpa) || isnan(altitudeM)) {
            error = "BMP280 returned invalid readings";
            return false;
        }

        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "pressure_hpa", pressureHpa });
        values.push_back(SensorMetricValue{ "altitude_m", altitudeM });
        return true;
    }

private:
    Adafruit_BMP280 _sensor;
    SensorConfig    _config;
};

class Dht22SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        (void)error;
        _pin = config.pin;
        _dht.reset(new DHT(_pin, DHT22));
        _dht->begin();
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "humidity_pct" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        // Force a single sensor reading, then read both values from cache
        _dht->read(true);
        const float temperatureC = _dht->readTemperature(false, true);
        const float humidityPct = _dht->readHumidity(true);
        if (isnan(temperatureC) || isnan(humidityPct)) {
            error = "DHT22 returned invalid readings";
            return false;
        }
        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "humidity_pct", humidityPct });
        return true;
    }

private:
    uint8_t              _pin = 0;
    std::unique_ptr<DHT> _dht;
};

class Ds18b20SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _pin = config.pin;
        _oneWire.reset(new OneWire(_pin));
        _dallas.reset(new DallasTemperature(_oneWire.get()));
        _dallas->begin();
        if (_dallas->getDeviceCount() == 0) {
            error = "No DS18B20 found on pin " + String(_pin);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        _dallas->requestTemperatures();
        const float temperatureC = _dallas->getTempCByIndex(0);
        if (temperatureC == DEVICE_DISCONNECTED_C) {
            error = "DS18B20 disconnected";
            return false;
        }
        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        return true;
    }

private:
    uint8_t                               _pin = 0;
    std::unique_ptr<OneWire>              _oneWire;
    std::unique_ptr<DallasTemperature>    _dallas;
};

class Sht31SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_sensor.begin(config.address)) {
            error = "SHT31 not found at 0x" + String(config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "temperature_c", "humidity_pct" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        const float temperatureC = _sensor.readTemperature() + _config.temperatureOffsetC;
        const float humidityPct = _sensor.readHumidity();
        if (isnan(temperatureC) || isnan(humidityPct)) {
            error = "SHT31 returned invalid readings";
            return false;
        }
        values.push_back(SensorMetricValue{ "temperature_c", temperatureC });
        values.push_back(SensorMetricValue{ "humidity_pct", humidityPct });
        return true;
    }

private:
    Adafruit_SHT31 _sensor;
    SensorConfig   _config;
};

class Bh1750SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!_sensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, config.address)) {
            error = "BH1750 not found at 0x" + String(config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "lux" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        float lux = 0.0f;
        if (!_sensor.measurementReady(true) || (lux = _sensor.readLightLevel()) < 0) {
            error = "BH1750 read failed";
            return false;
        }
        values.push_back(SensorMetricValue{ "lux", lux });
        return true;
    }

private:
    BH1750 _sensor;
};

class Ina219SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _sensor.reset(new Adafruit_INA219(config.address));
        if (!_sensor->begin()) {
            error = "INA219 not found at 0x" + String(config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "bus_voltage_v", "shunt_voltage_mv", "current_ma", "power_mw" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        (void)error;
        values.clear();
        values.push_back(SensorMetricValue{ "bus_voltage_v", _sensor->getBusVoltage_V() });
        values.push_back(SensorMetricValue{ "shunt_voltage_mv", _sensor->getShuntVoltage_mV() });
        values.push_back(SensorMetricValue{ "current_ma", _sensor->getCurrent_mA() });
        values.push_back(SensorMetricValue{ "power_mw", _sensor->getPower_mW() });
        return true;
    }

private:
    std::unique_ptr<Adafruit_INA219> _sensor;
};

#if OF_ENABLE_SENSOR_MPU6050
class Mpu6050SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!_mpu.begin(config.address)) {
            error = "MPU6050 not found at 0x" + String(config.address, HEX);
            return false;
        }
        _mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        _mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        _mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
        return true;
    }

    std::vector<String> metricKeys() const override {
        return { "accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z", "temperature_c" };
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        (void)error;
        values.clear();
        sensors_event_t a, g, temp;
        _mpu.getEvent(&a, &g, &temp);
        values.push_back(SensorMetricValue{ "accel_x", a.acceleration.x });
        values.push_back(SensorMetricValue{ "accel_y", a.acceleration.y });
        values.push_back(SensorMetricValue{ "accel_z", a.acceleration.z });
        values.push_back(SensorMetricValue{ "gyro_x", g.gyro.x });
        values.push_back(SensorMetricValue{ "gyro_y", g.gyro.y });
        values.push_back(SensorMetricValue{ "gyro_z", g.gyro.z });
        values.push_back(SensorMetricValue{ "temperature_c", temp.temperature });
        return true;
    }

private:
    Adafruit_MPU6050 _mpu;
};
#endif  // OF_ENABLE_SENSOR_MPU6050

// ── Library-free additions (raw Wire / analogRead / pulseIn / HardwareSerial) ────

#if OF_ENABLE_SENSOR_AHT20
// AHT10/AHT20/AHT21 temperature + humidity over I²C (0x38), raw Wire — no library.
// begin() triggers calibration; each read issues a one-shot measurement.
class Aht20SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _addr    = config.address ? config.address : 0x38;
        _offsetC = config.temperatureOffsetC;
        of_i2c_begin();
        delay(40);
        Wire.beginTransmission(_addr);
        if (Wire.endTransmission() != 0) { error = "AHT20 not found at 0x" + String(_addr, HEX); return false; }
        Wire.beginTransmission(_addr);                 // calibrate: 0xBE 0x08 0x00
        Wire.write(0xBE); Wire.write(0x08); Wire.write(0x00);
        Wire.endTransmission();
        delay(10);
        return true;
    }
    std::vector<String> metricKeys() const override { return { "temperature_c", "humidity_pct" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        Wire.beginTransmission(_addr);                 // trigger: 0xAC 0x33 0x00
        Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);
        if (Wire.endTransmission() != 0) { error = "AHT20 measure cmd failed"; return false; }
        delay(80);
        if (Wire.requestFrom((int)_addr, 6) != 6) { error = "AHT20 read short"; return false; }
        uint8_t d[6];
        for (uint8_t i = 0; i < 6; ++i) d[i] = Wire.read();
        if (d[0] & 0x80) { error = "AHT20 busy"; return false; }   // status bit7 = busy
        uint32_t rawHum = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
        uint32_t rawTmp = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
        values.clear();
        values.push_back({ "temperature_c", (float)rawTmp * 200.0f / 1048576.0f - 50.0f + _offsetC });
        values.push_back({ "humidity_pct",  (float)rawHum * 100.0f / 1048576.0f });
        return true;
    }
private:
    uint8_t _addr    = 0x38;
    float   _offsetC = 0.0f;
};
#endif  // OF_ENABLE_SENSOR_AHT20

#if OF_ENABLE_SENSOR_DHT11
// DHT11 temperature + humidity — same library as DHT22, different chip constant.
class Dht11SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String&) override {
        _dht.reset(new DHT(config.pin, DHT11));
        _dht->begin();
        return true;
    }
    std::vector<String> metricKeys() const override { return { "temperature_c", "humidity_pct" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        _dht->read(true);
        const float t = _dht->readTemperature(false, true);
        const float h = _dht->readHumidity(true);
        if (isnan(t) || isnan(h)) { error = "DHT11 returned invalid readings"; return false; }
        values.clear();
        values.push_back({ "temperature_c", t });
        values.push_back({ "humidity_pct",  h });
        return true;
    }
private:
    std::unique_ptr<DHT> _dht;
};
#endif  // OF_ENABLE_SENSOR_DHT11

#if OF_ENABLE_SENSOR_ANALOG
// Generic analog input: value = analogRead(pin) * scale + offset. Covers soil
// moisture, MQ gas, LDR/photoresistor, potentiometers, voltage dividers, …
class AnalogSensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String&) override {
        _pin = config.pin; _scale = config.scale; _offset = config.offset;
        pinMode(_pin, INPUT);
        return true;
    }
    std::vector<String> metricKeys() const override { return { "value" }; }
    bool read(std::vector<SensorMetricValue>& values, String&) override {
        values.clear();
        values.push_back({ "value", (float)analogRead(_pin) * _scale + _offset });
        return true;
    }
private:
    uint8_t _pin = 0; float _scale = 1.0f; float _offset = 0.0f;
};
#endif  // OF_ENABLE_SENSOR_ANALOG

#if OF_ENABLE_SENSOR_ULTRASONIC
// HC-SR04 ultrasonic range finder: trig = pin, echo = clock_pin. Reports cm.
class UltrasonicSensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!config.clockPin) { error = "ultrasonic needs 'clock_pin' (echo)"; return false; }
        _trig = config.pin; _echo = config.clockPin;
        pinMode(_trig, OUTPUT); digitalWrite(_trig, LOW);
        pinMode(_echo, INPUT);
        return true;
    }
    std::vector<String> metricKeys() const override { return { "distance_cm" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        digitalWrite(_trig, LOW);  delayMicroseconds(3);
        digitalWrite(_trig, HIGH); delayMicroseconds(10);
        digitalWrite(_trig, LOW);
        const unsigned long us = pulseIn(_echo, HIGH, 30000UL);   // ~5 m ceiling
        if (us == 0) { error = "ultrasonic timeout (no echo)"; return false; }
        values.clear();
        values.push_back({ "distance_cm", us / 58.0f });
        return true;
    }
private:
    uint8_t _trig = 0, _echo = 0;
};
#endif  // OF_ENABLE_SENSOR_ULTRASONIC

#if OF_ENABLE_SENSOR_ADS1115
// ADS1115 16-bit I²C ADC (0x48): all 4 single-ended channels as volts (±4.096 V
// FSR). Raw Wire — no library.
class Ads1115SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _addr = config.address ? config.address : 0x48;
        of_i2c_begin();
        Wire.beginTransmission(_addr);
        if (Wire.endTransmission() != 0) { error = "ADS1115 not found at 0x" + String(_addr, HEX); return false; }
        return true;
    }
    std::vector<String> metricKeys() const override { return { "ch0_v", "ch1_v", "ch2_v", "ch3_v" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        static const char* keys[4] = { "ch0_v", "ch1_v", "ch2_v", "ch3_v" };
        values.clear();
        for (uint8_t ch = 0; ch < 4; ++ch) {
            // OS=1 | MUX=100+ch (single-ended) | PGA=001(±4.096V) | MODE=1(single-shot)
            //      | DR=100(128SPS) | COMP_QUE=11(disabled)
            const uint16_t cfg = 0x8000 | ((uint16_t)(0x4 | ch) << 12) | (0x1 << 9)
                               | 0x0100 | (0x4 << 5) | 0x0003;
            Wire.beginTransmission(_addr);
            Wire.write(0x01); Wire.write(cfg >> 8); Wire.write(cfg & 0xFF);
            if (Wire.endTransmission() != 0) { error = "ADS1115 config write failed"; return false; }
            delay(9);                                  // 128 SPS → ~8 ms conversion
            Wire.beginTransmission(_addr);
            Wire.write(0x00);
            if (Wire.endTransmission(false) != 0) { error = "ADS1115 pointer write failed"; return false; }
            if (Wire.requestFrom((int)_addr, 2) != 2) { error = "ADS1115 read short"; return false; }
            const uint8_t hi = Wire.read();            // sequence the two reads explicitly
            const uint8_t lo = Wire.read();
            const int16_t raw = (int16_t)(((uint16_t)hi << 8) | lo);
            values.push_back({ keys[ch], raw * 4.096f / 32768.0f });
        }
        return true;
    }
private:
    uint8_t _addr = 0x48;
};
#endif  // OF_ENABLE_SENSOR_ADS1115

#if OF_ENABLE_SENSOR_CCS811
// CCS811 eCO₂/TVOC air-quality (0x5A) over raw I²C — no library.
class Ccs811SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _addr = config.address ? config.address : 0x5A;
        of_i2c_begin();
        uint8_t id = 0;                                // HW_ID (0x20) must read 0x81
        if (!readReg(0x20, &id, 1) || id != 0x81) { error = "CCS811 not found at 0x" + String(_addr, HEX); return false; }
        Wire.beginTransmission(_addr); Wire.write(0xF4);   // APP_START — leave boot mode
        if (Wire.endTransmission() != 0) { error = "CCS811 app start failed"; return false; }
        delay(20);
        Wire.beginTransmission(_addr); Wire.write(0x01); Wire.write(0x10);  // MEAS_MODE=1 (1 Hz)
        Wire.endTransmission();
        return true;
    }
    std::vector<String> metricKeys() const override { return { "eco2_ppm", "tvoc_ppb" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        uint8_t d[4];
        if (!readReg(0x02, d, 4)) { error = "CCS811 read failed"; return false; }   // ALG_RESULT_DATA
        values.clear();
        values.push_back({ "eco2_ppm", (float)(((uint16_t)d[0] << 8) | d[1]) });
        values.push_back({ "tvoc_ppb", (float)(((uint16_t)d[2] << 8) | d[3]) });
        return true;
    }
private:
    bool readReg(uint8_t reg, uint8_t* buf, uint8_t n) {
        Wire.beginTransmission(_addr); Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom((int)_addr, (int)n) != n) return false;
        for (uint8_t i = 0; i < n; ++i) buf[i] = Wire.read();
        return true;
    }
    uint8_t _addr = 0x5A;
};
#endif  // OF_ENABLE_SENSOR_CCS811

#if OF_ENABLE_SENSOR_UART
// MH-Z19 NDIR CO₂ over UART (9600 8N1). rx_pin/tx_pin wire the hardware UART
// (uart_num) to the sensor. ESP32 family only.
class Mhz19SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!config.rxPin || !config.txPin) { error = "mhz19 needs rx_pin and tx_pin"; return false; }
        _uart.reset(new HardwareSerial(config.uartNum));
        _uart->begin(config.baudRate ? config.baudRate : 9600, SERIAL_8N1,
                     (int8_t)config.rxPin, (int8_t)config.txPin);
        return true;
    }
    std::vector<String> metricKeys() const override { return { "co2_ppm" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        static const uint8_t cmd[9] = { 0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79 };
        while (_uart->available()) _uart->read();      // flush stale bytes
        _uart->write(cmd, 9);
        uint8_t r[9]; uint8_t got = 0; const uint32_t start = millis();
        while (got < 9 && millis() - start < 200) {
            if (_uart->available()) r[got++] = _uart->read();
        }
        if (got < 9 || r[0] != 0xFF || r[1] != 0x86) { error = "mhz19 no/bad response"; return false; }
        values.clear();
        values.push_back({ "co2_ppm", (float)(((uint16_t)r[2] << 8) | r[3]) });
        return true;
    }
private:
    std::unique_ptr<HardwareSerial> _uart;
};

// PMS5003 / PMSx003 particulate matter over UART (9600 8N1). Reports atmospheric
// PM1.0 / PM2.5 / PM10 (µg/m³). ESP32 family only.
class Pms5003SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!config.rxPin) { error = "pms5003 needs rx_pin"; return false; }
        _uart.reset(new HardwareSerial(config.uartNum));
        _uart->begin(config.baudRate ? config.baudRate : 9600, SERIAL_8N1,
                     (int8_t)config.rxPin, config.txPin ? (int8_t)config.txPin : (int8_t)-1);
        return true;
    }
    std::vector<String> metricKeys() const override { return { "pm1_0", "pm2_5", "pm10" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        const uint32_t start = millis();
        while (millis() - start < 1500) {              // hunt for a 0x42 0x4D frame header
            if (_uart->available() < 2) { delay(2); continue; }
            if (_uart->read() != 0x42)  continue;
            if (_uart->peek() != 0x4D)  continue;
            _uart->read();                             // consume 0x4D
            uint8_t b[30]; uint8_t got = 0; const uint32_t t = millis();
            while (got < 30 && millis() - t < 200) { if (_uart->available()) b[got++] = _uart->read(); }
            if (got < 30) { error = "pms5003 short frame"; return false; }
            // b[0..1] = frame length; atmospheric PM at b[8..9], b[10..11], b[12..13].
            values.clear();
            values.push_back({ "pm1_0", (float)(((uint16_t)b[8]  << 8) | b[9])  });
            values.push_back({ "pm2_5", (float)(((uint16_t)b[10] << 8) | b[11]) });
            values.push_back({ "pm10",  (float)(((uint16_t)b[12] << 8) | b[13]) });
            return true;
        }
        error = "pms5003 no frame";
        return false;
    }
private:
    std::unique_ptr<HardwareSerial> _uart;
};
#endif  // OF_ENABLE_SENSOR_UART

}  // namespace

SensorManager& SensorManager::instance() {
    static SensorManager inst;
    return inst;
}

bool SensorManager::begin() {
    registerBuiltInSensors();
    of_i2c_begin();  // start the bus without clobbering pins a display will rebind
    reload();
    LOG_I(TAG, "Initialised (" + String(_sensors.size()) + " sensors active)");
    return true;
}

// Re-read sensors.json and rebuild every sensor. Runs on the loop task and holds the
// lock, so web-task readers never observe a half-rebuilt vector.
void SensorManager::reload() {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    if (!loadConfig()) {
        LOG_W(TAG, "Using empty sensor configuration");
    }
    startConfiguredSensors();
}

void SensorManager::requestReload() {
    _reloadPending = true;   // consumed in loop() on the main task
}

void SensorManager::loop() {
    if (_reloadPending) {
        _reloadPending = false;
        reload();
    }
    const uint32_t nowMs = millis();
    // Per-sensor lock granularity: _sensors is only rebuilt on this (loop) task
    // — reload() runs just above — so iterating it here needs no lock. Taking
    // _mtx around each individual poll (instead of the whole pass) lets the
    // web-task readers (fillConfigJson/fillHealthJson) interleave between
    // sensors rather than stall behind blocking sensor I/O (a DS18B20 sync
    // conversion alone holds ~750 ms).
    for (auto& sensor : _sensors) {
        of_lock_guard<of_recursive_mutex> lk(_mtx);
        pollSensor(sensor, nowMs);
    }
}

void SensorManager::fillConfigJson(JsonArray arr) const {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    for (const auto& inst : _sensors) {
        auto obj = arr.add<JsonObject>();
        obj["id"]               = inst.config.id;
        obj["type"]             = inst.config.type;
        obj["enabled"]          = inst.config.enabled;
        obj["poll_interval_ms"] = inst.config.pollIntervalMs;
        obj["variable_prefix"]  = inst.config.variablePrefix;
        obj["address"]          = inst.config.address;
        obj["pin"]              = inst.config.pin;
    }
}

void SensorManager::fillHealthJson(JsonArray arr, uint32_t& errorTotal) const {
    of_lock_guard<of_recursive_mutex> lk(_mtx);
    errorTotal = 0;
    for (const auto& inst : _sensors) {
        if (!inst.healthy || inst.errorCount > 0) {
            auto obj = arr.add<JsonObject>();
            obj["id"]         = inst.config.id;
            obj["healthy"]    = inst.healthy;
            obj["errorCount"] = inst.errorCount;
            if (inst.lastError.length()) obj["lastError"] = inst.lastError;
            errorTotal += inst.errorCount;
        }
    }
}

void SensorManager::registerSensor(const String& type, SensorFactory factory) {
    if (!type.length() || !factory) return;
    _registry[type] = std::move(factory);
}

// Generic I2C sensor (#22): reads a JSON-defined register map over raw Wire, so
// new chips can be added by editing sensors.json — no new driver/library/reflash.
class GenericI2cDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (_config.registers.empty()) {
            error = "i2c_generic needs a non-empty 'registers' map";
            return false;
        }
        of_i2c_begin();
        Wire.beginTransmission(_config.address);
        if (Wire.endTransmission() != 0) {
            error = "No I2C device at 0x" + String(_config.address, HEX);
            return false;
        }
        return true;
    }

    std::vector<String> metricKeys() const override {
        std::vector<String> keys;
        for (const auto& r : _config.registers) keys.push_back(r.metric);
        return keys;
    }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        for (const auto& r : _config.registers) {
            const uint8_t n = (r.bytes == 1) ? 1 : 2;
            Wire.beginTransmission(_config.address);
            Wire.write(r.reg);
            if (Wire.endTransmission(false) != 0) {  // repeated start
                error = "I2C write failed for register 0x" + String(r.reg, HEX);
                return false;
            }
            if (Wire.requestFrom(static_cast<int>(_config.address), static_cast<int>(n)) != n) {
                error = "I2C read short for register 0x" + String(r.reg, HEX);
                return false;
            }
            uint8_t b0 = Wire.read();
            uint8_t b1 = (n == 2) ? Wire.read() : 0;

            int32_t raw;
            if (n == 1) {
                raw = r.isSigned ? static_cast<int32_t>(static_cast<int8_t>(b0)) : b0;
            } else {
                uint16_t u = r.bigEndian ? ((b0 << 8) | b1) : ((b1 << 8) | b0);
                raw = r.isSigned ? static_cast<int32_t>(static_cast<int16_t>(u)) : u;
            }
            values.push_back(SensorMetricValue{ r.metric, raw * r.scale + r.offset });
        }
        return true;
    }

private:
    SensorConfig _config;
};

// HX711 24-bit load-cell ADC (#18): a simple 2-wire bit-bang protocol, no library.
// `pin` = DT (data), `clock_pin` = SCK. Reports raw counts and a scaled weight
// ((raw - tare_offset) / scale).
class Hx711SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        _config = config;
        if (!_config.clockPin) { error = "HX711 needs 'clock_pin' (SCK)"; return false; }
        pinMode(_config.pin, INPUT);
        pinMode(_config.clockPin, OUTPUT);
        digitalWrite(_config.clockPin, LOW);
        return true;
    }

    std::vector<String> metricKeys() const override { return { "raw", "weight" }; }

    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        values.clear();
        // Data low = conversion ready. Bail rather than block forever if it's not.
        uint32_t start = millis();
        while (digitalRead(_config.pin) == HIGH) {
            if (millis() - start > 200) { error = "HX711 not ready (check wiring/power)"; return false; }
            delay(1);
        }
        int32_t raw = 0;
        noInterrupts();
        for (uint8_t i = 0; i < 24; ++i) {
            digitalWrite(_config.clockPin, HIGH);
            delayMicroseconds(1);
            raw = (raw << 1) | (digitalRead(_config.pin) ? 1 : 0);
            digitalWrite(_config.clockPin, LOW);
            delayMicroseconds(1);
        }
        // 25th pulse selects channel A, gain 128 for the next reading.
        digitalWrite(_config.clockPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(_config.clockPin, LOW);
        interrupts();

        if (raw & 0x800000) raw |= ~0xFFFFFF;  // sign-extend 24-bit two's complement
        const float scale = _config.scale != 0.0f ? _config.scale : 1.0f;
        values.push_back(SensorMetricValue{ "raw", static_cast<float>(raw) });
        values.push_back(SensorMetricValue{ "weight", (raw - _config.tareOffset) / scale });
        return true;
    }

private:
    SensorConfig _config;
};

// SGP30 air-quality (#17): eCO2 + TVOC over I²C.
#if OF_ENABLE_SENSOR_SGP30
class Sgp30SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig&, String& error) override {
        if (!_sensor.begin()) { error = "SGP30 not found"; return false; }
        return true;
    }
    std::vector<String> metricKeys() const override { return { "eco2_ppm", "tvoc_ppb" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        if (!_sensor.IAQmeasure()) { error = "SGP30 measure failed"; return false; }
        values.clear();
        values.push_back({ "eco2_ppm", static_cast<float>(_sensor.eCO2) });
        values.push_back({ "tvoc_ppb", static_cast<float>(_sensor.TVOC) });
        return true;
    }
private:
    Adafruit_SGP30 _sensor;
};
#endif  // OF_ENABLE_SENSOR_SGP30

// VL53L0X time-of-flight distance (#17).
#if OF_ENABLE_SENSOR_VL53L0X
class Vl53l0xSensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!_sensor.begin(config.address ? config.address : 0x29)) {
            error = "VL53L0X not found";
            return false;
        }
        return true;
    }
    std::vector<String> metricKeys() const override { return { "distance_mm" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        VL53L0X_RangingMeasurementData_t m;
        _sensor.rangingTest(&m, false);
        if (m.RangeStatus == 4) { error = "VL53L0X out of range"; return false; }
        values.clear();
        values.push_back({ "distance_mm", static_cast<float>(m.RangeMilliMeter) });
        return true;
    }
private:
    Adafruit_VL53L0X _sensor;
};
#endif  // OF_ENABLE_SENSOR_VL53L0X

// MAX6675 K-type thermocouple (#17): SPI-ish — SCK=clock_pin, CS=cs_pin, SO=pin.
#if OF_ENABLE_SENSOR_MAX6675
class Max6675SensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig& config, String& error) override {
        if (!config.clockPin || !config.csPin || !config.pin) {
            error = "MAX6675 needs clock_pin (SCK), cs_pin (CS) and pin (SO)";
            return false;
        }
        _sensor.reset(new MAX6675(config.clockPin, config.csPin, config.pin));
        return true;
    }
    std::vector<String> metricKeys() const override { return { "temperature_c" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        const float c = _sensor->readCelsius();
        if (isnan(c)) { error = "MAX6675 read failed (open thermocouple?)"; return false; }
        values.clear();
        values.push_back({ "temperature_c", c });
        return true;
    }
private:
    std::unique_ptr<MAX6675> _sensor;
};
#endif  // OF_ENABLE_SENSOR_MAX6675

// Sensirion SCD40/41 CO₂ (#17): CO₂ + temperature + humidity over I²C.
#if OF_ENABLE_SENSOR_SCD4X
class Scd4xSensorDriver final : public SensorDriver {
public:
    bool begin(const SensorConfig&, String& error) override {
        _sensor.begin(Wire);
        _sensor.stopPeriodicMeasurement();              // ensure idle before (re)start
        if (_sensor.startPeriodicMeasurement()) { error = "SCD4x start failed"; return false; }
        return true;
    }
    std::vector<String> metricKeys() const override { return { "co2_ppm", "temperature_c", "humidity_pct" }; }
    bool read(std::vector<SensorMetricValue>& values, String& error) override {
        uint16_t co2 = 0; float temp = 0, hum = 0;
        bool ready = false;
        if (_sensor.getDataReadyFlag(ready) || !ready) { error = "SCD4x not ready"; return false; }
        if (_sensor.readMeasurement(co2, temp, hum) || co2 == 0) { error = "SCD4x read failed"; return false; }
        values.clear();
        values.push_back({ "co2_ppm", static_cast<float>(co2) });
        values.push_back({ "temperature_c", temp });
        values.push_back({ "humidity_pct", hum });
        return true;
    }
private:
    SensirionI2CScd4x _sensor;
};
#endif  // OF_ENABLE_SENSOR_SCD4X

void SensorManager::registerBuiltInSensors() {
#if OF_ENABLE_SENSOR_SGP30
    if (!_registry.count("sgp30")) {
        registerSensor("sgp30", []() { return std::unique_ptr<SensorDriver>(new Sgp30SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_VL53L0X
    if (!_registry.count("vl53l0x")) {
        registerSensor("vl53l0x", []() { return std::unique_ptr<SensorDriver>(new Vl53l0xSensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_MAX6675
    if (!_registry.count("max6675")) {
        registerSensor("max6675", []() { return std::unique_ptr<SensorDriver>(new Max6675SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_SCD4X
    if (!_registry.count("scd4x")) {
        registerSensor("scd4x", []() { return std::unique_ptr<SensorDriver>(new Scd4xSensorDriver()); });
    }
#endif

    if (!_registry.count("hx711")) {
        registerSensor("hx711", []() { return std::unique_ptr<SensorDriver>(new Hx711SensorDriver()); });
    }

    if (!_registry.count("i2c_generic")) {
        registerSensor("i2c_generic", []() {
            return std::unique_ptr<SensorDriver>(new GenericI2cDriver());
        });
    }

    if (!_registry.count("bme280")) {
        registerSensor("bme280", []() {
            return std::unique_ptr<SensorDriver>(new Bme280SensorDriver());
        });
    }

    if (!_registry.count("bmp280")) {
        registerSensor("bmp280", []() {
            return std::unique_ptr<SensorDriver>(new Bmp280SensorDriver());
        });
    }

    if (!_registry.count("dht22")) {
        registerSensor("dht22", []() { return std::unique_ptr<SensorDriver>(new Dht22SensorDriver()); });
    }
    if (!_registry.count("ds18b20")) {
        registerSensor("ds18b20", []() { return std::unique_ptr<SensorDriver>(new Ds18b20SensorDriver()); });
    }
    if (!_registry.count("sht31")) {
        registerSensor("sht31", []() { return std::unique_ptr<SensorDriver>(new Sht31SensorDriver()); });
    }
    if (!_registry.count("bh1750")) {
        registerSensor("bh1750", []() { return std::unique_ptr<SensorDriver>(new Bh1750SensorDriver()); });
    }
    if (!_registry.count("ina219")) {
        registerSensor("ina219", []() { return std::unique_ptr<SensorDriver>(new Ina219SensorDriver()); });
    }
#if OF_ENABLE_SENSOR_MPU6050
    if (!_registry.count("mpu6050")) {
        registerSensor("mpu6050", []() { return std::unique_ptr<SensorDriver>(new Mpu6050SensorDriver()); });
    }
#endif

    // ── Library-free additions ──────────────────────────────────────────────────
#if OF_ENABLE_SENSOR_AHT20
    if (!_registry.count("aht20")) {
        registerSensor("aht20", []() { return std::unique_ptr<SensorDriver>(new Aht20SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_DHT11
    if (!_registry.count("dht11")) {
        registerSensor("dht11", []() { return std::unique_ptr<SensorDriver>(new Dht11SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_ANALOG
    if (!_registry.count("analog")) {
        registerSensor("analog", []() { return std::unique_ptr<SensorDriver>(new AnalogSensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_ULTRASONIC
    if (!_registry.count("ultrasonic")) {
        registerSensor("ultrasonic", []() { return std::unique_ptr<SensorDriver>(new UltrasonicSensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_ADS1115
    if (!_registry.count("ads1115")) {
        registerSensor("ads1115", []() { return std::unique_ptr<SensorDriver>(new Ads1115SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_CCS811
    if (!_registry.count("ccs811")) {
        registerSensor("ccs811", []() { return std::unique_ptr<SensorDriver>(new Ccs811SensorDriver()); });
    }
#endif
#if OF_ENABLE_SENSOR_UART
    if (!_registry.count("mhz19")) {
        registerSensor("mhz19", []() { return std::unique_ptr<SensorDriver>(new Mhz19SensorDriver()); });
    }
    if (!_registry.count("pms5003")) {
        registerSensor("pms5003", []() { return std::unique_ptr<SensorDriver>(new Pms5003SensorDriver()); });
    }
#endif
}

bool SensorManager::loadConfig() {
    _configs.clear();

    JsonDocument doc;
    if (!StorageManager::instance().readJson(OF_SENSORS_PATH, doc)) {
        return false;
    }
    if (!doc["sensors"].is<JsonArray>()) {
        return true;
    }

    std::map<String, uint16_t> idUsage;
    auto ensureUniqueId = [&](const String& requested, const String& fallbackPrefix) {
        String base = requested;
        if (!base.length()) base = fallbackPrefix;
        if (!idUsage.count(base)) {
            idUsage[base] = 1;
            return base;
        }

        uint16_t suffix = idUsage[base];
        String candidate;
        do {
            candidate = base + "_" + String(suffix++);
        } while (idUsage.count(candidate));
        idUsage[base] = suffix;
        idUsage[candidate] = 1;
        LOG_W(TAG, "Duplicate sensor id '" + base + "' remapped to '" + candidate + "'");
        return candidate;
    };

    for (auto item : doc["sensors"].as<JsonArray>()) {
        SensorConfig cfg;
        cfg.type               = item["type"] | String("");
        cfg.type.toLowerCase();
        if (!cfg.type.length()) continue;

        cfg.id                 = item["id"] | String("");
        cfg.enabled            = item["enabled"] | true;
        cfg.pollIntervalMs     = item["poll_interval_ms"] | 5000;
        cfg.variablePrefix     = item["variable_prefix"] | String("");
        cfg.address             = item["address"] | 0x76;
        cfg.temperatureOffsetC  = item["temperature_offset_c"] | 0.0f;
        cfg.seaLevelPressureHpa = item["sea_level_pressure_hpa"] | 1013.25f;
        cfg.pin                 = item["pin"] | 0;
        cfg.clockPin            = item["clock_pin"] | 0;
        cfg.csPin               = item["cs_pin"] | 0;
        cfg.scale               = item["scale"] | 1.0f;
        cfg.tareOffset          = item["tare_offset"] | 0;
        cfg.offset              = item["offset"] | 0.0f;
        cfg.rxPin               = item["rx_pin"] | 0;
        cfg.txPin               = item["tx_pin"] | 0;
        cfg.uartNum             = item["uart_num"] | 1;
        cfg.baudRate            = item["baud_rate"] | 9600;
        if (item["registers"].is<JsonArrayConst>()) {
            for (JsonObjectConst r : item["registers"].as<JsonArrayConst>()) {
                RegisterSpec spec;
                spec.metric    = r["metric"] | String("");
                spec.reg       = r["reg"] | 0;
                spec.bytes     = r["bytes"] | 2;
                spec.bigEndian = r["big_endian"] | true;
                spec.isSigned  = r["signed"] | false;
                spec.scale     = r["scale"] | 1.0f;
                spec.offset    = r["offset"] | 0.0f;
                if (spec.metric.length()) cfg.registers.push_back(spec);
            }
        }
        cfg.id = ensureUniqueId(cfg.id, cfg.type);
        _configs.push_back(cfg);
    }

    return true;
}

void SensorManager::startConfiguredSensors() {
    of_lock_guard<of_recursive_mutex> lk(_mtx);

    // Purge the previous generation's mirror variables before re-defining, so a
    // sensor deleted/renamed by the hot reload doesn't leave frozen sensor.*
    // values behind until reboot. Each old instance is purged via its actual
    // prefix (covers a custom variable_prefix). Deliberately NO blanket
    // "sensor." sweep: that namespace isn't exclusively ours (an HA import's
    // user-chosen variable_id may live there), and non-persistent variables
    // don't survive a reboot, so the per-instance purge already covers every
    // variable this manager ever defined.
    auto& vm = VariableManager::instance();
    for (const auto& old : _sensors) {
        vm.removeByPrefix(variablePrefixFor(old.config) + ".");
    }

    _sensors.clear();

    for (const auto& cfg : _configs) {
        if (!cfg.enabled) continue;

        auto factoryIt = _registry.find(cfg.type);
        if (factoryIt == _registry.end()) {
            LOG_W(TAG, "No registered driver for sensor type '" + cfg.type + "'");
            continue;
        }

        SensorInstance sensor;
        sensor.config = cfg;
        sensor.driver = factoryIt->second();

        String error;
        if (!sensor.driver || !sensor.driver->begin(sensor.config, error)) {
            LOG_W(TAG, "Failed to start sensor '" + cfg.id + "': " + error);
            publishError(cfg, error, millis());
            continue;
        }

        defineVariables(sensor);
        _sensors.push_back(std::move(sensor));
        LOG_I(TAG, "Registered sensor '" + cfg.id + "' (" + cfg.type + ")");
    }
}

String SensorManager::variablePrefixFor(const SensorConfig& config) const {
    if (config.variablePrefix.length()) return config.variablePrefix;
    return "sensor." + config.id;
}

void SensorManager::defineVariables(const SensorInstance& sensor) {
    const String prefix = variablePrefixFor(sensor.config);
    for (const auto& metric : sensor.driver->metricKeys()) {
        VariableManager::instance().define(prefix + "." + metric, VarType::Float, sensor.config.id + " " + metric, false);
    }
}

void SensorManager::pollSensor(SensorInstance& sensor, uint32_t nowMs) {
    if (sensor.lastPollMs > 0 && (nowMs - sensor.lastPollMs) < sensor.config.pollIntervalMs) {
        return;
    }
    sensor.lastPollMs = nowMs;

    std::vector<SensorMetricValue> values;
    String error;
    if (!sensor.driver->read(values, error)) {
        LOG_W(TAG, "Read failed for sensor '" + sensor.config.id + "': " + error);
        sensor.errorCount++;
        sensor.lastError = error;
        sensor.healthy   = false;
        publishError(sensor.config, error, nowMs);
        return;
    }

    sensor.healthy   = true;
    sensor.lastError = "";
    publishReading(sensor, values, nowMs);
}

void SensorManager::publishReading(const SensorInstance& sensor, const std::vector<SensorMetricValue>& values, uint32_t nowMs) {
    JsonDocument doc;
    doc["id"] = sensor.config.id;
    doc["type"] = sensor.config.type;
    doc["timestamp_ms"] = nowMs;
    auto valueObj = doc["values"].to<JsonObject>();

    const String prefix = variablePrefixFor(sensor.config);
    for (const auto& metric : values) {
        valueObj[metric.key] = metric.value;
        VariableManager::instance().setFloat(prefix + "." + metric.key, metric.value);
    }

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::SensorValueUpdated, sensor.config.id, payload);
}

void SensorManager::publishError(const SensorConfig& config, const String& error, uint32_t nowMs) {
    JsonDocument doc;
    doc["id"] = config.id;
    doc["type"] = config.type;
    doc["error"] = error;
    doc["timestamp_ms"] = nowMs;

    String payload;
    serializeJson(doc, payload);
    EventBus::instance().publish(EventType::SensorError, config.id, payload);
}
