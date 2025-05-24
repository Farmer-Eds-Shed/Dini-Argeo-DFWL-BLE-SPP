#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SCALE_RX 16
#define SCALE_TX 17
#define SCALE_BAUD 19200

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;

volatile bool deviceConnected = false;
bool oldDeviceConnected = false;

#define SERVICE_UUID        "0000181d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a9d-0000-1000-8000-00805f9b34fb"

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✅ Client connected");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("❌ Client disconnected");
  }
};

void sendTruTestWeight(float kg) {
  if (kg <= 0.0 || kg > 1500.0) {
    Serial.printf("⚠️ Skipping invalid weight: %.2f kg\n", kg);
    return;
  }

  uint16_t rawWeight = (uint16_t)(kg / 0.05);
  uint8_t payload[11] = {0};

  payload[0] = 0x04;                  // Flag = kg
  payload[1] = rawWeight & 0xFF;      // LSB
  payload[2] = (rawWeight >> 8);      // MSB
  payload[10] = 0x00;                 // Stable weight indicator

  pCharacteristic->setValue(payload, sizeof(payload));
  pCharacteristic->indicate();  // Use Indicate instead of Notify

  Serial.printf("📤 Indicated weight: %.2f kg [Payload: %02X %02X %02X ... %02X]\n",
                kg, payload[0], payload[1], payload[2], payload[10]);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(SCALE_BAUD, SERIAL_8N1, SCALE_RX, SCALE_TX);

  Serial.println("🚀 Starting Tru-Test BLE emulator (Indicate-only)");

  BLEDevice::init("S3 123456");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_INDICATE
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  Serial.println("📡 Advertising as 'S3 123456'");
}

void loop() {
  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    Serial.println("📥 Received: " + line);

    if (line.startsWith("ST,GS,")) {
      // Robustly extract the weight value
      int idx1 = line.indexOf(',');              // after ST
      int idx2 = line.indexOf(',', idx1 + 1);    // after GS
      int idx3 = line.indexOf(',', idx2 + 1);    // after weight

      if (idx3 > idx2) {
        String weightStr = line.substring(idx2 + 1, idx3);
        weightStr.trim();

        Serial.printf("🔬 Parsed weight string: '%s'\n", weightStr.c_str());

        float weight = weightStr.toFloat();
        Serial.println("🔁 Sending via indicate...");
        sendTruTestWeight(weight);
      } else {
        Serial.println("❌ Could not parse weight. Not enough commas.");
      }
    }
  }

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("📡 Restarted advertising");
    oldDeviceConnected = false;
  }

  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
  }

  delay(10);
}
