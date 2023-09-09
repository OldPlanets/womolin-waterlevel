/**
 * @file tanklevel.cpp
 * @author Martin Verges <martin@verges.cc>
 * @version 0.1
 * @date 2022-07-09
 * 
 * @copyright Copyright (c) 2022 by the author alone
 *            https://gitlab.womolin.de/martin.verges/waterlevel
 * 
 * License: CC BY-NC-SA 4.0
 */

#include "log.h"

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>
#include "tanklevel.h"
#include <bits/stdc++.h>
#include <soc/rtc.h>
extern "C" {
  #if ESP_ARDUINO_VERSION_MAJOR >= 2
    #include <esp32/clk.h>
  #else
    #include <esp_clk.h>
  #endif
}

TANKLEVEL::TANKLEVEL(uint8_t dout, uint8_t pd_sck, gpio_num_t pin) {
    hx711.begin(dout, pd_sck, HX711_GAIN);
    setAirPumpPIN(pin);
}

void TANKLEVEL::loop() {
  if (setupConfig.start)
  { 
    beginLevelSetup();
  }

  // Stop repressurizing the tube after X seconds
  if (airPumpEnabled && runtime() - (isSetupRunning() ? min(airPumpDurationMS, (uint64_t)timing.setupIntervalMs - WAIT_READING_AFTER_PUMP)  : airPumpDurationMS) > airPumpStarttime) {
    deactivateAirPump();
  }

  if (isSetupRunning()) {
    // run the level setup
    if (runtime() - timing.lastSetupRead >= timing.setupIntervalMs) {
      timing.lastSetupRead = runtime();
      int val = runLevelSetup();
      if (!val) LOG_INFO_LN(F("[SENSOR] Unable to read data from sensor!"));
      activateAirPump("Setup, keeping perfect pressure while filling up");
    }
  }
  else
  {
    if (runtime() - timing.lastSensorRead >= timing.sensorIntervalMs && !airPumpEnabled && runtime() - airPumpEndtime >= WAIT_READING_AFTER_PUMP)
    {
      timing.lastSensorRead = runtime();
      getCalulcatedMedianReading(false);
      calculateLevel();
      if (automaticAirPump && levelConfig.setupDone)
      {
        if (firstReadSincePump)
        {
          firstReadSincePump = false;
          setPressurizeOnLevelNVS(getLevel() + repressurizeLevels, true);
        }
        else if (getLevel() >= levelConfig.pressurizeOnLevel)
        {
          activateAirPump("Tank is filling up");
        }
        else if (getLevel() <= levelConfig.pressurizeOnLevel - (repressurizeLevels * 2))
        {
          // tank is emptying and tube is losing pressure, update the level at which we need to repressurzize once we fill up again
          setPressurizeOnLevelNVS(getLevel() + repressurizeLevels, true); 
        }
      }
    }
  }
}

bool TANKLEVEL::canSleep()
{
  return !isSetupRunning() 
    && !airPumpEnabled
    && !firstReadSincePump; // let it take one more fresh reading after pressurizing before deep sleeping
}

void TANKLEVEL::deactivateAirPump() {
  LOG_INFO_LN(F("[AIRPUMP] Shutting down"));
  airPumpEnabled = false;
  airPumpStarttime = 0;
  airPumpEndtime = runtime();
  firstReadSincePump = true;
  digitalWrite(airPumpPIN, LOW);

  levelConfig.airPressureOnFilling = airPressure;
  if (!isSetupRunning())
  {
    updateAirPressureNVS(airPressure);
  }
}

void TANKLEVEL::activateAirPump(String reason) {
  LOG_INFO_F("[AIRPUMP] Starting Air Pump on GPIO %d at runtime %" PRIu64 ". Reason: %s\n", airPumpPIN, runtime(), reason.c_str());
  airPumpEnabled = true;
  airPumpStarttime = runtime();
  airPumpEndtime = 0;
  digitalWrite(airPumpPIN, HIGH);
}

void TANKLEVEL::setAirPumpPIN(gpio_num_t gpio) {
  airPumpPIN = gpio; 
  pinMode(airPumpPIN, OUTPUT);
  digitalWrite(airPumpPIN, LOW);
  airPumpEnabled = false;
}

uint64_t TANKLEVEL::runtime() {
  return rtc_time_slowclk_to_us(rtc_time_get(), esp_clk_slowclk_cal_get()) / 1000;
}

bool TANKLEVEL::setMaxVolume(uint32_t tankvolume, String unit) {
  if (unit.equals("liters")) tankvolume = tankvolume * 1000;
  else if (unit.equals("milliliters")) tankvolume = tankvolume;
  else if (unit.equals("usgallons")) tankvolume = tankvolume * 1000 * 3.785411784;
  else LOG_INFO_F("[ERROR] Unknown unit '%s' given\n", unit);

  if (preferences.begin(NVS.c_str(), false)) {
    LOG_INFO_F("[CONFIG] Tank volume of %d milliliters saved to NVS.\n", tankvolume);
    preferences.putUInt("volume", tankvolume);
    preferences.end();
    levelConfig.volumeMilliLiters = tankvolume;
    return true;
  } else {
    LOG_INFO_LN("setMaxVolume() - Unable to write data to NVS, giving up...");
    return false;
  }
}

bool TANKLEVEL::updateOffsetNVS() {
  if (preferences.begin(NVS.c_str(), false)) {
    preferences.putDouble("offset", levelConfig.offset);
    preferences.end();
    return true;
  } else {
    LOG_INFO_LN("updateOffsetNVS() - Unable to write data to NVS, giving up...");
    return false;
  }
}

bool TANKLEVEL::writeToNVS() {
  if (preferences.begin(NVS.c_str(), false)) {
    preferences.clear();
    preferences.putBool("setupDone", true);
    preferences.putDouble("offset", levelConfig.offset);
    preferences.putUInt("airpressure", levelConfig.airPressureOnFilling);
    preferences.putUInt("volume", levelConfig.volumeMilliLiters);
    preferences.putUChar("pressurizelevel", levelConfig.pressurizeOnLevel);

    for (uint8_t i = 0; i <= 100; i++) {
      preferences.putInt(String("val" + String(i)).c_str(), levelConfig.readings[i]);
      // LOG_INFO("Write new value = ");
      // LOG_INFO_LN(levelConfig.readings[i]);
    }
    preferences.end();
    LOG_INFO_LN("writeToNVS() - Config written to NVS");
    return true;
  } else {
    LOG_INFO_LN("writeToNVS() - Unable to write data to NVS, giving up...");
    return false;
  }
}

bool TANKLEVEL::writeSingleEntrytoNVS(uint8_t i, int value) {
  if (i == 255 && preferences.begin(NVS.c_str(), false)) {
    preferences.putBool("setupDone", value > 0);
    preferences.end();
    return true;
  } else if (i < 0 or i > 100) return false;
  if (preferences.begin(NVS.c_str(), false)) {
    preferences.putInt(String("val" + String(i)).c_str(), value);
    preferences.end();
    return true;
  }
  return false;
}

int TANKLEVEL::getLevelData(int perc) {
  if (perc >= 0 and perc <= 100) {
    return levelConfig.readings[perc];
  } else return -1;
}

void TANKLEVEL::setSensorOffset(double newOffset) {
  if (newOffset == 0.0) {
    LOG_INFO_LN(F("Reading the new offset from sensor"));
    newOffset = getSensorRawMedianReading(false);
  }
  levelConfig.offset = newOffset;
  //hx711.set_offset(levelConfig.offset); // we aren't calling any function of the library which actually use the offet but calculate it ourself
}

void TANKLEVEL::begin(String ns) {
  NVS = ns;

  if (!preferences.begin(NVS.c_str(), false)) {
    LOG_INFO_LN("Error opening NVS Namespace, giving up...");
  } else {
    levelConfig.setupDone = preferences.getBool("setupDone", false);
    levelConfig.airPressureOnFilling = preferences.getUInt("airpressure", 0);
    levelConfig.pressurizeOnLevel = preferences.getUChar("pressurizelevel", 255);
    setSensorOffset(preferences.getDouble("offset", 0.0));

    levelConfig.volumeMilliLiters = preferences.getUInt("volume", 0);

    if (levelConfig.setupDone) {
      LOG_INFO_LN("LevelData restored from Storage...");
      for (uint8_t i = 0; i <= 100; i++) {
        levelConfig.readings[i] = preferences.getInt(String("val" + String(i)).c_str(), 0);
      }
    } else {
      LOG_INFO_LN("No stored configuration found on NVS...");
    }
    preferences.end();
  }
}

bool TANKLEVEL::isSetupRunning() {
  return setupConfig.running;
}

bool TANKLEVEL::isConfigured() {
  return levelConfig.setupDone;
}

double TANKLEVEL::getSensorRawMedianReading(bool cached) {
  if(cached) return lastRawReading;
  lastRawReading = hx711.read_median(10);
  hasSensorError = lastRawReading == 0;
  //LOG_INFO_F("Current sensor raw reading %.2f\n", lastRawReading);
  return lastRawReading;
}

int TANKLEVEL::getCalulcatedMedianReading(bool cached) {
  if (cached) return lastMedian;
  lastMedian = (int)round((getSensorRawMedianReading(false) - levelConfig.offset) / 100);  
  return lastMedian;
}

uint8_t TANKLEVEL::calculateLevel() {
  // Find the highest percentage of the current reading value
  if (levelConfig.setupDone)
  {
    for(uint8_t x=100; x>0; x--) {
      if (lastMedian >= levelConfig.readings[x]) {
          level = x;
          return level;
      }
    }
  }
  level = 0;
  return level;
}

bool TANKLEVEL::updateAirPressureNVS(uint32_t newPressure) {
  if (preferences.begin(NVS.c_str(), false)) {
    // prevent unneccessary writes to NVS, only if there is a larger pressure difference
    uint32_t old = preferences.getUInt("airpressure");
    if (old+NVS_WRITE_TOLERANCE_HPA > newPressure && old-NVS_WRITE_TOLERANCE_HPA < newPressure) {
      // only a minor change, we don't update the old value
    } else {
      preferences.putUInt("airpressure", newPressure);
      LOG_INFO_LN(F("updateAirPressureNVS() - Wrote new pressure to NVS"));
    }
    preferences.end();
    return true;
  } else {
    LOG_INFO_LN(F("updateAirPressureNVS() - Unable to write data to NVS, giving up..."));
    return false;
  }
}

bool TANKLEVEL::setPressurizeOnLevelNVS(uint8_t newLevel, bool writeNVS) {
  levelConfig.pressurizeOnLevel = newLevel;
  if (writeNVS && preferences.begin(NVS.c_str(), false))
  {
    uint8_t old = preferences.getUChar("pressurizelevel", 255);
    if (old+NVS_WRITE_TOLERANCE_LEVEL > newLevel && old-NVS_WRITE_TOLERANCE_LEVEL < newLevel) {
      // only a minor change, we don't update the old value
    } else {
      preferences.putUChar("pressurizelevel", newLevel);
      LOG_INFO_LN(F("updatePressurizeOnLevelNVS - Wrote new level to NVS"));
    }
    preferences.putUChar("pressurizelevel", newLevel);
    preferences.end();
    return true;
  } else {
    LOG_INFO_LN(F("updatePressurizeOnLevelNVS() - Unable to write data to NVS, giving up..."));
    return false;
  }
}

void TANKLEVEL::setCutoffLimits(float lower_end, float upper_end) {
  LOWER_END = lower_end;
  UPPER_END = upper_end;
}

// Search through the setupConfig sensor readings and find the upper limit cutoff index
int TANKLEVEL::findEndCutoffIndex() {
  float endCutoff = (setupConfig.readings[MAX_DATA_POINTS-1] - setupConfig.readings[0]) * UPPER_END + setupConfig.readings[0];
  int endIndex = -1;
  for (uint8_t i = 0; i < MAX_DATA_POINTS; i++) {
    if (setupConfig.readings[i] > endCutoff) {
      endIndex = i;
      break;
    }
  }
  return endIndex;
}

// Search through the setupConfig sensor readings and find the lower limit cutoff index
int TANKLEVEL::findStartCutoffIndex(int endIndex) {
  float startCutoff = (setupConfig.readings[endIndex] - setupConfig.readings[0]) * LOWER_END + setupConfig.readings[0];
  int startIndex = -1;
  for (uint8_t i = 0; i < MAX_DATA_POINTS; i++) {
    if (setupConfig.readings[i] < startCutoff) startIndex = i;
    else break;
  }
  return startIndex;
}

bool TANKLEVEL::beginLevelSetup() {
  setupConfig.start = false;
  if (!isSetupRunning()) {  // Start the level setup
    setSensorOffset(0.0); // empty tank is always our sensor offset, so set the new offset here (0.0 means new reading)
    setupConfig.running = true;
    setupConfig.valueCount = 0;
    setupConfig.readings[setupConfig.valueCount++] = 0; // just set the offset to the current reading, so this always 0 except for noise
    LOG_INFO_F("Begin level setup with a sensor offset of %.02f\n", levelConfig.offset);
    return true;
  } else {
    LOG_INFO_LN("Level setup is already running");
    return false;
  }
}

bool TANKLEVEL::abortLevelSetup() {
  LOG_INFO("Abort setup ... ");
  resetSetupData();
  return true;
}

void TANKLEVEL::resetSetupData() {
  setupConfig.valueCount = 0;
  setupConfig.running = false;
  setupConfig.start = false;
  setupConfig.abort = false;
  setupConfig.end = false;
}

bool TANKLEVEL::setupFrom2Values(int lower, int upper) {    
  if (upper < lower) return false;
  for (size_t Y = 0; Y <= 100; Y++) {
    levelConfig.readings[Y] = (upper - lower) / 100.0 * Y + lower;
    //LOG_INFO_LN(levelConfig.readings[Y]);
  }
  LOG_INFO_LN("Level config done!");
  levelConfig.setupDone = true;
  writeToNVS();
  return true;
}

bool TANKLEVEL::endLevelSetup() {
  if (setupConfig.abort) return abortLevelSetup();
  LOG_INFO_LN("Exiting setup");

  while(setupConfig.valueCount < MAX_DATA_POINTS) {
    // force end of setup by filling empty slots
    setupConfig.readings[setupConfig.valueCount++] = lastMedian;
  }

  std::sort(std::begin(setupConfig.readings), std::end(setupConfig.readings));
  int endIndex = findEndCutoffIndex();
  int startIndex = findStartCutoffIndex(endIndex);
  // LOG_INFO_F("Start Index = %d\n", startIndex);
  // LOG_INFO_F("End Index = %d\n", endIndex);

  // calculate the percentages
  int readingCount = endIndex - startIndex + 1;  // number of readings to use
  int x = startIndex;                        // index of reading[]
  float y1 = 0.00;                           // lower percentage of reading[] index (e.g. entry x equals 55%)
  float y2 = 0.00;                           // upper percentage of reading[] index (e.g. entry x+1 equals 58%)
  for (size_t Y = 0; Y <= 100; Y++) {         // % value (1-100%)
    if (Y==0) {
      levelConfig.readings[Y] = setupConfig.readings[x];
      continue;
    }
    while (Y > y2 && x <= endIndex) {
      // find the next dataset to calculate Z
      y1 = (float)(x-startIndex  ) / readingCount * 100;     // lower percentage of this sensor reading
      y2 = (float)(x-startIndex+1.0) / readingCount * 100;     // upper percentage of the next sensor reading
      x++;
    }
    // float Z = setupConfig.readings[x-1] + ((Y - y1) / (y2 - y1) * (setupConfig.readings[x] - setupConfig.readings[x-1])); // save the value into the configuration
    levelConfig.readings[Y] = setupConfig.readings[x-1] + ((Y - y1) / (y2 - y1) * (setupConfig.readings[x] - setupConfig.readings[x-1]));
    // LOG_INFO_F("\t\t\ty1=\t%f\t | y2=\t%f\n", y1, y2);
    // LOG_INFO_F("Y=%d\t| Z = %d | z1=\t%f\t| z2=\t%f\n", Y, (int)Z, setupConfig.readings[x-1], setupConfig.readings[x]);
  }
  levelConfig.setupDone = true;
  levelConfig.pressurizeOnLevel = 100 +repressurizeLevels;
  firstReadSincePump = false; 
  if (!writeToNVS()) return false;

  // cleanup
  resetSetupData();
  return true;
}

int TANKLEVEL::runLevelSetup() {
  if (setupConfig.start) return beginLevelSetup();
  if (setupConfig.abort) return abortLevelSetup();
  if (setupConfig.end) return endLevelSetup();
  if (isSetupRunning()) {
    if (setupConfig.valueCount >= MAX_DATA_POINTS) {
      endLevelSetup();
      return 0;
    }
    LOG_INFO_F("Recording new entry with a value of %d\n", getCalulcatedMedianReading(false));
    setupConfig.readings[setupConfig.valueCount++] = lastMedian;
    return lastMedian;
  } else return 0;
}

void TANKLEVEL::setAirPressure(int32_t hPa, bool runPumpIfPressureDifferenceIsLarge) {
  if (hPa > 0 && hPa < 2000) {
    // LOG_INFO_F("New airPressure is %d hPa\n", hPa);
    airPressure = hPa;

    if (automaticAirPump && runPumpIfPressureDifferenceIsLarge) {
      // Automatic repressurization is enabled.
      // Check if there is a larger difference from the stored air pressure value
      if (abs(levelConfig.airPressureOnFilling - airPressure) > automatichAirPumpOnPressureDifferenceHPA)
      {
        // airPressure reduced more than X or increased more than X
        activateAirPump("Atmospheric air pressure changed");
      }
    }
  } else if (hPa != 0) LOG_INFO_F("[ERROR] Invalid airpressure value given. Got %d\n", hPa);
}
