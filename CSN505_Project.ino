/*************************************************************
  Blynk Automated Fan System with Notifications & Override
  
  WHAT THIS DOES:
  - Measures Temperature and Humidity using a DHT11 sensor.
  - Automatically turns a fan ON if it gets too hot or humid.
  - Automatically turns the fan OFF once it cools down (Hysteresis).
  - Allows a user to "Force" the fan ON via a phone app button.
  - Sends a phone notification/email when the fan starts.
  - Displays all data on a physical 16x2 LCD screen.

  WARNING:
  For this example you'll need Adafruit DHT sensor libraries:
    https://github.com/adafruit/Adafruit_Sensor
    https://github.com/adafruit/DHT-sensor-library

  App dashboard setup:
    - Value Display widget attached to V5 (Temperature)
    - Value Display widget attached to V6 (Humidity)
    - Value Display widget attached to V7 (Fan RPM)
    - Switch widget attached to V10 (Manual Override)
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

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
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
const float TEMP_HIGH = 30.0; // Start fan if Temp goes ABOVE this
const float HUM_HIGH  = 70.0; // Start fan if Humidity goes ABOVE this
const float TEMP_LOW  = 25.0; // Stop fan only when Temp falls BELOW this
const float HUM_LOW   = 65.0; // Stop fan only when Humidity falls BELOW this

// --- SYSTEM MEMORY (Variables the computer tracks) ---
volatile int pulseCount = 0;   // Counts fan rotations
int rpm = 0;                   // Stores calculated speed
bool fanActive = false;        // Is the fan currently spinning?
bool manualOverride = false;   // Did the user press the "Force ON" button?
bool notificationSent = false; // Ensures we only send ONE alert per start

DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// This sub-routine counts every pulse the fan sends as it spins
void IRAM_ATTR countPulse() {
  pulseCount++;
}

// This runs whenever you flip the switch on either the Blynk web or mobile interfaces
BLYNK_WRITE(V10) {
  manualOverride = param.asInt(); // Sets to 1 (True) or 0 (False)
  Serial.print("Manual Switch flipped: ");
  Serial.println(manualOverride ? "ON" : "OFF");
}

// This is the MAIN logic that runs every 2 seconds
void updateSystem() {
  // 1. Get data from the sensor
  float h = dht.readHumidity();
  float t = dht.readTemperature(); 

  // 2. Calculate Fan Speed (RPM)
  rpm = (pulseCount * 12); 
  pulseCount = 0;

  // 3. Error Check: Stop if the sensor is unplugged
  if (isnan(h) || isnan(t)) {
    Serial.println("Error: Cannot read sensor!");
    return;
  }

  // 4. DECISION LOGIC: Should the fan be on?
  bool shouldBeOn = false;

  if (manualOverride) {
    shouldBeOn = true; // User forced it ON
  } else {
    // If it was OFF, check if it's too hot/humid to start
    if (!fanActive) {
      if (t >= TEMP_HIGH || h >= HUM_HIGH) shouldBeOn = true;
    } 
    // If it was ALREADY ON, stay on until it hits the LOW targets
    else {
      if (t > TEMP_LOW || h > HUM_LOW) shouldBeOn = true;
    }
  }

  // 5. ACTION: Turn fan hardware ON or OFF
  if (shouldBeOn) {
    analogWrite(FAN_PWM_PIN, 255); // Full power to the fan
    
    // 6. NOTIFICATION: Alert the user if the fan just started
    if (!fanActive && !notificationSent) {
      String source = manualOverride ? "Manual Activation" : "Sensor";
      String message = "Fan Active via " + source + "! T:" + String(t,1) + "C, H:" + String(h,0) + "%";
      
      Blynk.logEvent("fan_on", message); // Sends Push/Email via Blynk Console
      notificationSent = true; 
    }
    fanActive = true; 
  } else {
    analogWrite(FAN_PWM_PIN, 0);   // Cut power to the fan
    fanActive = false;
    notificationSent = false;      // Ready to notify again next time
  }

  // 7. SYNC: Send the data to your phone screen
  Blynk.virtualWrite(V5, t);
  Blynk.virtualWrite(V6, h);
  Blynk.virtualWrite(V7, rpm);

  // 8. DISPLAY: Show data on the physical LCD screen
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T:"); lcd.print((int)t); lcd.print("C ");
  lcd.print("H:"); lcd.print((int)h); lcd.print("%");
  
  lcd.setCursor(0, 1);
  if (manualOverride) lcd.print("MANUAL-ON");
  else lcd.print(fanActive ? "AUTO-ON" : "AUTO-OFF");
  
  lcd.setCursor(10, 1); // Move cursor to the end of the second line
  lcd.print("R:"); lcd.print(rpm);
}

// THIS RUNS ONCE when you turn the ESP32 on
void setup() {
  Serial.begin(115200);

  // Wake up the LCD
  lcd.init();
  lcd.backlight();
  lcd.print("System Starting");

  // Wake up the sensors
  dht.begin();
  pinMode(FAN_TACH_PIN, INPUT_PULLUP);
  // Tell the ESP32 to listen for fan pulses on Pin 5
  attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), countPulse, FALLING);
  
  pinMode(FAN_PWM_PIN, OUTPUT);
  analogWrite(FAN_PWM_PIN, 0); // Start with fan off for safety

  // Connect to Blynk Cloud
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Create a timer to repeat the "updateSystem" logic every 5 seconds
  timer.setInterval(5000L, updateSystem);
}

// THIS RUNS REPETITIVELY as fast as possible
void loop() {
  Blynk.run(); // Keeps the connection to your phone alive
  timer.run(); // Runs the fan logic based on timer interval
}