# Dini-Argeo-DFWL-BLE-SPP
[![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Bluetooth: Dual Mode](https://img.shields.io/badge/bluetooth-BLE%20%2B%20SPP-lightgrey)](https://www.bluetooth.com/)

This project emulates a TruTest S3 livestock weighing scale using an ESP32. It supports both:
- **BLE (Bluetooth Low Energy)** For BLE Apps ( HerdApp)
- **Classic Bluetooth SPP** to send weight data to an Agrident wand

The ESP32 reads serial data from a connected scale (e.g., Dini Argeo) and broadcasts the weight to both protocols.

## 🧰 Features

- ✅ BLE Indicate (UUID: `181D/2A9D`) for HerdApp compatibility  
- ✅ Classic Bluetooth Serial (SPP) for Agrident wands  
- ✅ Fixed 4-digit PIN (`1234`) for pairing  
- ✅ Dual-mode support: BLE and SPP can connect simultaneously  
- ✅ Auto-formats and sends weight data like `21.0 kg`    

## 🔌 Hardware

- ESP32 (WROOM or similar with classic BT + BLE)
- Scale with serial output (TTL/RS232)

## 📦 Wiring

| ESP32 Pin | Scale      |
|-----------|------------|
| GPIO 16   | TX (scale) |
| GPIO 17   | RX (scale) |
| GND       | GND        |
| VCC       | AUX VDC    |

## 📡 Bluetooth Pairing

- Hard coded **PIN:** `1234` for SPP
- HerdApp connects over BLE
- Agrident connects via SPP (ASCII serial)

## 🧪 Example Serial Input

The ESP32 expects data like:
`ST,GS, 21.0,kg`
It extracts `21.0` and sends:
- BLE payload matching Tru-Test S3 spec
- SPP: Passthrough from TTL serial port

## 🛠️ To Build

1. Use the Arduino IDE
2. Install required libraries:
   - `BluetoothSerial`
   - `BLEDevice` (comes with ESP32 board support)
3. Flash to ESP32

## 📋 License

GPLv3 — free to modify and share. Contributions welcome!

## 🐄 Developed by Farmer Ed's Shed

Part of a broader open-source farmtech mission.  
[More agri hacks →](https://github.com/Farmer-Eds-Shed)
