
/*************************************************************
  Blynk Automated Fan System: CSN505 Project
  
  WHAT THIS DOES:
  - Measures Temperature and Humidity using a DHT11 sensor and automatically controls a fan.
  - Has a "Fail-Safe" feature that turns fan to max if the sensor breaks/unplugs.
  - Has a "RPM Smoothing" feature that prevents the RPM display from jumping around.
  - Has a feature to allow the fan to work even if the WiFi is down.
  - Allows a user to "Force" the fan ON via a button on the web or mobile dashboard UI.
  - Sends a phone notification/email when the fan has started.
  - LCD Display: Shows Temp, Humidity, Mode (Auto/Manual), and RPM.
 
 *************************************************************/

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #error "Missing secrets.h! Please make a copy of secrets.template.h, rename it to secrets.h and fill in the required information."
#endif

/* Populates Blynk credentials using information set in secrets.h file */
#define BLYNK_TEMPLATE_ID    SECRET_BLYNK_TEMPLATE_ID
#define BLYNK_TEMPLATE_NAME  SECRET_BLYNK_TEMPLATE_NAME
#define BLYNK_AUTH_TOKEN     SECRET_BLYNK_AUTH_TOKEN

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- NETWORK SETTINGS ---
char ssid[] = SECRET_SSID; 
char pass[] = SECRET_PASS;

// --- PIN ASSIGNMENTS (Where wires connect to the ESP32) ---
#define DHTPIN 4          // DHT11 Sensor Pin
#define DHTTYPE DHT11     // Type of sensor used
#define FAN_PWM_PIN 18    // Fan Speed Control Pin
#define FAN_TACH_PIN 5    // Fan Speed Sensor (RPM) Pin

// --- SYSTEM SETTINGS (The "Brain" Rules) ---
float tempThreshold = 30.0; // Start fan if the detected temperature goes ABOVE this (Adjustable via V11)
float humThreshold = 70.0;  // Start fan if the detected humidity goes ABOVE this (Adjustable via V12)
int errorCount = 0;         // Tracks how many times the sensor failed to read

// --- SYSTEM MEMORY (Variables the computer tracks) ---
volatile int pulseCount = 0;   // Counts raw fan rotations
bool fanActive = false;        // Is the fan currently spinning?
bool manualOverride = false;   // Did the user press the "Force ON" button?
bool notificationSent = false; // Ensures we only send ONE alert per start

// --- DATA SMOOTHING (Prevents the RPM display from being "jumpy") ---
const int numReadings = 5;     // Number of readings to average
int readings[numReadings]; 
int readIndex = 0;
int total = 0;
int averageRpm = 0;

DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// This sub-routine counts every pulse the fan sends as it spins
void IRAM_ATTR countPulse() {
  pulseCount++;
}

// --- APP CONTROLS (Responding to your phone) ---

// This runs when you flip the "Force ON" switch in the app
BLYNK_WRITE(V10) {
  manualOverride = param.asInt();
}

// This runs when you slide the "Temperature Target" slider (V11)
BLYNK_WRITE(V11) {
  tempThreshold = param.asFloat();
}

// This runs when you slide the "Humidity Target" slider (V12)
BLYNK_WRITE(V12) {
  humThreshold = param.asFloat();
}

// --- THE MAIN LOGIC (Runs every 5 seconds) ---
void updateSystem() {
  // 1. Get data from the sensor
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // 2. Calculate Fan Speed (RPM) with Smoothing
  // We check every 5 seconds, so we multiply by 12 to get "Pulses per Minute"
  int currentRpm = (pulseCount * 12); 
  pulseCount = 0; // Reset counter for the next 5 seconds
  
  total = total - readings[readIndex];    // Remove the oldest reading
  readings[readIndex] = currentRpm;       // Add the newest reading
  total = total + readings[readIndex];    // Add it to the running total
  readIndex = (readIndex + 1) % numReadings;
  averageRpm = total / numReadings;       // The "Smooth" number we show on the LCD

  // 3. FAIL-SAFE: Check if the sensor is broken or unplugged
  bool sensorError = isnan(h) || isnan(t);
  if (sensorError) {
    errorCount++;
    if (errorCount >= 5) {
      analogWrite(FAN_PWM_PIN, 255); // Force Fan to 100% for safety!
      if (Blynk.connected()) Blynk.logEvent("error", "Sensor Failure! Fan forced to MAX.");
    }
  } else {
    errorCount = 0; // Reset error count if sensor starts working again
  }

  // 4. DECISION LOGIC: Should the fan be on?
  // We stay ON if: Manual is ON -OR- (Sensor is OK AND Temp/Hum is high)
  bool shouldBeOn = (manualOverride || (!sensorError && (t >= tempThreshold || h >= humThreshold)));

  // 5. ACTION: Turn fan hardware ON or OFF
  if (shouldBeOn) {
    analogWrite(FAN_PWM_PIN, 255); // Give the fan full power
    
    // Send a notification if the fan just started
    if (!fanActive && !notificationSent && Blynk.connected()) {
      String source = manualOverride ? "Manual Activation" : "Sensor";
      String message = "Fan Active via " + source + "! T:" + String(t,1) + "C, H:" + String(h,0) + "%";
      
	  Blynk.logEvent("fan_on", message); // Sends Push/Email via Blynk Consol
	  notificationSent = true;
    }
    fanActive = true;
  } else {
    analogWrite(FAN_PWM_PIN, 0); // Turn the fan off
    fanActive = false;
    notificationSent = false;
  }

  // 6. SYNC: Send the data to your phone (Only if the internet is working)
  if (Blynk.connected()) {
    Blynk.virtualWrite(V5, t);
    Blynk.virtualWrite(V6, h);
    Blynk.virtualWrite(V7, averageRpm);
  }

  // 7. DISPLAY: Show data on the physical LCD screen (Always works offline)
  lcd.clear();
  lcd.setCursor(0, 0);
  if (sensorError) {
    lcd.print("SENSOR ERROR!");
  } else {
    // Top Row: T:XXC H:XX%
    lcd.print("T:"); lcd.print((int)t); lcd.print("C  ");
    lcd.print("H:"); lcd.print((int)h); lcd.print("%");
  }

  lcd.setCursor(0, 1);
  // Bottom Row: MODE:XXX R:XXXX
  lcd.print("MODE:");
  lcd.print(manualOverride ? "MAN" : "AUT");
  
  lcd.setCursor(9, 1); 
  lcd.print("R:"); lcd.print(averageRpm);
}

// THIS RUNS ONCE when you turn the ESP32 on
void setup() {
  Serial.begin(115200);

  // Wake up the LCD
  lcd.init();
  lcd.backlight();
  lcd.print("System Loading...");

  // Wake up the sensor and fan controls
  dht.begin();
  pinMode(FAN_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), countPulse, FALLING);
  pinMode(FAN_PWM_PIN, OUTPUT);
  
  // Set the "Smoothing" memory to zero
  for (int i = 0; i < numReadings; i++) readings[i] = 0;

  // Connect to Blynk (Non-Blocking Mode)
  // This allows the fan logic to keep running even if WiFi drops!
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connectWiFi(ssid, pass);
  
  // Run the fan logic every 5 seconds (5000L)
  timer.setInterval(5000L, updateSystem);
}

// THIS RUNS REPETITIVELY as fast as possible
void loop() {
  if (Blynk.connected()) {
    Blynk.run(); // Keep phone connection alive
  }
  timer.run(); // Check if it's time to run "updateSystem"
}