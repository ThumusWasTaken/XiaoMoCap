/**
 * BMI270 IMU Logger with SdFat  |  XIAO ESP32S3
 * ─────────────────────────────────────────────
 * Uses SdFat instead of the built-in SD library.
 * SdFat writes in 512-byte aligned blocks which avoids
 * the partial-write corruption seen with the SD library
 * on long recordings.
 *
 * Libraries (Arduino Library Manager):
 *   - Arduino_BMI270_BMM150
 *   - SdFat  by Bill Greiman
 *
 * Wiring:
 *   BMI270  SDA → D4,  SCL → D5  (I2C)
 *   SD      CS  → D2,  SCK → D7, MOSI → D9, MISO → D8
 *
 * Serial Monitor: 921600 baud
 */

#include <Wire.h>
#include <SPI.h>
#include <Arduino_BMI270_BMM150.h>
#include "SdFat.h"
#include "esp_timer.h"   // int64_t µs — no rollover


// ── SD pins ───────────────────────────────────────────────────────────────────
#define SD_CS_PIN SS
#define SPI_CLOCK   SD_SCK_MHZ(16)   // 16 MHz — safe for most SD cards

// ── Log file ──────────────────────────────────────────────────────────────────
#define LOG_FILE   "data.csv"

// ── Flush every N samples ─────────────────────────────────────────────────────
#define FLUSH_EVERY   100

SdFat  sd;
SdFile logFile;


// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(921600);
  while (!Serial) delay(10);
  Serial.println("\n=== BMI270 SdFat Logger ===");

  // I2C for BMI270
  Wire.begin();
  Wire.setClock(400000);

  if (!IMU.begin()) {
    Serial.println("BMI270 not found! Halt.");
    while (1);
  }
  Serial.println("BMI270 OK");

  // SdFat init
  Serial.print("Initializing SD card...");

  if (!sd.begin(SD_CS_PIN)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("initialization done.");
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {

  // ── Ask for duration ─────────────────────────────────────────────────────────
  Serial.println("\nDuration in seconds:");
  uint32_t duration_ms = readInt() * 1000UL;
  if (duration_ms == 0) duration_ms = 60000UL;

  // ── Ask for sample rate ───────────────────────────────────────────────────────
  Serial.println("Sample rate in Hz (1-100):");
  uint16_t hz = (uint16_t)readInt();
  if (hz == 0 || hz > 100) hz = 10;

  int64_t interval_us = 1000000LL / hz;
  Serial.printf("Logging %lu s @ %u Hz\n", duration_ms / 1000UL, hz);

  // ── Open file ─────────────────────────────────────────────────────────────────
  sd.remove(LOG_FILE);
  if (!logFile.open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC)) {
    Serial.println("Cannot open file!");
    return;
  }

  // Write CSV header
  logFile.println("timestamp_ms,ax_mg,ay_mg,az_mg,gx_ddps,gy_ddps,gz_ddps,n");

  // ── Timing ───────────────────────────────────────────────────────────────────
  int64_t nextTick_us = esp_timer_get_time();
  int64_t end_us      = nextTick_us + (int64_t)duration_ms * 1000LL;

  float    ax, ay, az, gx, gy, gz;
  uint32_t n = 0;
  char     buf[80];

  Serial.println("Recording...");

  // ── Hot loop ──────────────────────────────────────────────────────────────────
  while (esp_timer_get_time() < end_us) {

    // Wait for next sample slot
    while (esp_timer_get_time() < nextTick_us) {}
    int64_t ts_us = nextTick_us;   // capture before advancing
    nextTick_us  += interval_us;

    // Read sensors
    IMU.readAcceleration(ax, ay, az);
    IMU.readGyroscope(gx, gy, gz);

    // Format line — snprintf, no String, no heap allocation
    int len = snprintf(buf, sizeof(buf),
      "%lu,%d,%d,%d,%d,%d,%d,%lu",
      (unsigned long)(ts_us / 1000LL),
      (int16_t)(ax * 1000), (int16_t)(ay * 1000), (int16_t)(az * 1000),
      (int16_t)(gx * 10),   (int16_t)(gy * 10),   (int16_t)(gz * 10),
      (unsigned long)n);

    logFile.write(buf, len);
    logFile.write('\n');

    n++;
    if (n % FLUSH_EVERY == 0) logFile.sync();   // SdFat uses sync() not flush()
  }

  // ── Close ─────────────────────────────────────────────────────────────────────
  logFile.sync();
  logFile.close();
  Serial.printf("Done — %lu samples written.\n\n", (unsigned long)n);

  // ── Read back ─────────────────────────────────────────────────────────────────
  Serial.println("--- SD Readback ---");
  SdFile rf;
  if (rf.open(LOG_FILE, O_RDONLY)) {
    uint8_t blk[512];
    int     got;
    while ((got = rf.read(blk, sizeof(blk))) > 0) {
      Serial.write(blk, got);
    }
    rf.close();
  } else {
    Serial.println("Cannot open file for readback!");
  }

  Serial.println("\n--- Done. Reset to run again. ---");
  while (1) delay(100);
}

// ── Helper: block until a number arrives on Serial ───────────────────────────
int32_t readInt() {
  String s = "";
  while (true) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') { if (s.length()) return s.toInt(); }
      else s += c;
    }
    delay(5);
  }
}
