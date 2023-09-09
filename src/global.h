/**
 * @file global.h
 * @author Martin Verges <martin@verges.cc>
 * @version 0.1
 * @date 2022-07-09
 * 
 * @copyright Copyright (c) 2022 by the author alone
 *            https://gitlab.womolin.de/martin.verges/waterlevel
 * 
 * License: CC BY-NC-SA 4.0
 */

#include <Arduino.h>
#include "tanklevel.h"
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "MQTTclient.h"
#include "wifimanager.h"

#define webserverPort 80                    // Start the Webserver on this port
#define NVS_NAMESPACE "tanksensor"          // Preferences.h namespace to store settings

#include <SPI.h>
#include <Wire.h>

bool otaRunning = false;

RTC_DATA_ATTR struct timing_t {
  // Check Services like MQTT, ...
  uint64_t lastServiceCheck = 0;               // last millis() from ServiceCheck
  const unsigned int serviceInterval = 30000;  // Interval in ms to execute code

  // Sensor data in loop()
  uint64_t lastStatusUpdate = 0;                  // last millis() from Status report
  const unsigned int statusUpdateInterval = 5000; // Interval in ms to execute code
} Timing;

RTC_DATA_ATTR uint64_t sleepTime = 0;       // Time that the esp32 slept

WIFIMANAGER WifiManager;
bool enableWifi = true;                     // Enable Wifi, disable to reduce power consumtion, stored in NVS
u_int16_t shutDownWifiMin = 0;              // Shut down Wifi that many minutes after booting up (so poweroff/on can be used as replacement for the button to request wifi)
bool enableBle = true;                      // Enable Ble, disable to reduce power consumtion, stored in NVS
bool enableBleSleep = true;                 // If WiFi is off, sleep between advertising while no BLE client is connected

#define LEVELMANAGERS 1
TANKLEVEL LevelManager1(HX711_DT_PIN, HX711_SCK_PIN, (gpio_num_t)PUMP_PIN);
TANKLEVEL * LevelManagers[LEVELMANAGERS] = {
  &LevelManager1
};

#if HAS_BUTTON_INSTALLED
struct Button {
  const gpio_num_t PIN;
  bool pressed;
};
Button button1 = {GPIO_NUM_4, false};       // Run the setup (use a RTC GPIO)
void IRAM_ATTR ISR_button1() {
  button1.pressed = true;
}
#endif

String hostname;
AsyncWebServer webServer(webserverPort);
AsyncEventSource events("/api/events");
Preferences preferences;

MQTTclient Mqtt;

uint64_t runtime() {
  return rtc_time_slowclk_to_us(rtc_time_get(), esp_clk_slowclk_cal_get()) / 1000;
}

void deepsleepForSeconds(int seconds) {
    esp_sleep_enable_timer_wakeup(seconds * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : 
      LOG_INFO_LN(F("[POWER] Wakeup caused by external signal using RTC_IO"));
      #if HAS_BUTTON_INSTALLED
      button1.pressed = true;
      #endif
    break;
    case ESP_SLEEP_WAKEUP_EXT1 : LOG_INFO_LN(F("[POWER] Wakeup caused by external signal using RTC_CNTL")); break;
    case ESP_SLEEP_WAKEUP_TIMER : 
      LOG_INFO_LN(F("[POWER] Wakeup caused by timer"));
      uint64_t timeNow, timeDiff;
      timeNow = rtc_time_slowclk_to_us(rtc_time_get(), esp_clk_slowclk_cal_get());
      timeDiff = timeNow - sleepTime;
      printf("Now: %" PRIu64 "ms, Duration: %" PRIu64 "ms\n", timeNow / 1000, timeDiff / 1000);
      delay(2000);
    break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : LOG_INFO_LN(F("[POWER] Wakeup caused by touchpad")); break;
    case ESP_SLEEP_WAKEUP_ULP : LOG_INFO_LN(F("[POWER] Wakeup caused by ULP program")); break;
    default : LOG_INFO_F("[POWER] Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

// Check if a feature is enabled, that prevents the
// deep sleep mode of our ESP32 chip.
void sleepOrDelay();