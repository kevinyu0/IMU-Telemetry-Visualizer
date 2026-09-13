#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>

const char* ssid = "TELUSBM9432";
const char* password = "RH7hdMH9NKcJ";
const char* targetIP = "192.168.1.237";
const int udpPort = 8888;
WiFiUDP udp;

#define MPU_ADDRESS 0x68 // I2C address of the MPU6050
#define WHO_AM_I 0x75 // Register address for WHO_AM_I
#define PWR_MGMT_1 0x6B // Register address for power management
#define ACCEL_X_H 0x3B // Register address for accelerometer X-axis high byte
#define GYRO_X_H 0x43 // Register address for gyroscope X-axis high byte

float gyroXError = 0.0;
float gyroYError = 0.0;
float gyroZError = 0.0;

const float ACCEL_SCALE = 16384.0; // Scale factor for accelerometer (assuming ±2g range)
const float GYRO_SCALE = 131.0; // Scale factor for gyroscope (assuming ±250°/s range)

unsigned long previousMillis = 0;
//unsigned long cycle = 0;

float finalRoll = 0.0;
float finalPitch = 0.0;
float yaw = 0.0;

const int ONBOARD_LED = 2;
unsigned long lastToggleTime = 0;
bool ledState = false;

void setup() {
  Serial.begin(115200);

  Serial.print("Connecting to WiFi network: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }

  Serial.println("Connected to WiFi network!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Wire.begin(21, 22, 400000);

  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(WHO_AM_I); // Request the WHO_AM_I register
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 1); // Request 1 byte from the MPU6050
  byte sensorID = Wire.read(); // Read the byte

  Serial.print("Sensor ID: 0x");
  Serial.println(sensorID, HEX); // Print the sensor ID in hexadecimal format

  if (sensorID != 0x70) {
    Serial.println("Error: MPU6050 not detected!");
    while(1);
  }
    
  Serial.println("MPU6050 successfully detected!");
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(PWR_MGMT_1); // Request the power management register
  Wire.write(0x00);
  Wire.endTransmission(true);
  // Wake up the MPU6050

  // Calibrate the gyroscope to find the average error values
  for (int i = 0; i < 500; i++) {
    Wire.beginTransmission(MPU_ADDRESS);
    Wire.write(GYRO_X_H); // Request the gyroscope X-axis high byte
    Wire.endTransmission(false);

    Wire.requestFrom(MPU_ADDRESS, 6); // Request 6 bytes from the MPU6050

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

    delay(5); // Delay for 5 milliseconds between readings
  }

  //Divide the accumulated errors by the number of samples to get the average error
  gyroXError /= 500.0;
  gyroYError /= 500.0;
  gyroZError /= 500.0;

  Serial.println("Gyroscope calibrated successfully!");

  pinMode(ONBOARD_LED, OUTPUT);
}

void loop() {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(ACCEL_X_H); // Request the accelerometer X-axis high byte
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDRESS, 14); // Request 14 bytes from the MPU6050

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
  float dt = (currentMillis - previousMillis) / 1000.0; // Convert to seconds

  float roll = atan2(accelY_g, accelZ_g) * 180.0 / PI;
  float pitch = atan2(-accelX_g, sqrt(accelY_g * accelY_g + accelZ_g * accelZ_g)) * 180.0 / PI;

  if (roll - finalRoll > 180.0) {
    finalRoll += 360.0;
  } else if (finalRoll - roll > 180.0) {
    finalRoll -= 360.0;
  }

  if (fabs(pitch) > 80) {
    finalRoll = (finalRoll + (gyroX_dps * dt));
  } else {
    finalRoll = 0.96 * (finalRoll + (gyroX_dps * dt)) + 0.04 * roll;
  }

  finalPitch = 0.96 * (finalPitch + (gyroY_dps * dt)) + 0.04 * pitch;
  yaw += gyroZ_dps * dt;

  /*
  if (cycle % 50 == 0) { // Print every 50 cycles to reduce serial output
    Serial.print("Accelerometer X-axis: ");
    Serial.println(accelX_g); // Print the accelerometer X-axis value in g
    Serial.print("Accelerometer Y-axis: ");
    Serial.println(accelY_g); // Print the accelerometer Y-axis value 
    Serial.print("Accelerometer Z-axis: ");
    Serial.println(accelZ_g); // Print the accelerometer Z-axis value

    Serial.println();

    Serial.print("Gyroscope X-axis: ");
    Serial.println(gyroX_dps );
    Serial.print("Gyroscope Y-axis: ");
    Serial.println(gyroY_dps);
    Serial.print("Gyroscope Z-axis: ");
    Serial.println(gyroZ_dps);

    Serial.println();

    Serial.print("Roll: ");
    Serial.println(finalRoll);
    Serial.print("Pitch: ");
    Serial.println(finalPitch);
    Serial.print("Yaw: ");
    Serial.println(yaw);

    Serial.println("------------------------------");
  }
  */

  char packetBuffer[50];
  snprintf(packetBuffer, sizeof(packetBuffer), "%.2f, %.2f, %.2f", finalRoll, finalPitch, yaw);
  udp.beginPacket(targetIP, udpPort);
  udp.print(packetBuffer);
  int success = udp.endPacket();

  if (millis() - lastToggleTime >= 500) {
    ledState = !ledState;
    digitalWrite(ONBOARD_LED, ledState);
    lastToggleTime = millis();
}

  previousMillis = currentMillis; // Update the previous time for the next loop iteration
  delay(10);
  //cycle++;
}