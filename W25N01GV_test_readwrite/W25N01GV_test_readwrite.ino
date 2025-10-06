#include <Wire.h>
#include <time.h>
#include "W25N01GV.h"
#include "BadBlockManager.h"
#include "Logger.h"

#include <Arduino_BMI270_BMM150.h>
#include <Adafruit_MLX90393.h>

#include <WiFi.h>
#include <WebServer.h>

// Flash
#define FLASH_CS D7
W25N01GV flash(FLASH_CS);
BadBlockManager bbm(flash);
Logger logger(flash, bbm);

int32_t data_num = 0;
// Sensors
bool acc_active = true;
bool gyr_active = true;
bool mag_active = true;
Adafruit_MLX90393 mag = Adafruit_MLX90393();

// WiFi credentials for Access Point
const char* ssid = "ESP32-DataServer";
const char* password = "12345678";

// Configurable IP settings
IPAddress local_IP(192, 168, 1, 100);  // Change this IP as needed
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// Create WebServer object on port 80
WebServer server(80);

String dat = "";

void shutdownWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Wi-Fi turned off.");
}

void startWiFi() {
  WiFi.mode(WIFI_AP);                      // Set Wi-Fi mode to AP
  WiFi.softAPConfig(local_IP, gateway, subnet);  // Configure IP
  WiFi.softAP(ssid, password);             // Start AP
  Serial.println("Wi-Fi Access Point restarted");
}


void handleData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("timestamp,ax,ay,az,gx,gy,gz,mx,my,mz,counter\n");

  uint8_t readBuf[PAGE_SIZE];
  uint32_t lastPage = logger.getCurrentPage();

  for (uint32_t page = 0; page < lastPage; page++) {
    if (!bbm.isBlockGood(page / 64)) continue;
    if (!flash.readPage(page, readBuf, sizeof(readBuf))) continue;

    PageHeader *hdr = (PageHeader*)readBuf;
    if (hdr->magic != PAGE_MAGIC) continue;

    for (uint16_t i = 0; i < RECORDS_PER_PAGE; i++) {
      SensorRecord *rec = (SensorRecord*)(readBuf + HEADER_SIZE + i * RECORD_SIZE);
      if (rec->timestamp == 0xFFFFFFFF) break;

      char line[128];
      int len = snprintf(line, sizeof(line),
        "%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu\n",
        rec->timestamp,
        rec->ax, rec->ay, rec->az,
        rec->gx, rec->gy, rec->gz,
        rec->mx, rec->my, rec->mz,
        rec->counter);
      server.sendContent_P(line, len);
    }
    delay(1);
  }

  server.sendContent("");
}



void setup() {
  //Serial.begin(115200);
  Serial.begin(921600);
  delay(1000);
  Serial.println("Starting multi-sensor NAND logger...");

  // I2C setup (Xiao ESP32-C3: SDA = 8, SCL = 9)
  Wire.begin();
  Wire.setClock(400000);
  delay(100);

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

  Serial.println("Erasing flash... this may take a few seconds.");
  logger.eraseAll();
  Serial.println("Flash erase complete.");

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

  // --- BMI270 init ---
  if (!IMU.begin()) {
    Serial.println("BMI270 not detected!");
    while (1);
  }
  Serial.println("BMI270 connected!");
  Serial.print("Accelerometer rate = ");
  Serial.println(IMU.accelerationSampleRate());
  Serial.print("Gyroscope rate = ");
  Serial.println(IMU.gyroscopeSampleRate());

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
  mag.setOversampling(MLX90393_OSR_0);
  mag.setFilter(MLX90393_FILTER_2);

  delay(10);

  // Configure Access Point with custom IP
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  
  Serial.println("=== ESP32 Data Server Started ===");
  Serial.println("WiFi SSID: " + String(ssid));
  Serial.println("WiFi Password: " + String(password));
  Serial.println("Server IP: " + IP.toString());
  Serial.println("Endpoints:");
  Serial.println("  http://" + IP.toString() + "/data (CSV)");
  Serial.println("  http://" + IP.toString() + "/json (JSON)");
  Serial.println("===================================");

  // Set up web server routes
  //server.on("/", handleRoot);
  server.on("/data", handleData);

  // Start server
  server.begin();
  Serial.println("HTTP server started");

  delay(1000);
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

  uint32_t interval_us = 1000000UL / sample_rate_hz;  // sampling interval in microseconds

  Serial.printf("Logging for %lu seconds at %u Hz...\n", duration_ms / 1000, sample_rate_hz);
  Serial.println("timestamp,ax,ay,az,gx,gy,gz,mx,my,mz,counter");

  // --- Get initial time ---
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t base_timestamp_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;

  // --- Loop control ---
  uint32_t startTime = millis();
  uint64_t current_timestamp_us = base_timestamp_us;

  uint8_t mag_count = 0;

  while (millis() - startTime < duration_ms) {
    // --- Sensor readings ---
    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    float mx = 0, my = 0, mz = 0;

    if (acc_active) IMU.readAcceleration(ax, ay, az);
    if (gyr_active) IMU.readGyroscope(gx, gy, gz);
    if (mag_active && mag_count == 0) mag.readData(&mx, &my, &mz);
    mag_count = (mag_count + 1) % 4;

    SensorRecord rec;
    // Store timestamp in milliseconds
    rec.timestamp = (uint32_t)(current_timestamp_us / 1000ULL);
    rec.ax = (int16_t)(ax * 1000);
    rec.ay = (int16_t)(ay * 1000);
    rec.az = (int16_t)(az * 1000);
    rec.gx = (int16_t)(gx * 100);
    rec.gy = (int16_t)(gy * 100);
    rec.gz = (int16_t)(gz * 100);
    rec.mx = (int16_t)(mx * 10);
    rec.my = (int16_t)(my * 10);
    rec.mz = (int16_t)(mz * 10);
    rec.counter = data_num;

    logger.logRecord(rec);

    // Increment timestamp deterministically
    current_timestamp_us += interval_us;

    // Timing control
    uint64_t nextTick = micros() + interval_us;
    while ((long)(micros() - nextTick) < 0) {
      // yield();
    }

    data_num++;
  }

  logger.flush();
  Serial.println("Logging complete.");
  startWiFi();
  delay(30000);

  for (uint16_t i = 0; i < 5; i++) {
    server.handleClient();
    delay(10);
    Serial.println("\n--- handled ---");
    Serial.println("\n--- write yes to read again, no to exit read loop ---");
    String input = "";
    while (input.length() == 0) {
      while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') break;
        input += c;
      }
    }
    if (input == "no") break;
  }

  Serial.println("\n--- CSV complete ---");
  while (1);
}

// void loop() {
//   Serial.println("\n--- Logging Menu ---");
//   Serial.println("Enter logging duration in seconds: ");
//   String input = "";
//   while (input.length() == 0) {
//     while (Serial.available()) {
//       char c = Serial.read();
//       if (c == '\n' || c == '\r') break;
//       input += c;
//     }
//   }
//   uint32_t duration_ms = input.toInt() * 1000UL;
//   if (duration_ms == 0) duration_ms = 60000;

//   Serial.println("Enter sampling rate in Hz: ");
//   input = "";
//   while (input.length() == 0) {
//     while (Serial.available()) {
//       char c = Serial.read();
//       if (c == '\n' || c == '\r') break;
//       input += c;
//     }
//   }
//   uint16_t sample_rate_hz = input.toInt();
//   if (sample_rate_hz == 0 || sample_rate_hz > 100) sample_rate_hz = 10;
//   uint32_t interval = 1000000 / sample_rate_hz; // µs
//   uint32_t nextTick = micros();

//   Serial.printf("Logging for %lu seconds at %u Hz...\n", duration_ms / 1000, sample_rate_hz);
//   Serial.println("timestamp_iso,ax,ay,az,gx,gy,gz,mx,my,mz,counter");
//   // Serial.println("timestamp_iso,ax,ay,az,gx,gy,gz,stamp");

//   //int32_t data_num = 0;
//   String str_acc = "";
//   String str_gyr = "";
//   String str_mag = "";
//   uint8_t count_mag = 0; // set to read every 4 times
//   //String line = "";

//   uint32_t startTime = millis();
//   //char lineBuf[160];  // adjust depending on number of fields
//   uint8_t mag_count = 0;

//   while (millis() - startTime < duration_ms) {
//     // Timestamp (ISO format)
//     time_t now_t = time(NULL);
//     struct tm* tm_info = gmtime(&now_t);
//     char timestamp[25];
//     strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

//     // Sensor readings
//     float ax = 0, ay = 0, az = 0;
//     float gx = 0, gy = 0, gz = 0;
//     float mx = 0, my = 0, mz = 0;

//     if (acc_active) IMU.readAcceleration(ax, ay, az);
//     if (gyr_active) IMU.readGyroscope(gx, gy, gz);
//     if (mag_active && mag_count == 0) mag.readData(&mx, &my, &mz);
//     mag_count = (mag_count + 1) % 4;

//       SensorRecord rec;
//       rec.timestamp = (uint32_t)time(NULL);
//       rec.ax = (int16_t)(ax * 1000);   // scale to milli-g
//       rec.ay = (int16_t)(ay * 1000);
//       rec.az = (int16_t)(az * 1000);
//       rec.gx = (int16_t)(gx * 100);    // scale to centi-dps
//       rec.gy = (int16_t)(gy * 100);
//       rec.gz = (int16_t)(gz * 100);
//       rec.mx = (int16_t)(mx * 10);     // scale to tenths of µT
//       rec.my = (int16_t)(my * 10);
//       rec.mz = (int16_t)(mz * 10);
//       rec.counter = data_num;

//       logger.logRecord(rec);


//     // --- Timing control ---
//     nextTick += interval;
//     while ((long)(micros() - nextTick) < 0) {
//       // Optionally yield or light sleep here
//     }

//     data_num++;
//   }

//   //  Serial.println("Last line:");
//   Serial.println(data_num);
//   logger.flush();
//   Serial.println("Logging complete. Dumping logs...\n");

//   Serial.println("Turning on wifi. 30sec to connect...\n");
//   startWiFi();

//   delay(30000);


//   for (uint16_t i = 0; i < 5; i++) {
//     server.handleClient();
//     delay(10);
//     Serial.println("\n--- handled ---");
//     Serial.println("\n--- write yes to read again, no to exit read loop ---");
//     String input = "";
//     while (input.length() == 0) {
//       while (Serial.available()) {
//         char c = Serial.read();
//         if (c == '\n' || c == '\r') break;
//         input += c;
//       }
//     }
//     if (input == "no"){
//       break;
//     }
//   }


//   Serial.println("\n--- CSV complete ---");
//   while (1);
// }
