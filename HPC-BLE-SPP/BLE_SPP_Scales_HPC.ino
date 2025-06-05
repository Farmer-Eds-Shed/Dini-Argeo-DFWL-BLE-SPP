#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "BluetoothSerial.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"

#define SCALE_RX 16
#define SCALE_TX 17
#define SCALE_BAUD 9600

#define STABILITY_CONFIRM_COUNT 4
#define RATE_LIMIT_MS 2000

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
BluetoothSerial SerialBT;

volatile bool deviceConnected = false;
bool oldDeviceConnected = false;

unsigned long lastSendTime = 0;
String lastSentWeightStr = "";
String lastObservedWeightStr = "";
int stabilityCounter = 0;
bool isStable = false;

#define SERVICE_UUID        "0000181d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a9d-0000-1000-8000-00805f9b34fb"

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("[BLE] Client disconnected");
  }
};

void sendTruTestWeight(float kg, bool stable) {
  if (kg <= 0.0 || kg > 1500.0) {
    Serial.printf("[WARN] Skipping invalid weight: %.2f kg\n", kg);
    return;
  }

  uint16_t rawWeight = (uint16_t)(kg / 0.05);
  uint8_t payload[11] = {0};

  payload[0] = 0x04;                  // kg flag
  payload[1] = rawWeight & 0xFF;      // LSB
  payload[2] = (rawWeight >> 8);      // MSB
  payload[10] = stable ? 0x00 : 0x80; // 0x00 = stable, 0x80 = unstable

  pCharacteristic->setValue(payload, sizeof(payload));
  pCharacteristic->indicate();

  Serial.printf("[BLE] Sent %s weight: %.2f kg\n", stable ? "stable" : "unstable", kg);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(SCALE_BAUD, SERIAL_8N1, SCALE_RX, SCALE_TX);

  Serial.println("[SYS] Starting HPC BLE/SPP emulator");

  if (!SerialBT.begin("HPC SPP")) {
    Serial.println("[ERROR] Bluetooth SPP failed to start!");
  } else {
    Serial.println("[SPP] Bluetooth SPP started.");
  }

  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
  esp_bt_pin_code_t pin_code;
  memcpy(pin_code, "1234", 4);
  esp_bt_gap_set_pin(pin_type, 4, pin_code);

  BLEDevice::init("HPC BLE");
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

  Serial.println("[BLE] Advertising as 'HPC BLE'");
}

void loop() {
  static String hpcBuffer = "";

  while (Serial2.available()) {
    char c = Serial2.read();
    hpcBuffer += c;

    if (hpcBuffer.length() >= 7) {
      String chunk = hpcBuffer.substring(hpcBuffer.length() - 7);
      if (chunk.startsWith("=")) {
        String weightStr = chunk.substring(1);
        weightStr.trim();
        float weight = weightStr.toFloat();

        // Stability logic
        if (weightStr == lastObservedWeightStr) {
          if (stabilityCounter < STABILITY_CONFIRM_COUNT) stabilityCounter++;
        } else {
          stabilityCounter = 0;
        }
        lastObservedWeightStr = weightStr;
        isStable = (stabilityCounter >= STABILITY_CONFIRM_COUNT);

        unsigned long now = millis();
        if ((weightStr != lastSentWeightStr) || (now - lastSendTime > RATE_LIMIT_MS)) {
          // Send BLE
          if (deviceConnected) {
            sendTruTestWeight(weight, isStable);
          }

          // Send DFWL-style SPP message
          if (SerialBT.hasClient()) {
            String sppMsg = "ST,GS," + weightStr + ",kg";
            SerialBT.println(sppMsg);
            Serial.println("[SPP] Sent: " + sppMsg);
          }

          lastSentWeightStr = weightStr;
          lastSendTime = now;
        }
      }

      // Trim buffer to 7 chars max
      hpcBuffer = hpcBuffer.substring(hpcBuffer.length() - 7);
    }
  }

  // Handle BLE advertising
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("[BLE] Restarted advertising");
    oldDeviceConnected = false;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
  }

  delay(5);
}
