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

uint32_t data_num = 0;
// Sensors
bool acc_active = true;
bool gyr_active = true;
bool mag_active = true;
uint32_t duration_ms = 60000;       // Default: 60 seconds
uint16_t sample_rate_hz = 10;       // Default: 10 Hz
bool configReceived = false;
Adafruit_MLX90393 mag = Adafruit_MLX90393();

// Switch and LED settings
#define LED_PIN 2        // D0 on XIAO ESP32-C3 (GPIO2)
#define BUTTON_PIN 9     // BOOT button is on GPIO9
volatile bool buttonInterruptTriggered = false;
unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 200;  // ms debounce
bool interruptsAllowed = false;
volatile bool longPressDetected = false;
unsigned long buttonPressStart = 0;
bool buttonHeld = false;

// WiFi credentials for Access Point
const char* ssid = "TOL-001";
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

    configReceived = true;

    Serial.printf("✔ Config received: %lu ms, %u Hz\n", duration_ms, sample_rate_hz);
    Serial.printf("✔ Config received: accel: %, gyro: %, magne: %", acc_active, gyr_active, mag_active);
    server.send(200, "text/plain", "✔ Logging config received.");
  } else {
    server.send(400, "text/plain", "❌ Missing 'duration' or 'rate' parameter.");
  }
}

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

void IRAM_ATTR handleButtonPress() {
  if (!interruptsAllowed) return;  // Ignore until setup is done

  unsigned long currentTime = millis();
  if ((currentTime - lastInterruptTime) > debounceDelay) {
    buttonInterruptTriggered = true;
    lastInterruptTime = currentTime;
  }
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
  server.on("/send", handleLoggingConfig);

  // Start server
  server.begin();
  Serial.println("HTTP server started");

  
  //Internal LED setup
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Ensure it's off initially
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
  //LED off for standby state
  digitalWrite(LED_PIN, LOW);
  
  // Enable interrupt trigger AFTER things are stable
  interruptsAllowed = true;

  Serial.println("Press button to start WiFi.");
  
  while (1) {

    if (buttonInterruptTriggered) {
      Serial.println("👉 BOOT button interrupt triggered! Starting WiFi");
      buttonInterruptTriggered = false;
      break;
    }
  }
  //interruptsAllowed = false;
  digitalWrite(LED_PIN, LOW);

  startWiFi();

  Serial.println("\n--- Logging Menu ---");
  Serial.println("Input duration and sampling rate in GUI: ");
  unsigned long longPressStart = 0;
  bool isPressing = false;
  bool ledState_init = false;
  // Waiting for either config to be sent from GUI via WIFI or for a long button press to activate default settings
  while (!configReceived && !longPressDetected) {
    server.handleClient();

    // Check for long press
    if (digitalRead(BUTTON_PIN) == LOW) {
      if (!isPressing) {
        longPressStart = millis();
        isPressing = true;
      } else {
        if (millis() - longPressStart >= 3000) {
          longPressDetected = true;
          configReceived = true;
          Serial.println("Long button press detected. Using default config.");
          break;
        }
      }
    } else {
      isPressing = false;
    }

    delay(100);
    ledState_init = !ledState_init;
    if (ledState_init == true) {
      //printing less regularily
      Serial.println("Waiting for logging config via /send?duration=60&rate=10 or long press...");
      digitalWrite(LED_PIN, HIGH); // On
    } else {
      digitalWrite(LED_PIN, LOW); // Off
    }
    delay(400);
  }

  shutdownWiFi();

  uint32_t interval_us = 1000000UL / sample_rate_hz;  // sampling interval in microseconds

  Serial.printf("Logging for %lu seconds at %u Hz...\n", duration_ms / 1000, sample_rate_hz);
  

  // all is setup do another button press to start the measurement
  // Enable interrupt trigger AFTER things are stable
  interruptsAllowed = true;
  while (1) {
    digitalWrite(LED_PIN, HIGH);

    if (buttonInterruptTriggered) {
      Serial.println("👉 BOOT button interrupt triggered! Starting test");
      buttonInterruptTriggered = false;
      break;
    }
  }
  // Disable interrupt trigger AFTER things are stable
  interruptsAllowed = false;
  digitalWrite(LED_PIN, LOW);

  Serial.println("timestamp,ax,ay,az,gx,gy,gz,mx,my,mz,counter");


  // --- Get initial time ---
  struct timeval tv;
  gettimeofday(&tv, NULL);
  //uint64_t base_timestamp_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;

  // --- Setup for LED blinking during meas ---
  uint32_t lastBlinkTime = 0;
  bool ledState = false;
  const uint32_t blinkInterval = 3000;  // Blink every 3 s
  // Improvement make it so ON/OFF intevals are different - minimal LED on time

  // --- Loop control ---
  uint32_t startTime = millis();
  //uint64_t current_timestamp_us = base_timestamp_us;
  uint32_t current_timestamp_us = 0;
  uint8_t mag_count = 0;
  uint32_t nextTick = micros();
  // Init sensor readings:
  float ax = 0, ay = 0, az = 0;
  float gx = 0, gy = 0, gz = 0;
  float mx = 0, my = 0, mz = 0;

  //Calculate Magnetometer rate with modulo:
  uint8_t mag_modulo = 1;
  if (sample_rate_hz >= 50) mag_modulo = 4;
  if (sample_rate_hz < 50 && sample_rate_hz >= 25) mag_modulo = 2;
  

  while (millis() - startTime < duration_ms) {
    // --- Sensor readings ---
    if (acc_active) IMU.readAcceleration(ax, ay, az);
    if (gyr_active) IMU.readGyroscope(gx, gy, gz);
    if (mag_active && mag_count == 0) mag.readData(&mx, &my, &mz);
    mag_count = (mag_count + 1) % mag_modulo;

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

    // --- Timing control ---
    nextTick += interval_us;  // schedule next tick
    while ((int32_t)(micros() - nextTick) < 0) {
      // optional:
      // yield();  // allow background tasks
    }

    data_num++;

    // Handle LED blinking without delay
    if (millis() - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = millis();
      ledState = !ledState;
      if (ledState == true) {
        digitalWrite(LED_PIN, HIGH); // On
      } else {
        digitalWrite(LED_PIN, LOW); // Off
      } 
    }

    // Detect long button press (3 seconds)
    static bool buttonHeld = false;
    static unsigned long buttonPressStart = 0;

    if (digitalRead(BUTTON_PIN) == LOW) {
      if (!buttonHeld) {
        buttonHeld = true;
        buttonPressStart = millis();
      } else {
        if (millis() - buttonPressStart >= 3000) {
          Serial.println("Long press detected! (3 seconds). Exiting measurement loop");
          // Add any special logic here (e.g., cancel logging, add marker, etc.)
          break; // ← Optional early exit
        }
      }
    } else {
      buttonHeld = false;
    }
  }

  digitalWrite(LED_PIN, LOW);

  logger.flush();
  Serial.println("Logging complete.");

  // CRITICAL: Properly disable SPI before using GPIO9 for button
  disableSPIForButton();

  // Enable interrupt trigger AFTER SPI is disabled
  interruptsAllowed = true;
  //USB download false
  bool USB_download = false;
  bool skip_download = false;

  unsigned long buttonHoldStart = 0;
  bool buttonHeld = false;
  bool buttonReleased = true;

  Serial.println("Press BOOT button briefly to download, or hold ≥3 s to skip download.");

  while (1) {
    digitalWrite(LED_PIN, HIGH);
    delay(400);
    digitalWrite(LED_PIN, LOW);
    delay(800);

    // if (buttonInterruptTriggered) {
    //   buttonInterruptTriggered = false;
    //   Serial.println("👉 BOOT button interrupt triggered!");
    //   break;
    // }
    // --- Button handling (no interrupt use here) ---
    int buttonState = digitalRead(BUTTON_PIN);

    if (buttonState == LOW) {
      if (buttonReleased) {
        // button just pressed
        buttonReleased = false;
        buttonHoldStart = millis();
      } else {
        // button is being held
        if (millis() - buttonHoldStart >= 3000) {
          Serial.println("⏭ Long press detected — skipping download step.");
          skip_download = true;
          break;
        }
      }
    } else {
      // Button is released
      if (!buttonReleased) {
        unsigned long pressDuration = millis() - buttonHoldStart;
        if (pressDuration < 3000) {
          Serial.println("👉 Short press detected — entering download mode.");
          break;
        }
        buttonReleased = true;
      }
    }
    if (Serial.available()) {
      String command = Serial.readStringUntil('\n');
      command.trim();

      if (command == "DOWNLOAD") {
        USB_download = true;
        break;     
      }
    }
  }
  // Disable interrupt trigger
  interruptsAllowed = false;
  digitalWrite(LED_PIN, LOW);
  
  // CRITICAL: Re-enable SPI before accessing flash
  enableSPIForFlash();

  if (skip_download) {
    Serial.println("Skipping Wi-Fi/USB data transfer. Returning to standby...");
  } else if (USB_download == false) {
    startWiFi();
    delay(30000);  // Give WiFi time to start
    Serial.println("=== BEGIN CSV ===");
  } else {
    Serial.println("=== BEGIN CSV ===");
  }

  //LED on during flash read
  digitalWrite(LED_PIN, HIGH);
  // Dump logs
  server.handleClient();
  delay(10);

  // for (uint16_t i = 0; i < 5; i++) {
  //   server.handleClient();
  //   delay(10);
  //   Serial.println("\n--- handled ---");
  //   Serial.println("\n--- write yes to read again, no to exit read loop ---");
  //   String input = "";
  //   while (input.length() == 0) {
  //     while (Serial.available()) {
  //       char c = Serial.read();
  //       if (c == '\n' || c == '\r') break;
  //       input += c;
  //     }
  //   }
  //   if (input == "no") break;
  // }

  digitalWrite(LED_PIN, LOW);

  Serial.println("\n--- CSV complete ---");
  
  delay(5000);

  // Reset flag to wait again for next config
  configReceived = false;
  shutdownWiFi();

  // Disable SPI again before waiting for button
  disableSPIForButton();

  // --- Wait for button press to reboot ---
  // Serial.println("Press BOOT button to reboot and restart setup...");

  // // Enable interrupt trigger AFTER things are stable
  // interruptsAllowed = true;
  // while (1) {
  //   digitalWrite(LED_PIN, HIGH);
  //   delay(200);
  //   digitalWrite(LED_PIN, LOW);
  //   delay(2000);

  //   if (buttonInterruptTriggered) {
  //     Serial.println("👉 BOOT button interrupt triggered! Rebooting system");
  //     buttonInterruptTriggered = false;
  //     break;
  //   }
  // }
  // // Disable interrupt trigger AFTER things are stable
  // interruptsAllowed = false;
  Serial.println("System rebooting and void setup running...");
  ESP.restart();   // 🌀 full software reboot (runs setup() again)
}
