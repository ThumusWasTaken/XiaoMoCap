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
#include "Adafruit_MLX90395.h"
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

Adafruit_MLX90395 mag = Adafruit_MLX90395();

// ── Adjust BMI/BMM ranges ─────────────────────────────────────────────────────

#define BMI270_ADDR  0x68   // SDO → GND = 0x68,  SDO → VCC = 0x69

void setAccelRange(uint8_t range) {
  // range: 0=±2g  1=±4g  2=±8g  3=±16g
  Wire.beginTransmission(BMI270_ADDR);
  Wire.write(0x41);    // ACC_RANGE register
  Wire.write(range & 0x03);
  Wire.endTransmission();
  float ranges[] = {2, 4, 8, 16};
  Serial.printf("Accel range set to ±%.0fg\n", ranges[range & 0x03]);
}

void setGyroRange(uint8_t range) {
  // range: 0=±2000  1=±1000  2=±500  3=±250  4=±125  dps
  Wire.beginTransmission(BMI270_ADDR);
  Wire.write(0x43);    // GYR_RANGE register
  Wire.write(range & 0x07);
  Wire.endTransmission();
  float ranges[] = {2000, 1000, 500, 250, 125};
  Serial.printf("Gyro range set to ±%.0f dps\n", ranges[range & 0x07]);
}


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
  Serial.print("Accelerometer rate = ");
  Serial.println(IMU.accelerationSampleRate());
  Serial.print("Gyroscope rate = ");
  Serial.println(IMU.gyroscopeSampleRate());
  //setAccelRange(1);   // ±4g
  //setGyroRange(0);    // ±2000 dps

  // --- MLX90393 init ---
  if (!mag.begin_I2C()) {
    Serial.println("MLX90395 not detected!");
    while (1);
  }
  Serial.println("MLX90395 connected.");
  //uint8_t gain_val 7;
  
  Serial.print("Gain: "); Serial.println(mag.getGain());
  Serial.print("Resolution: "); Serial.println(mag.getResolution());
  Serial.print("OSR: "); Serial.println(mag.getOSR());
    // mag.setResolution(MLX90395_RES_16);
    // mag.setOSR(MLX90395_OSR_1);
    //mag.setFilter(MLX90395_FILTER_2); //?
  //mag.setResolution(MLX90395_RES_16);   // finest LSB — already set
    //mag.setOSR(MLX90395_OSR_2);           // can work for freq up to 100Hz
    //mag.setOSR(MLX90395_OSR_0);            // default value
  //mag.setGain(5);                        // safe for Earth-field sensing

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
  // logFile.println("timestamp_ms,ax_mg,ay_mg,az_mg,gx_ddps,gy_ddps,gz_ddps,n");
  logFile.println("timestamp_ms,ax_mg,ay_mg,az_mg,gx_ddps,gy_ddps,gz_ddps,mx,my,mz,n");

  // ── Timing ───────────────────────────────────────────────────────────────────
  int64_t nextTick_us = esp_timer_get_time();
  int64_t end_us      = nextTick_us + (int64_t)duration_ms * 1000LL;

  float    ax, ay, az, gx, gy, gz, mx, my, mz;
  mx=0, my=0, mz=0;
  uint32_t n = 0;
  char     buf[128];

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
    mag.readData(&mx, &my, &mz);

    // Serial.print("X: "); Serial.print(mag.magnetic.x);
    // Serial.print(" \tY: "); Serial.print(mag.magnetic.y); 
    // Serial.print(" \tZ: "); Serial.println(mag.magnetic.z); 


    // Format line — snprintf, no String, no heap allocation
    int len = snprintf(buf, sizeof(buf),
      "%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu",
      // "%lu,%d,%d,%d,%d,%d,%d,%lu",
      // (unsigned long)(ts_us / 1000LL),
      // (int16_t)(ax * 1000), (int16_t)(ay * 1000), (int16_t)(az * 1000),
      // (int16_t)(gx * 100),  (int16_t)(gy * 100),  (int16_t)(gz * 100),
      // (int16_t)(mx * 10),   (int16_t)(my * 10),   (int16_t)(mz * 10),
      // (unsigned long)n);
     (unsigned long)(ts_us / 1000LL),
      (int16_t)(ax), (int16_t)(ay), (int16_t)(az),
      (int16_t)(gx),  (int16_t)(gy),  (int16_t)(gz),
      (int16_t)(mx),   (int16_t)(my),   (int16_t)(mz),
      (unsigned long)n);

    logFile.write(buf, len);
    logFile.write('\n');

    Serial.println(buf);

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
