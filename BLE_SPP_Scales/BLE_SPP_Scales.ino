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
#define SCALE_BAUD 19200

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
BluetoothSerial SerialBT;

volatile bool deviceConnected = false;
bool oldDeviceConnected = false;

unsigned long lastSendTime = 0;
String lastSentWeightStr = "";


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

void sendTruTestWeight(float kg) {
  if (kg <= 0.0 || kg > 1500.0) {
    Serial.printf("[WARN] Skipping invalid weight: %.2f kg\n", kg);
    return;
  }

  uint16_t rawWeight = (uint16_t)(kg / 0.05);
  uint8_t payload[11] = {0};

  payload[0] = 0x04;                  // Flag = kg
  payload[1] = rawWeight & 0xFF;      // LSB
  payload[2] = (rawWeight >> 8);      // MSB
  payload[10] = 0x00;                 // Stable weight indicator

  pCharacteristic->setValue(payload, sizeof(payload));
  pCharacteristic->indicate();

  Serial.printf("[BLE] Indicated weight: %.2f kg [Payload: %02X %02X %02X ... %02X]\n",
                kg, payload[0], payload[1], payload[2], payload[10]);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(SCALE_BAUD, SERIAL_8N1, SCALE_RX, SCALE_TX);

  Serial.println("[SYS] Starting Tru-Test BLE/SPP emulator");

  if (!SerialBT.begin("ONeill SPP")) {
    Serial.println("[ERROR] Bluetooth SPP failed to start!");
  } else {
    Serial.println("[SPP] Bluetooth SPP started.");
  }

  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
  esp_bt_pin_code_t pin_code;
  memcpy(pin_code, "1234", 4);
  esp_bt_gap_set_pin(pin_type, 4, pin_code);

  BLEDevice::init("ONeill BLE");
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

  Serial.println("[BLE] Advertising as 'ONeill BLE'");
}

void loop() {
  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    Serial.println("[UART] Received: " + line);

    if (line.startsWith("ST,GS,")) {
      int idx1 = line.indexOf(',');
      int idx2 = line.indexOf(',', idx1 + 1);
      int idx3 = line.indexOf(',', idx2 + 1);

      if (idx3 > idx2) {
        String weightStr = line.substring(idx2 + 1, idx3);
        weightStr.trim();

        Serial.printf("[PARSE] Weight string: '%s'\n", weightStr.c_str());

        float weight = weightStr.toFloat();

        unsigned long now = millis();
        if (deviceConnected) {
          if (weightStr != lastSentWeightStr || (now - lastSendTime > 10000)) {
            Serial.println("[BLE] Sending via indicate...");
            sendTruTestWeight(weight);
            lastSentWeightStr = weightStr;
            lastSendTime = now;
          } else {
            Serial.println("[BLE] Skipped due to rate limit");
          }
        }


        if (SerialBT.hasClient()) {
          Serial.println("[SPP] Sending via SPP...");
          SerialBT.println(line);
        } else {
          Serial.println("[SPP] No client connected.");
        }
      } else {
        Serial.println("[ERROR] Could not parse weight. Not enough commas.");
      }
    }
  }

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("[BLE] Restarted advertising");
    oldDeviceConnected = false;
  }

  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
  }

  delay(10);
}
