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

//Function to handle CSV data request
void handleData() {
  // Tell the browser CORS is allowed
  server.sendHeader("Access-Control-Allow-Origin", "*");
  // Start HTTP 200 response with CSV content type
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", ""); // headers
  server.sendContent("timestamp_iso,ax,ay,az,gx,gy,gz,mx,my,mz,counter\n");
  // server.sendContent("timestamp_iso,ax,ay,az,gx,gy,gz,counter\n");

  uint8_t readBuf[2048];
  uint16_t lastPage = logger.getCurrentPage();

  for (uint16_t page = 0; page < lastPage; page++) {
    if (!bbm.isBlockGood(page / 64)) continue;
    if (!flash.readPage(page, readBuf, sizeof(readBuf))) continue;

    String chunk = "";
    for (uint16_t i = 0; i < sizeof(readBuf); i++) {
      char c = (char)readBuf[i];
      if (c == (char)0xFF || c == 0) break;
      //Serial.print(c);

      if ((c >= 32 && c <= 126) || c == '\n' || c == '\r') {
        chunk += c;
      }
    }

    if (chunk.length() > 0) {
      server.sendContent(chunk);  // send each page as soon as it’s ready
    }

    delay(2); // let WiFi stack breathe
  }

  server.sendContent(""); // end response
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
  uint32_t interval = 1000000 / sample_rate_hz; // µs
  uint32_t nextTick = micros();

  Serial.printf("Logging for %lu seconds at %u Hz...\n", duration_ms / 1000, sample_rate_hz);
  Serial.println("timestamp_iso,ax,ay,az,gx,gy,gz,mx,my,mz,counter");
  // Serial.println("timestamp_iso,ax,ay,az,gx,gy,gz,stamp");

  int16_t data_num = 0;
  String str_acc = "";
  String str_gyr = "";
  String str_mag = "";
  uint8_t count_mag = 0; // set to read every 4 times
  String line = "";

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
    if (acc_active) {
      // if (IMU.accelerationAvailable()) IMU.readAcceleration(ax, ay, az);
      IMU.readAcceleration(ax, ay, az);
      str_acc = "," + String(ax,3) + "," + String(ay,3) + "," + String(az,3);
    } else {
      str_acc = "";
    }
    if (gyr_active) {
      // if (IMU.gyroscopeAvailable()) IMU.readGyroscope(gx, gy, gz);
      IMU.readGyroscope(gx, gy, gz);
      str_gyr = "," + String(gx,2) + "," + String(gy,2) + "," + String(gz,2);
    } else {
      str_gyr = "";
    }

    // MLX90393 readings
    float mx = 0, my = 0, mz = 0;
    if (mag_active) {
      if (count_mag == 0) {
        mag.readData(&mx, &my, &mz);
        str_mag = "," + String(mx,2) + "," + String(my,2) + "," + String(mz,2);
      }
      count_mag = (count_mag + 1) % 4;      
    } else {
      str_mag = "";
    }

    // Format CSV
    line = String(timestamp) + str_acc + str_gyr + str_mag + "," + String(data_num);

    logger.logRecord(line);
    Serial.println(line);

    // --- Timing control ---
    nextTick += interval;  // schedule next tick
    while ((long)(micros() - nextTick) < 0) {
      // optional: low-power sleep here
      // yield();  // allow background tasks
    }

    data_num ++;
  }
  Serial.println("Last line:");
  Serial.println(line);
  logger.flush();
  Serial.println("Logging complete. Dumping logs...\n");

  Serial.println("Turning on wifi. 30sec to connect...\n");
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
    if (input == "no"){
      break;
    }
  }


  Serial.println("\n--- CSV complete ---");
  while (1);
}
