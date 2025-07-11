#include <Wire.h>
#include <time.h>
#include "W25N01GV.h"
#include "BadBlockManager.h"
#include "Logger.h"

#include <Adafruit_MLX90393.h>
#include <Arduino_BMI270_BMM150.h>

#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// Flash
#define FLASH_CS D7
W25N01GV flash(FLASH_CS);
BadBlockManager bbm(flash);
Logger logger(flash, bbm);

// Sensors
Adafruit_MLX90393 mag = Adafruit_MLX90393();
#define LED_PIN 2        // D0 on XIAO ESP32-C3 (GPIO2)
#define LED_COUNT  1      // Only one NeoPixel
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
#define BUTTON_PIN 9     // BOOT button is on GPIO9
volatile bool buttonInterruptTriggered = false;
unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 200;  // ms debounce
bool interruptsAllowed = false;

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

// Global variables for logging
uint32_t duration_ms = 60000;       // Default: 60 seconds
uint16_t sample_rate_hz = 10;       // Default: 10 Hz
uint32_t interval = 100;            // Calculated from sample_rate_hz
bool configReceived = false;
bool acc_active = true;
bool gyr_active = true;
bool mag_active = true;


// Function to properly disable SPI to free up GPIO9
void disableSPIForButton() {
  // End SPI communication
  SPI.end();
  // Set CS high to deselect flash
  digitalWrite(FLASH_CS, HIGH);
  // Configure GPIO9 as input with pullup for button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Small delay to let pin settle
  delay(10);
  
  Serial.println("SPI disabled, GPIO9 configured for button");
}

// Function to re-enable SPI for flash access
void enableSPIForFlash() {
  // Reconfigure SPI
  SPI.begin();
  // Reinitialize flash
  flash.setSPIFrequency(1000000);
  // Set CS as output and select flash
  pinMode(FLASH_CS, OUTPUT);
  digitalWrite(FLASH_CS, LOW);
  // Small delay to let SPI settle
  delay(10);
  
  Serial.println("SPI re-enabled for flash access");
}

// Function to handle CSV data request
void handleData() {
  String csvData = "timestamp_iso,ax,ay,az,gx,gy,gz,mx,my,mz\n";
  
  csvData += String(dat) + "\n";

  Serial.println("\n Data sent: \n");
  Serial.println(csvData);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/csv", csvData);

  Serial.println("\n GET DATA \n");
  delay(30);
}

void IRAM_ATTR handleButtonPress() {
  if (!interruptsAllowed) return;  // Ignore until setup is done

  unsigned long currentTime = millis();
  if ((currentTime - lastInterruptTime) > debounceDelay) {
    buttonInterruptTriggered = true;
    lastInterruptTime = currentTime;
  }
}

void handleLoggingConfig() {
  if (server.hasArg("duration") && server.hasArg("rate")) {
    for (int i = 0; i < server.args(); i++) {
      Serial.printf(" - %s = %s\n", server.argName(i).c_str(), server.arg(i).c_str());
    }

    duration_ms = server.arg("duration").toInt() * 1000UL;
    sample_rate_hz = server.arg("rate").toInt();
    
    acc_active = (server.arg("accelerometer") == "true");
    gyr_active = (server.arg("gyroscope") == "true");
    mag_active = (server.arg("magnetometer") == "true");

    if (duration_ms == 0) duration_ms = 60000;
    if (sample_rate_hz == 0 || sample_rate_hz > 100) sample_rate_hz = 10;

    interval = 1000 / sample_rate_hz;
    configReceived = true;

    Serial.printf("✔ Config received: %lu ms, %u Hz\n", duration_ms, sample_rate_hz);
    Serial.printf("✔ Config received: accel: %, gyro: %, magne: %", acc_active, gyr_active, mag_active);
    server.send(200, "text/plain", "✔ Logging config received.");
  } else {
    server.send(400, "text/plain", "❌ Missing 'duration' or 'rate' parameter.");
  }
}

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting multi-sensor NAND logger...");

  // I2C setup (Xiao ESP32-C3: SDA = 8, SCL = 9)
  Wire.begin();
  Wire.setClock(100000);
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
  server.on("/send", handleLoggingConfig);

  // Start server
  server.begin();
  Serial.println("HTTP server started");

  //Neopixel LED setup
  strip.setBrightness(50);  // Set brightness (0-255)
  //Boot button setup
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Wait for button to be released and settle
  while (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Waiting for button release...");
    delay(50);
  }
  // Small delay to ensure pin settles
  delay(100);
  // Attach interrupt but don't allow it to trigger yet
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);
  // Set debounce timer
  lastInterruptTime = millis();
}

void loop() {
  
  startWiFi();

  // Wait for config
  while (!configReceived) {
    server.handleClient();
    delay(100);
    Serial.println("Waiting for logging config via /send?duration=60&rate=10");
    delay(1000);
  }

  shutdownWiFi();

  // //Green flashing ligh ready to start recording
  // // CRITICAL: Properly disable SPI before using GPIO9 for button
  // disableSPIForButton();
  // // Enable interrupt trigger AFTER SPI is disabled
  // interruptsAllowed = true;
  // while (1) {
  //   strip.setPixelColor(0, strip.Color(0, 30, 0)); // Green
  //   strip.show();
  //   delay(1000);
  //   strip.setPixelColor(0, strip.Color(0, 0, 0)); // Off
  //   strip.show();
  //   delay(2000);

  //   if (buttonInterruptTriggered) {
  //     buttonInterruptTriggered = false;
  //     Serial.println("👉 BOOT button triggered - measurement starting");
  //     break;
  //   }
  // }
  // // Disable interrupt trigger
  // interruptsAllowed = false;
  // strip.setPixelColor(0, strip.Color(0, 0, 0)); // Off
  // strip.show();
  // // CRITICAL: Re-enable SPI before accessing flash
  // enableSPIForFlash();
  // delay(1000);

  // Once config is received, start logging
  Serial.printf("Starting logging for %lu ms at %u Hz...\n", duration_ms, sample_rate_hz);

  uint32_t startTime = millis();
  uint32_t lastBlinkTime = 0;
  bool ledState = false;
  const uint32_t blinkInterval = 1000;  // Blink every 500 ms
  while (millis() - startTime < duration_ms) {
    
    //server.handleClient();

    // Timestamp
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    char timestamp[25];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    // BMI270 readings
    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    if (acc_active == true) {
      if (IMU.accelerationAvailable()) IMU.readAcceleration(ax, ay, az);
    }
    if (gyr_active == true) {
      if (IMU.gyroscopeAvailable()) IMU.readGyroscope(gx, gy, gz);
    }

    // MLX90393 readings
    float mx = 0, my = 0, mz = 0;
    if (mag_active == true) {
      mag.readData(&mx, &my, &mz);
    }
    
    String line = String(timestamp) + "," +
                  String(ax,3) + "," + String(ay,3) + "," + String(az,3) + "," +
                  String(gx,2) + "," + String(gy,2) + "," + String(gz,2) + "," +
                  String(mx,2) + "," + String(my,2) + "," + String(mz,2);

    logger.logRecord(line);
    Serial.println(line);
    delay(interval);

    // Handle LED blinking without delay
    if (millis() - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = millis();
      ledState = !ledState;
      if (ledState == true) {
        strip.setPixelColor(0, strip.Color(20, 0, 0)); // On
      } else {
        strip.setPixelColor(0, strip.Color(0, 0, 0)); // Off
      } 
      strip.show();
    }
  }

  logger.flush();
  Serial.println("✔ Logging complete. Dumping logs...");

  // CRITICAL: Properly disable SPI before using GPIO9 for button
  disableSPIForButton();

  // Enable interrupt trigger AFTER SPI is disabled
  interruptsAllowed = true;
  while (1) {
    strip.setPixelColor(0, strip.Color(0, 0, 30)); // Blue
    strip.show();
    delay(500);
    strip.setPixelColor(0, strip.Color(0, 0, 0)); // Off
    strip.show();
    delay(2000);

    if (buttonInterruptTriggered) {
      buttonInterruptTriggered = false;
      Serial.println("👉 BOOT button interrupt triggered!");
      break;
    }
  }
  // Disable interrupt trigger
  interruptsAllowed = false;
  digitalWrite(LED_PIN, LOW);
  
  // CRITICAL: Re-enable SPI before accessing flash
  enableSPIForFlash();
  
  startWiFi();
  delay(30000);  // Give WiFi time to start

  // Dump logs
  dat = "";
  uint8_t readBuf[2048];
  uint16_t lastPage = logger.getCurrentPage();
  for (uint16_t page = 0; page < lastPage; page++) {
    flash.readPage(page, readBuf, 2048);
    for (uint16_t i = 0; i < 2048; i++) {
      char c = (char)readBuf[i];
      if (c == 0xFF || c == 0) break;
      Serial.print(c);
      dat += c;
    }
    server.handleClient();
    delay(500);
  }

  Serial.println("✔ CSV ready to be fetched");
  server.handleClient();
  delay(10000);

  // Reset flag to wait again for next config
  configReceived = false;
  shutdownWiFi();

  // Disable SPI again before waiting for button
  disableSPIForButton();

  // Enable interrupt trigger AFTER things are stable
  interruptsAllowed = true;
  while (1) {
    strip.setPixelColor(0, strip.Color(10, 0, 10)); // Red
    strip.show();
    delay(5000);

    if (buttonInterruptTriggered) {
      Serial.println("👉 BOOT button interrupt triggered! Restart wifi");
      buttonInterruptTriggered = false;
      break;
    }
  }
  // Disable interrupt trigger AFTER things are stable
  interruptsAllowed = false;

}

