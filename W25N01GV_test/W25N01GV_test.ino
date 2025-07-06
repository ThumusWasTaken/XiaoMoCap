#include <Wire.h>
#include <time.h>
#include "W25N01GV.h"
#include "BadBlockManager.h"
#include "Logger.h"

#include <Adafruit_MLX90393.h>
#include <Arduino_BMI270_BMM150.h>

// Flash
#define FLASH_CS D7
W25N01GV flash(FLASH_CS);
BadBlockManager bbm(flash);
Logger logger(flash, bbm);

// Sensors
Adafruit_MLX90393 mag = Adafruit_MLX90393();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting multi-sensor NAND logger...");

  // I2C setup (Xiao ESP32-C3: SDA = 8, SCL = 9)
  Wire.begin();
  delay(100);

  // --- BMI270 init ---
  if (!IMU.begin()) {
    Serial.println("BMI270 not detected!");
    while (1);
  }
  Serial.println("BMI270 connected!");
  Serial.print("Accelerometer rate = ");
  Serial.println(IMU.accelerationSampleRate());

  // --- MLX90393 init ---
  if (!mag.begin_I2C()) {
    Serial.println("MLX90393 not detected!");
    while (1);
  }
  Serial.println("MLX90393 connected.");
  mag.setGain(MLX90393_GAIN_1X);
  mag.setResolution(MLX90393_X, MLX90393_RES_16);
  mag.setResolution(MLX90393_Y, MLX90393_RES_16);
  mag.setResolution(MLX90393_Z, MLX90393_RES_16);
  mag.setOversampling(MLX90393_OSR_2);

  // --- Flash setup ---
  flash.setSPIFrequency(1000000);
  if (!flash.begin()) {
    Serial.println("Flash init failed!"); while (1);
  }
  flash.clearBlockProtection();
  flash.setECCEnabled(true);
  bbm.scan();
  if (!logger.begin()) {
    Serial.println("Logger init failed!"); while (1);
  }

  // Set initial time
  struct tm timeinfo;
  timeinfo.tm_year = 2025 - 1900;
  timeinfo.tm_mon = 5;
  timeinfo.tm_mday = 30;
  timeinfo.tm_hour = 14;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;
  time_t t_of_day = mktime(&timeinfo);
  struct timeval now = { .tv_sec = t_of_day };
  settimeofday(&now, NULL);
}

void loop() {
  Serial.println("\n--- Logging Menu ---");
  Serial.println("Enter logging duration in seconds: ");
  String input = "";
  while (input.length() == 0) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') break;
      input += c;
    }
  }
  uint32_t duration_ms = input.toInt() * 1000UL;
  if (duration_ms == 0) duration_ms = 60000;

  Serial.println("Enter sampling rate in Hz: ");
  input = "";
  while (input.length() == 0) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') break;
      input += c;
    }
  }
  uint16_t sample_rate_hz = input.toInt();
  if (sample_rate_hz == 0 || sample_rate_hz > 100) sample_rate_hz = 10;
  uint32_t interval = 1000 / sample_rate_hz;

  Serial.printf("Logging for %lu seconds at %u Hz...\n", duration_ms / 1000, sample_rate_hz);
  Serial.println("timestamp_iso,ax,ay,az,gx,gy,gz,mx,my,mz");

  uint32_t startTime = millis();
  while (millis() - startTime < duration_ms) {
    // Timestamp
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    // BMI270 readings
    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    if (IMU.accelerationAvailable()) IMU.readAcceleration(ax, ay, az);
    if (IMU.gyroscopeAvailable()) IMU.readGyroscope(gx, gy, gz);

    // MLX90393 readings
    float mx = 0, my = 0, mz = 0;
    mag.readData(&mx, &my, &mz);

    // Format CSV
    String line = String(timestamp) + "," +
                  String(ax,3) + "," + String(ay,3) + "," + String(az,3) + "," +
                  String(gx,2) + "," + String(gy,2) + "," + String(gz,2) + "," +
                  String(mx,2) + "," + String(my,2) + "," + String(mz,2);

    logger.logRecord(line);
    Serial.println(line);

    delay(interval);
  }

  logger.flush();
  Serial.println("Logging complete. Dumping logs...\n");

  uint8_t readBuf[2048];
  uint16_t lastPage = logger.getCurrentPage();
  for (uint16_t page = 0; page < lastPage; page++) {
    flash.readPage(page, readBuf, 2048);
    for (uint16_t i = 0; i < 2048; i++) {
      char c = (char)readBuf[i];
      if (c == 0xFF || c == 0) break;
      Serial.print(c);
    }
  }
  Serial.println("\n--- CSV complete ---");
  while (1);
}
