#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SD.h>
#include <SPI.h>

#define WIFI_SSID " "
#define WIFI_PASSWORD " "


#define DATABASE_URL " "
#define DATABASE_SECRET " "

// Pins
#define SOIL_MOISTURE_PIN 35
#define RAIN_SENSOR_PIN 13
#define PIR_SENSOR_PIN 32
#define DHT_PIN 14
#define MOTOR_IN1 26
#define MOTOR_IN2 27
#define SD_CS_PIN 5

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// DHT
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

// Variables
bool motorStatus = false;
bool rainDetected = false;
bool pirDetected = false;
bool firebaseMotorControl = true;  
bool firebasePaused = false;
bool pauseStatusPrev = false; 
String plantName = "Default";
int minMoistureThreshold = 50;
int maxMoistureThreshold = 80;

unsigned long lastUploadTime = 0;
int hh = 0, mm = 0, ss = 1;
File logFile;


void motorStart() {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  motorStatus = true;
  Serial.println("MOTOR STARTED");
}

void motorStop() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  motorStatus = false;
  Serial.println("MOTOR STOPPED");
}

float calculatePredictionHours(int soil, float temp, float hum) {
  int moistureToDrop = soil - minMoistureThreshold;
  if (moistureToDrop <= 0) return 0;
  float baseDryRate = 2.0; 
  float tempFactor = (temp - 25) * 0.1;
  float humFactor = (50 - hum) * 0.05;
  float dryRate = baseDryRate + tempFactor + humFactor;
  if (dryRate <= 0) dryRate = 0.5;
  
  float hours = moistureToDrop / dryRate;
  int h = (int)hours;
  int m = (int)((hours - h) * 60);
  return h + (m / 100.0); 
}


void uploadFirebase(int soil, float temp, float hum, float predictionHours) {
  Firebase.RTDB.setInt(&fbdo, "/soil_moisture", soil);
  Firebase.RTDB.setFloat(&fbdo, "/temperature", temp);
  Firebase.RTDB.setFloat(&fbdo, "/humidity", hum);
  Firebase.RTDB.setString(&fbdo, "/motor_status", motorStatus ? "On" : "Off");
  Firebase.RTDB.setString(&fbdo, "/rain_status", rainDetected ? "Raining" : "Not_Raining");
  Firebase.RTDB.setString(&fbdo, "/pir_status", pirDetected ? "Found" : "Not_Found");

  String predText = (predictionHours <= 0) ? "Irrigation Now" : String(predictionHours, 2) + " hrs";
  Firebase.RTDB.setString(&fbdo, "/next_irrigation", predText);
}


void updatePlantSettings() {
  // Plant name
  if (Firebase.RTDB.getString(&fbdo, "/plant_name")) {
    plantName = fbdo.stringData();
  }

  // Thresholds
  if (Firebase.RTDB.getInt(&fbdo, "/min_threshold")) {
    minMoistureThreshold = fbdo.intData();
  }
  if (Firebase.RTDB.getInt(&fbdo, "/max_threshold")) {
    maxMoistureThreshold = fbdo.intData();
  }
}


void updateOLED(int soil, float temp, float hum, float predictionHours) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Plant: "); display.println(plantName);
  display.print("Soil: "); display.print(soil); display.println("%");
  display.print("Temp: "); display.print(temp); display.println("C");
  display.print("Hum: "); display.print(hum); display.println("%");
  display.print("Motor: "); display.println(motorStatus ? "On" : "Off");
  display.print("Rain: "); display.println(rainDetected ? "Yes" : "No");
  display.print("PIR: "); display.println(pirDetected ? "Found" : "None");
  display.print("Next Irrigation: ");
  display.println((predictionHours <= 0) ? "Now" : String(predictionHours, 2) + " hrs");
  display.display();
}


void updateTime() {
  ss++;
  if (ss == 60) { ss = 0; mm++; }
  if (mm == 60) { mm = 0; hh++; }
}


void logToSD(int soil, float temp, float hum, float predictionHours) {
  String timestamp = (hh < 10 ? "0" : "") + String(hh) + ":" + (mm < 10 ? "0" : "") + String(mm) + ":" + (ss < 10 ? "0" : "") + String(ss);
  logFile = SD.open("/log.csv", FILE_APPEND);
  if (logFile) {
    logFile.printf("%s,%d,%.2f,%.2f,%s,%s,%s,%.2f\n",
                   timestamp.c_str(), soil, temp, hum,
                   motorStatus ? "On" : "Off",
                   rainDetected ? "Yes" : "No",
                   pirDetected ? "Found" : "None",
                   predictionHours);
    logFile.close();
  }
}


void setup() {
  Serial.begin(115200);

  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(RAIN_SENSOR_PIN, INPUT);
  pinMode(PIR_SENSOR_PIN, INPUT);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  motorStop();

  dht.begin();

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) while (true);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Smart Irrigation");
  display.println("System Booting...");
  display.display();
  delay(1500);

  // WiFi
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    display.print(".");
    display.display();
  }
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi: Connected");
  display.display();
  delay(1000);

  // Firebase
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting Firebase...");
  display.display();
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (Firebase.ready()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Firebase: OK");
    display.display();
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Firebase: ERROR");
    display.display();
  }
  delay(1000);

  // SD Card
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Initializing SD...");
  display.display();
  if (SD.begin(SD_CS_PIN)) {
    logFile = SD.open("/log.csv", FILE_WRITE);
    if (logFile) {
      logFile.println("Time,Soil(%),Temp(C),Humidity(%),Motor,Rain,PIR,NextIrrigationHours");
      logFile.close();
    }
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SD Card: OK");
    display.display();
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SD Card: ERROR");
    display.display();
  }
  delay(1500);

  
  updatePlantSettings();

  // Ready screen
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("System Ready");
  display.display();
  delay(1000);
}


void loop() {
  
  if (Firebase.RTDB.getBool(&fbdo, "/pause_realtime")) firebasePaused = fbdo.boolData();

  
  static bool pauseStatusPrev = false;
  if (firebasePaused != pauseStatusPrev) {
    if (firebasePaused) {
      motorStop(); 
      Serial.println("SYSTEM PAUSED");
    } else {
      Serial.println("SYSTEM RESUMED");
    }
    pauseStatusPrev = firebasePaused;
  }

  if (firebasePaused) { delay(500); return; }

  
  updatePlantSettings();

  
  int soilValue = analogRead(SOIL_MOISTURE_PIN);
  int rainValue = digitalRead(RAIN_SENSOR_PIN);
  int pirValue = digitalRead(PIR_SENSOR_PIN);

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  if (isnan(temperature) || isnan(humidity)) { delay(2000); return; }

  int soilMoisturePercent = map(soilValue, 4095, 0, 0, 100);
  rainDetected = (rainValue == LOW);
  pirDetected = (pirValue == HIGH);

  
  if (Firebase.RTDB.getBool(&fbdo, "/motor_manual")) 
    firebaseMotorControl = !fbdo.boolData();

  float predictionHours = calculatePredictionHours(soilMoisturePercent, temperature, humidity);

  
  if (firebaseMotorControl) { 
    
    if (rainDetected) motorStop();
    else {
      if (soilMoisturePercent < minMoistureThreshold && !motorStatus) motorStart();
      else if (soilMoisturePercent >= maxMoistureThreshold && motorStatus) motorStop();
    }
  } else {
    
    if (Firebase.RTDB.getString(&fbdo, "/motor_status")) {
      String motorCmd = fbdo.stringData();
      if (motorCmd == "On" && !motorStatus) motorStart();
      else if (motorCmd == "Off" && motorStatus) motorStop();
    }
  }

  
  uploadFirebase(soilMoisturePercent, temperature, humidity, predictionHours);
  updateOLED(soilMoisturePercent, temperature, humidity, predictionHours);
  logToSD(soilMoisturePercent, temperature, humidity, predictionHours);

  updateTime();
  delay(2000);
}
