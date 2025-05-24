// Tru-Test BLE Emulator: GATT Weight + SCP over Nordic UART
// Supports HerdApp (Weight Service) and Agrident (NUS SCP)

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SCALE_RX 16
#define SCALE_TX 17
#define SCALE_BAUD 19200

BLEServer* pServer = nullptr;
BLECharacteristic* pWeightChar = nullptr;
BLECharacteristic* pNusTxChar = nullptr;
BLECharacteristic* pNusRxChar = nullptr;

volatile bool deviceConnected = false;
bool oldDeviceConnected = false;
float lastWeight = 0.0;
bool weightIsStable = true;

#define SERVICE_WEIGHT_UUID     "0000181d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_WEIGHT_UUID "00002a9d-0000-1000-8000-00805f9b34fb"

#define SERVICE_NUS_UUID        "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID             "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID             "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✅ BLE client connected");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("❌ BLE client disconnected");
  }
};

class NUSCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    char buffer[64] = {0};
    size_t len = pChar->getLength();
    memcpy(buffer, pChar->getData(), len > 63 ? 63 : len);
    String cmd = String(buffer);
    cmd.trim();
    Serial.printf("📥 NUS RX: %s\n", cmd.c_str());

    if (cmd == "{RW}") {
      Serial.printf("🧪 SCP read lastWeight: %.2f | Stable: %s\\n", lastWeight, weightIsStable ? "YES" : "NO");
      char msg[16];
      if (lastWeight > 0 && weightIsStable)
        snprintf(msg, sizeof(msg), "[%0.1f]", lastWeight);
      else
        snprintf(msg, sizeof(msg), "[U%0.1f]", lastWeight);
      pNusTxChar->setValue((uint8_t*)msg, strlen(msg));
      pNusTxChar->notify();
      Serial.print("📤 NUS TX: ");
      Serial.println(msg);
    }
  }
};

void sendWeightIndication(float kg) {
  if (kg <= 0.0 || kg > 1500.0) {
    Serial.println("⚠️ Skipping invalid weight indication");
    return;
  }

  uint16_t raw = (uint16_t)(kg / 0.05);
  uint8_t payload[11] = {0};
  payload[0] = 0x04; // Flags: kg
  payload[1] = raw & 0xFF;
  payload[2] = (raw >> 8) & 0xFF;
  payload[10] = 0x00; // Stable

  pWeightChar->setValue(payload, sizeof(payload));
  pWeightChar->indicate();

  Serial.print("📤 Indicated payload: ");
  for (int i = 0; i < 11; i++) Serial.printf("%02X ", payload[i]);
  Serial.printf(" | %.2f kg\\n", kg);
}


void setup() {
  delay(500); // allow BLE stack to stabilize
  Serial.begin(115200);
  Serial2.begin(SCALE_BAUD, SERIAL_8N1, SCALE_RX, SCALE_TX);

  BLEDevice::init("S3 200006");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
    // Generic Access Service (0x1800)
  BLEService *gapService = pServer->createService(BLEUUID((uint16_t)0x1800));
  BLECharacteristic *deviceNameChar = gapService->createCharacteristic(
      BLEUUID((uint16_t)0x2A00), BLECharacteristic::PROPERTY_READ);
  deviceNameChar->setValue("S3 200001");

  BLECharacteristic *appearanceChar = gapService->createCharacteristic(
      BLEUUID((uint16_t)0x2A01), BLECharacteristic::PROPERTY_READ);
  uint8_t appearanceValue[2] = {0x00, 0x00};
  appearanceChar->setValue(appearanceValue, 2);

  BLECharacteristic *connParamsChar = gapService->createCharacteristic(
      BLEUUID((uint16_t)0x2A04), BLECharacteristic::PROPERTY_READ);
  uint8_t connParams[8] = {
    0x06, 0x00, // Min interval (7.5ms)
    0x20, 0x00, // Max interval (40ms)
    0x00, 0x00, // Latency
    0xC8, 0x00  // Timeout (2000ms)
  };
  connParamsChar->setValue(connParams, 8);

  BLECharacteristic *carChar = gapService->createCharacteristic(
      BLEUUID((uint16_t)0x2AA6), BLECharacteristic::PROPERTY_READ);
  uint8_t car = 0x01;
  carChar->setValue(&car, 1);

  gapService->start();

// Generic Attribute Service (0x1801)
  BLEService *gattService = pServer->createService(BLEUUID((uint16_t)0x1801));
  gattService->start();

  // Weight Service (HerdApp)
  BLEService* pWeightService = pServer->createService(SERVICE_WEIGHT_UUID);
  pWeightChar = pWeightService->createCharacteristic(
    CHARACTERISTIC_WEIGHT_UUID, BLECharacteristic::PROPERTY_INDICATE);
  BLE2902* p2902 = new BLE2902();
  p2902->setIndications(true);
  p2902->setNotifications(true);
  pWeightChar->addDescriptor(p2902);
  pWeightService->start();

  // Nordic UART Service (Agrident)
  BLEService* pNusService = pServer->createService(SERVICE_NUS_UUID);
  pNusRxChar = pNusService->createCharacteristic(NUS_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  pNusRxChar->setCallbacks(new NUSCallbacks());
  pNusTxChar = pNusService->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pNusTxChar->addDescriptor(new BLE2902());
  pNusService->start();

  // Advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(SERVICE_WEIGHT_UUID);
  pAdvertising->addServiceUUID(SERVICE_NUS_UUID);
  pAdvertising->setMinInterval(0x20);
  pAdvertising->setMaxInterval(0x40);
  pAdvertising->addServiceUUID("180F");
  pAdvertising->start();

  // Battery Service (0x180F)
  BLEService *batteryService = pServer->createService(BLEUUID((uint16_t)0x180F));
  BLECharacteristic *batteryLevel = batteryService->createCharacteristic(
    BLEUUID((uint16_t)0x2A19),
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  uint8_t batteryPercent = 100;
  batteryLevel->setValue(&batteryPercent, 1);
  batteryService->start();


  Serial.println("📡 Advertising Tru-Test GATT + NUS");
}

void loop() {
  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    Serial.println("📥 Serial2: " + line);

    if (line.startsWith("ST,GS,")) {
      
      int idx1 = line.indexOf(',');
      int idx2 = line.indexOf(',', idx1 + 1);
      int idx3 = line.indexOf(',', idx2 + 1);
      if (idx3 > idx2) {
        String weightStr = line.substring(idx2 + 1, idx3);
        weightStr.trim();
        Serial.printf("🔬 Extracted weightStr: '%s'\n", weightStr.c_str());
        float kg = weightStr.toFloat();
        Serial.printf("🔍 Parsed weight: %.2f kg\n", kg);
        if (kg > 0.0 && kg < 1500.0) {
          lastWeight = kg;
          weightIsStable = true;
          Serial.printf("✅ lastWeight updated: %.2f kg\n", lastWeight);
          if (deviceConnected) Serial.println("📣 Sending indication...");
          sendWeightIndication(kg);
        }
      } else {
        Serial.printf("❌ Could not parse weight. Not enough commas. idx1=%d, idx2=%d, idx3=%d\n", idx1, idx2, idx3);
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

