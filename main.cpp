#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// Replace the data below with your network credentials
const char* ssid = "SSID";
const char* password = "PASSWORD";
const char* targetIP = "IP_ADDRESS";

const int udpPort = 8888;
WiFiUDP udp;

#define MPU_ADDRESS 0x68
#define WHO_AM_I 0x75
#define PWR_MGMT_1 0x6B
#define ACCEL_X_H 0x3B
#define GYRO_X_H 0x43

float gyroXError = 0.0;
float gyroYError = 0.0;
float gyroZError = 0.0;

const float ACCEL_SCALE = 16384.0;  // Conversion factor for accelerometer data to g's
const float GYRO_SCALE = 131.0; // Conversion factor for gyroscope data to degrees per second (dps)

unsigned long previousMillis = 0;

float finalRoll = 0.0;
float finalPitch = 0.0;
float yaw = 0.0;

const int ONBOARD_LED = 2;
unsigned long lastToggleTime = 0;
bool ledState = false;

void setup() {
  Serial.begin(115200);

  // Connect to WiFi network
  Serial.print("Connecting to WiFi network: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  Serial.print("Connecting...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();

  Serial.println("Connected to WiFi network!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  Wire.begin(21, 22, 400000);

  // Confirms identity of the MPU6050 sensor by reading the WHO_AM_I register
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(WHO_AM_I);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 1);
  byte sensorID = Wire.read();

  Serial.print("Sensor ID: 0x");
  Serial.println(sensorID, HEX);

  if (sensorID != 0x70) {
    Serial.println("Error: MPU6050 not detected!");
    while(1);
  }
  
  // Wakes up the MPU6050 sensor by writing 0 to the power management register
  Serial.println("MPU6050 successfully detected!");
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(PWR_MGMT_1);
  Wire.write(0x00);
  Wire.endTransmission(true);

  // Reads the gyroscope while stationary to find the average error values and calibrate the gyroscope
  Serial.println("Calibrating gyroscope... Please keep the sensor stationary.");
  for (int i = 0; i < 500; i++) {
    Wire.beginTransmission(MPU_ADDRESS);
    Wire.write(GYRO_X_H);
    Wire.endTransmission(false);

    Wire.requestFrom(MPU_ADDRESS, 6); // Requests gyroscope data from the MPU6050

    byte gyroXHigh = Wire.read();
    byte gyroXLow = Wire.read();
    byte gyroYHigh = Wire.read();
    byte gyroYLow = Wire.read();
    byte gyroZHigh = Wire.read();
    byte gyroZLow = Wire.read();
    
    int16_t gyroX = (gyroXHigh << 8) | gyroXLow;
    int16_t gyroY = (gyroYHigh << 8) | gyroYLow; 
    int16_t gyroZ = (gyroZHigh << 8) | gyroZLow;

    gyroXError += (gyroX / GYRO_SCALE);
    gyroYError += (gyroY / GYRO_SCALE);
    gyroZError += (gyroZ / GYRO_SCALE);

    delay(5);
  }

  gyroXError /= 500.0;
  gyroYError /= 500.0;
  gyroZError /= 500.0;

  Serial.println("Gyroscope calibrated successfully!");

  pinMode(ONBOARD_LED, OUTPUT);
}

void loop() {
  // Reads the accelerometer and gyroscope simulatenously
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(ACCEL_X_H);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 14);

  byte accelXHigh = Wire.read();
  byte accelXLow = Wire.read();
  byte accelYHigh = Wire.read();
  byte accelYLow = Wire.read();
  byte accelZHigh = Wire.read();
  byte accelZLow = Wire.read();

  byte tempHigh = Wire.read();
  byte tempLow = Wire.read();

  byte gyroXHigh = Wire.read();
  byte gyroXLow = Wire.read();
  byte gyroYHigh = Wire.read();
  byte gyroYLow = Wire.read();
  byte gyroZHigh = Wire.read();
  byte gyroZLow = Wire.read();

  int16_t accelX = (accelXHigh << 8) | accelXLow;
  int16_t accelY = (accelYHigh << 8) | accelYLow;
  int16_t accelZ = (accelZHigh << 8) | accelZLow;

  int16_t temp = (tempHigh << 8) | tempLow;

  int16_t gyroX = (gyroXHigh << 8) | gyroXLow;
  int16_t gyroY = (gyroYHigh << 8) | gyroYLow;
  int16_t gyroZ = (gyroZHigh << 8) | gyroZLow;

  float accelX_g = accelX / ACCEL_SCALE;
  float accelY_g = accelY / ACCEL_SCALE;
  float accelZ_g = accelZ / ACCEL_SCALE;

  float gyroX_dps = gyroX / GYRO_SCALE - gyroXError;
  float gyroY_dps = gyroY / GYRO_SCALE - gyroYError;
  float gyroZ_dps = gyroZ / GYRO_SCALE - gyroZError;

  unsigned long currentMillis = millis();
  float dt = (currentMillis - previousMillis) / 1000.0; // Calculates dt in seconds

  // Calculates roll and pitch angles from the accelerometer
  float roll = atan2(accelY_g, accelZ_g) * 180.0 / PI;
  float pitch = atan2(-accelX_g, sqrt(accelY_g * accelY_g + accelZ_g * accelZ_g)) * 180.0 / PI;

  finalRoll = finalRoll + (gyroX_dps * dt);

  // Shift the accelerometer data so it sits adjacent to the gyroscope data
  if (finalRoll - roll > 180.0) {
    roll += 360.0;
  } else if (roll - finalRoll > 180.0) {
    roll -= 360.0;
  }

  // Integrates accelerometer data if the pitch angle is not near 90 degrees to avoid gimbal lock
  if (fabs(pitch) <= 80) {
    finalRoll = (0.96 * finalRoll) + (0.04 * roll);
  }

  if (finalRoll > 180.0) {
    finalRoll -= 360.0;
  } else if (finalRoll < -180.0) {
    finalRoll += 360.0;
  }

  finalPitch = 0.96 * (finalPitch + (gyroY_dps * dt)) + 0.04 * pitch;
  yaw += gyroZ_dps * dt;  //Yaw can only be determined from gyroscope data

  // Sends roll, pitch, and yaw data over UDP to the specified target IP address and port
  char packetBuffer[50];
  snprintf(packetBuffer, sizeof(packetBuffer), "%.2f, %.2f, %.2f", finalRoll, finalPitch, yaw);
  udp.beginPacket(targetIP, udpPort);
  udp.print(packetBuffer);
  udp.endPacket();

  // Continuously toggles LED to track that loop is running and hasn't frozen
  if (millis() - lastToggleTime >= 500) {
    ledState = !ledState;
    digitalWrite(ONBOARD_LED, ledState);
    lastToggleTime = millis();
  }

  previousMillis = currentMillis;
  delay(10);
}
