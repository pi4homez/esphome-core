//
//  bme680_component.cpp
//  esphomelib
//
//  Created by Otto Winter on 10.05.18.
//  Copyright © 2018 Otto Winter. All rights reserved.
//
// Based on:
//   - https://cdn-shop.adafruit.com/product-files/3660/BME680.pdf
//   - https://github.com/adafruit/Adafruit_BME680
//   - https://github.com/BoschSensortec/BME680_driver
//   - https://github.com/pimoroni/bme680
//   - https://github.com/closedcube/ClosedCube_BME680_Arduino

#include "esphomelib/sensor/bme680_component.h"
#include "esphomelib/log.h"

#ifdef USE_BME680

ESPHOMELIB_NAMESPACE_BEGIN

namespace sensor {

static const char *TAG = "sensor.bme680";

static const uint8_t BME680_REGISTER_COEFF1 = 0x89;
static const uint8_t BME680_REGISTER_COEFF2 = 0xE1;

static const uint8_t BME680_REGISTER_CONFIG = 0x75;
static const uint8_t BME680_REGISTER_CONTROL_MEAS = 0x74;
static const uint8_t BME680_REGISTER_CONTROL_HUMIDITY = 0x72;
static const uint8_t BME680_REGISTER_CONTROL_GAS1 = 0x71;
static const uint8_t BME680_REGISTER_CONTROL_GAS0 = 0x70;
static const uint8_t BME680_REGISTER_HEATER_HEAT0 = 0x5A;
static const uint8_t BME680_REGISTER_HEATER_WAIT0 = 0x64;

static const uint8_t BME680_REGISTER_CHIPID = 0xD0;

static const uint8_t BME680_REGISTER_FIELD0 = 0x1D;

float BME680_GAS_LOOKUP_TABLE_1[16] PROGMEM = {
    0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, -0.8,
    0.0, 0.0, -0.2, -0.5, 0.0, -1.0, 0.0, 0.0
};

float BME680_GAS_LOOKUP_TABLE_2[16] PROGMEM = {
    0.0, 0.0, 0.0, 0.0, 0.1, 0.7, 0.0, -0.8,
    -0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

static const char* oversampling_to_str(BME680Oversampling oversampling) {
  switch (oversampling) {
    case BME680_OVERSAMPLING_NONE: return "None";
    case BME680_OVERSAMPLING_1X: return "1x";
    case BME680_OVERSAMPLING_2X: return "2x";
    case BME680_OVERSAMPLING_4X: return "4x";
    case BME680_OVERSAMPLING_8X: return "8x";
    case BME680_OVERSAMPLING_16X: return "16x";
    default: return "UNKNOWN";
  }
}

static const char* iir_filter_to_str(BME680IIRFilter filter) {
  switch (filter) {
    case BME680_IIR_FILTER_OFF: return "OFF";
    case BME680_IIR_FILTER_1X: return "1x";
    case BME680_IIR_FILTER_3X: return "3x";
    case BME680_IIR_FILTER_7X: return "7x";
    case BME680_IIR_FILTER_15X: return "15x";
    case BME680_IIR_FILTER_31X: return "31x";
    case BME680_IIR_FILTER_63X: return "63x";
    case BME680_IIR_FILTER_127X: return "127x";
    default: return "UNKNOWN";
  }
}

void BME680Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up BME680...");
  uint8_t chip_id;
  if (!this->read_byte(BME680_REGISTER_CHIPID, &chip_id) || chip_id != 0x61) {
    ESP_LOGE(TAG, "Communication with BME680 failed!");
    this->mark_failed();
    return;
  }

  // Read calibration
  uint8_t cal1[25];
  this->read_bytes(BME680_REGISTER_COEFF1, cal1, 25);
  uint8_t cal2[16];
  this->read_bytes(BME680_REGISTER_COEFF2, cal2, 16);

  this->calibration_.t1 = cal2[9] << 8 | cal2[8];
  this->calibration_.t2 = cal1[2] << 8 | cal1[1];
  this->calibration_.t3 = cal1[3];

  this->calibration_.h1 = cal2[2] << 4 | (cal2[1] & 0x0F);
  this->calibration_.h2 = cal2[0] << 4 | cal2[1];
  this->calibration_.h3 = cal2[3];
  this->calibration_.h4 = cal2[4];
  this->calibration_.h5 = cal2[5];
  this->calibration_.h6 = cal2[6];
  this->calibration_.h7 = cal2[7];

  this->calibration_.p1 = cal1[6] << 8 | cal1[5];
  this->calibration_.p2 = cal1[8] << 8 | cal1[7];
  this->calibration_.p3 = cal1[9];
  this->calibration_.p4 = cal1[12] << 8 | cal1[11];
  this->calibration_.p5 = cal1[14] << 8 | cal1[13];
  this->calibration_.p6 = cal1[16];
  this->calibration_.p7 = cal1[15];
  this->calibration_.p8 = cal1[20] << 8 | cal1[19];
  this->calibration_.p9 = cal1[22] << 8 | cal1[21];
  this->calibration_.p10 = cal1[23];

  this->calibration_.gh1 = cal2[14];
  this->calibration_.gh2 = cal2[12] << 8 | cal2[13];
  this->calibration_.gh3 = cal2[15];

  this->read_byte(0x02, &this->calibration_.res_heat_range);
  this->read_byte(0x00, &this->calibration_.res_heat_val);
  this->read_byte(0x04, &this->calibration_.range_sw_err);

  this->calibration_.ambient_temperature = 25; // prime ambient temperature

  ESP_LOGCONFIG(TAG, "    Temperature Oversampling: %s", oversampling_to_str(this->temperature_oversampling_));
  ESP_LOGCONFIG(TAG, "    Pressure Oversampling: %s", oversampling_to_str(this->pressure_oversampling_));
  ESP_LOGCONFIG(TAG, "    Humidity Oversampling: %s", oversampling_to_str(this->humidity_oversampling_));
  ESP_LOGCONFIG(TAG, "    IIR Filter: %s", iir_filter_to_str(this->iir_filter_));
  if (this->heater_duration_ == 0 || this->heater_temperature_ == 0) {
    ESP_LOGCONFIG(TAG, "    Heater OFF");
  } else {
    ESP_LOGCONFIG(TAG, "    Heater temperature=%u°C duration=%ums",
                  this->heater_temperature_, this->heater_duration_);
  }

  // Config register
  uint8_t config_register;
  this->read_byte(BME680_REGISTER_CONFIG, &config_register);
  config_register &= ~0b00011100;
  config_register |= (this->iir_filter_ & 0b111) << 2;
  this->write_byte(BME680_REGISTER_CONFIG, config_register);

  // Humidity control register
  uint8_t hum_control;
  this->read_byte(BME680_REGISTER_CONTROL_HUMIDITY, &hum_control);
  hum_control &= ~0b00000111;
  hum_control |= this->humidity_oversampling_ & 0b111;
  this->write_byte(BME680_REGISTER_CONTROL_HUMIDITY, hum_control);

  // Gas 1 control register
  uint8_t gas1_control;
  this->read_byte(BME680_REGISTER_CONTROL_GAS1, &gas1_control);
  gas1_control &= ~0b00011111;
  gas1_control |= 1 << 4;
  gas1_control |= 0; // profile 0
  this->write_byte(BME680_REGISTER_CONTROL_GAS1, gas1_control);

  const bool heat_off = this->heater_temperature_ == 0 || this->heater_duration_ == 0;

  // Gas 0 control register
  uint8_t gas0_control;
  this->read_byte(BME680_REGISTER_CONTROL_GAS0, &gas0_control);
  gas0_control &= ~0b00001000;
  gas0_control |= heat_off ? 0b100 : 0b000;
  this->write_byte(BME680_REGISTER_CONTROL_GAS0, gas0_control);

  if (!heat_off) {
    // Gas Heater Temperature
    uint8_t temperature = this->calc_heater_resistance_(this->heater_temperature_);
    this->write_byte(BME680_REGISTER_HEATER_HEAT0, temperature);

    // Gas Heater Duration
    uint8_t duration = this->calc_heater_duration_(this->heater_duration_);

    this->write_byte(BME680_REGISTER_HEATER_WAIT0, duration);
  }
}

float BME680Component::get_setup_priority() const {
  return setup_priority::HARDWARE_LATE;
}

void BME680Component::update() {
  uint8_t meas_control = 0; // No need to fetch, we're setting all fields
  meas_control |= (this->temperature_oversampling_ & 0b111) << 5;
  meas_control |= (this->pressure_oversampling_ & 0b111) << 5;
  meas_control |= 0b01; // forced mode
  this->write_byte(BME680_REGISTER_CONTROL_MEAS, meas_control);

  this->set_timeout("data", this->calc_meas_duration_(), [this]() {
    this->read_data_();
  });
}

uint8_t BME680Component::calc_heater_resistance_(uint16_t temperature) {
  float var1 = 0;
  float var2 = 0;
  float var3 = 0;
  float var4 = 0;
  float var5 = 0;
  uint8_t res_heat = 0;

  if (temperature < 200) temperature = 200;
  if (temperature > 400) temperature = 400;

  const float ambient_temperature = this->calibration_.ambient_temperature;
  const float gh1 = this->calibration_.gh1;
  const float gh2 = this->calibration_.gh2;
  const float gh3 = this->calibration_.gh3;
  const float res_heat_range = this->calibration_.res_heat_range;
  const float res_heat_val = this->calibration_.res_heat_val;

  var1 = (gh1 / 16.0f) + 49.0f;
  var2 = ((gh2 / 32768.0f) * 0.0005f) + 0.00235f;
  var3 = gh3 / 1024.0f;
  var4 = var1 * (1.0f + var2 * float(temperature));
  var5 = var4 + var3 * ambient_temperature;
  res_heat = 3.4f * ((var5 * (4 / (4 + res_heat_range)) *
      (1 / (1 + res_heat_val * 0.002f))) - 25);

  return res_heat;
}
uint8_t BME680Component::calc_heater_duration_(uint16_t duration) {
  uint8_t factor = 0;
  uint8_t duration_value;

  if (duration >= 0xfc0) {
    duration_value = 0xff;
  } else {
    while (duration > 0x3F) {
      duration /= 4;
      factor += 1;
    }
    duration_value = duration + (factor * 64);
  }

  return duration_value;
}
void BME680Component::read_data_() {
  uint8_t data[15];
  this->read_bytes(BME680_REGISTER_FIELD0, data, 15);

  uint32_t raw_temperature = (uint32_t(data[5]) << 12) | (uint32_t(data[6]) << 4) | (uint32_t(data[7]) >> 4);
  uint32_t raw_pressure = (uint32_t(data[2]) << 12) | (uint32_t(data[3]) << 4) | (uint32_t(data[4]) >> 4);
  uint32_t raw_humidity = (uint32_t(data[8]) << 8) | uint32_t(data[9]);
  uint16_t raw_gas = (uint16_t(data[13]) << 2) | (uint16_t(14) >> 6);
  uint8_t gas_range = data[14] & 0x0F;

  float temperature = this->calc_temperature_(raw_temperature);
  float pressure = this->calc_pressure_(raw_pressure);
  float humidity = this->calc_humidity_(raw_humidity);
  float gas_resistance = this->calc_gas_resistance_(raw_gas, gas_range);

  ESP_LOGD(TAG, "Got temperature=%.1f°C pressure=%.1fhPa humidity=%.1f% gas_resistance=%.1fΩ",
           temperature, pressure, humidity, gas_resistance);
  this->temperature_sensor_->push_new_value(temperature);
  this->pressure_sensor_->push_new_value(pressure);
  this->humidity_sensor_->push_new_value(humidity);
  this->gas_resistance_sensor_->push_new_value(gas_resistance);
}

float BME680Component::calc_temperature_(uint32_t raw_temperature) {
  float var1 = 0;
  float var2 = 0;
  float var3 = 0;
  float calc_temp = 0;
  float temp_adc = raw_temperature;

  const float t1 = this->calibration_.t1;
  const float t2 = this->calibration_.t2;
  const float t3 = this->calibration_.t3;

  /* calculate var1 data */
  var1 = ((temp_adc / 16384.0f) - (t1 / 1024.0f)) * t2;

  /* calculate var2 data */
  var3 = (temp_adc / 131072.0f) - (t1 / 8192.0f);
  var2 = var3 * var3 * t3 * 16.0f;

  /* t_fine value*/
  this->calibration_.tfine = (var1 + var2);

  /* compensated temperature data*/
  calc_temp = ((this->calibration_.tfine) / 5120.0f);

  return calc_temp;
}
float BME680Component::calc_pressure_(uint32_t raw_pressure) {
  const float tfine = this->calibration_.tfine;
  const float p1 = this->calibration_.p1;
  const float p2 = this->calibration_.p2;
  const float p3 = this->calibration_.p3;
  const float p4 = this->calibration_.p4;
  const float p5 = this->calibration_.p5;
  const float p6 = this->calibration_.p6;
  const float p7 = this->calibration_.p7;
  const float p8 = this->calibration_.p8;
  const float p9 = this->calibration_.p9;
  const float p10 = this->calibration_.p10;

  float var1 = 0;
  float var2 = 0;
  float var3 = 0;
  float var4 = 0;
  float calc_pres = 0;

  var1 = (tfine / 2.0f) - 64000.0f;
  var2 = var1 * var1 * (p6 / 131072.0f);
  var2 = var2 + var1 * p5 * 2.0f;
  var2 = (var2 / 4.0f) + (p4 * 65536.0f);
  var1 = (((p3 * var1 * var1) / 16384.0f) + (p2 * var1)) / 524288.0f;
  var1 = (1.0f + (var1 / 32768.0f)) * p1;
  calc_pres = 1048576.0f - float(raw_pressure);

  /* Avoid exception caused by division by zero */
  if (int(var1) != 0) {
    calc_pres = ((calc_pres - (var2 / 4096.0f)) * 6250.0f) / var1;
    var1 = (p9 * calc_pres * calc_pres) / 2147483648.0f;
    var2 = calc_pres * (p8 / 32768.0f);
    var4 = calc_pres / 256.0f;
    var3 = var4 * var4 * var4 * (p10 / 131072.0f);
    calc_pres = calc_pres + (var1 + var2 + var3 + (p7 * 128.0f)) / 16.0f;
  } else {
    calc_pres = 0;
  }

  return calc_pres / 100.0f;
}

float BME680Component::calc_humidity_(uint16_t raw_humidity) {
  const float tfine = this->calibration_.tfine;
  const float h1 = this->calibration_.h1;
  const float h2 = this->calibration_.h2;
  const float h3 = this->calibration_.h3;
  const float h4 = this->calibration_.h4;
  const float h5 = this->calibration_.h5;
  const float h6 = this->calibration_.h6;
  const float h7 = this->calibration_.h7;

  float calc_hum = 0;
  float var1 = 0;
  float var2 = 0;
  float var3 = 0;
  float var4 = 0;
  float temp_comp;

  /* compensated temperature data*/
  temp_comp = tfine / 5120.0f;

  var1 = float(raw_humidity) - (h1 * 16.0f + ((h3 / 2.0f) * temp_comp));
  var2 = var1 * (((h2 / 262144.0f) * (1.0f + ((h4 / 16384.0f) * temp_comp) +
      ((h5 / 1048576.0f) * temp_comp * temp_comp))));
  var3 = h6 / 16384.0f;
  var4 = h7 / 2097152.0f;

  calc_hum = var2 + (var3 + var4 * temp_comp) * var2 * var2;

  if (calc_hum > 100.0f)
    calc_hum = 100.0f;
  else if (calc_hum < 0.0f)
    calc_hum = 0.0f;

  return calc_hum;
}
uint32_t BME680Component::calc_gas_resistance_(uint16_t raw_gas, uint8_t range) {
  float calc_gas_res;
  float var1 = 0;
  float var2 = 0;
  float var3 = 0;
  const float range_sw_err = this->calibration_.range_sw_err;

  var1 = 1340.0f + (5.0f * range_sw_err);
  var2 = var1 * (1.0f + BME680_GAS_LOOKUP_TABLE_1[range] / 100.0f);
  var3 = 1.0f + (BME680_GAS_LOOKUP_TABLE_2[range] / 100.0f);

  calc_gas_res = 1.0f / (var3 * 0.000000125f * float(1 << range) * (((float(raw_gas) - 512.0f) / var2) + 1.0f));

  return calc_gas_res;
}
uint32_t BME680Component::calc_meas_duration_() {
  uint32_t tph_dur; // Calculate in us
  uint32_t meas_cycles;
  const uint8_t os_to_meas_cycles[6] = {0, 1, 2, 4, 8, 16};

  meas_cycles = os_to_meas_cycles[this->temperature_oversampling_];
  meas_cycles += os_to_meas_cycles[this->pressure_oversampling_];
  meas_cycles += os_to_meas_cycles[this->humidity_oversampling_];

  /* TPH measurement duration */
  tph_dur = meas_cycles * 1963u;
  tph_dur += 477 * 4; // TPH switching duration
  tph_dur += 477 * 5; // Gas measurement duration
  tph_dur += 500; // Get it to the closest whole number.
  tph_dur /= 1000; // Convert to ms

  tph_dur += 1; // Wake up duration of 1ms

  /* The remaining time should be used for heating */
  tph_dur += this->heater_duration_;

  return tph_dur;
}
BME680Component::BME680Component(I2CComponent *parent,
                                 const std::string &temperature_name,
                                 const std::string &pressure_name,
                                 const std::string &humidity_name,
                                 const std::string &gas_resistance_name,
                                 uint8_t address,
                                 uint32_t update_interval)
    : PollingComponent(update_interval), I2CDevice(parent, address),
      temperature_sensor_(new BME680TemperatureSensor(temperature_name, this)),
      pressure_sensor_(new BME680PressureSensor(pressure_name, this)),
      humidity_sensor_(new BME680HumiditySensor(humidity_name, this)),
      gas_resistance_sensor_(new BME680GasResistanceSensor(gas_resistance_name, this)) {

}
BME680TemperatureSensor *BME680Component::get_temperature_sensor() const {
  return this->temperature_sensor_;
}
BME680PressureSensor *BME680Component::get_pressure_sensor() const {
  return this->pressure_sensor_;
}
BME680HumiditySensor *BME680Component::get_humidity_sensor() const {
  return this->humidity_sensor_;
}
BME680GasResistanceSensor *BME680Component::get_gas_resistance_sensor() const {
  return this->gas_resistance_sensor_;
}
void BME680Component::set_temperature_oversampling(BME680Oversampling temperature_oversampling) {
  this->temperature_oversampling_ = temperature_oversampling;
}
void BME680Component::set_pressure_oversampling(BME680Oversampling pressure_oversampling) {
  this->pressure_oversampling_ = pressure_oversampling;
}
void BME680Component::set_humidity_oversampling(BME680Oversampling humidity_oversampling) {
  this->humidity_oversampling_ = humidity_oversampling;
}
void BME680Component::set_iir_filter(BME680IIRFilter iir_filter) {
  this->iir_filter_ = iir_filter;
}
void BME680Component::set_heater(uint16_t heater_temperature, uint16_t heater_duration) {
  this->heater_temperature_ = heater_temperature;
  this->heater_duration_ = heater_duration;
}

} // namespace sensor

ESPHOMELIB_NAMESPACE_END

#endif //USE_BME680