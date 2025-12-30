# 💧 Drinking Water Suitability Detection System (ESP32)

An ESP32-based IoT system designed to **measure, evaluate, and monitor drinking water quality** in real time using multiple sensors and cloud integration.
The system determines whether water is **suitable for drinking** based on standard thresholds and displays the result visually and audibly.

---

## 🚀 Features

* **Real-time water quality monitoring**
* **Automatic drinking water suitability evaluation**
* **IoT integration with ThingsBoard**
* **TFT display user interface**
* **Physical reset button & buzzer alert**
* **Low battery detection**
* **Remote reset via RPC (ThingsBoard)**

---

## 🔬 Measured Parameters

| Parameter   | Sensor / Method                          |
| ----------- | ---------------------------------------- |
| Temperature | DS18B20                                  |
| pH          | pH sensor via ADS1115 (16-bit ADC)       |
| Turbidity   | Turbidity sensor via ADS1115             |
| TDS         | TDS sensor with temperature compensation |
| Battery     | Voltage divider + ESP32 ADC              |

---

## 📊 Water Suitability Criteria (WHO Reference)

Water is considered **SUITABLE** if:

* **pH:** 6.5 – 8.5
* **Turbidity:** < 5 NTU
* **TDS:** ≤ 1000 ppm

If any parameter exceeds the limits, the water is classified as **NOT SUITABLE**.

---

## 🖥️ User Interface (TFT Display)

* **Live sensor readings** (Temperature, pH, Turbidity, TDS)
* **Stability status** before evaluation
* **Final result display**:

  * 🟢 **LAYAK** (Suitable)
  * 🔴 **TIDAK LAYAK** (Not Suitable)
* **Low battery warning**
* Visual border color based on result (Green / Red)

---

## 🔊 Buzzer Notification

* **1 long beep** → Water is suitable
* **3 short beeps** → Water is not suitable

---

## ☁️ IoT & ThingsBoard Integration

* Real-time telemetry upload every **1 second**
* MQTT-based communication
* Telemetry data includes:

  * Temperature
  * pH
  * Turbidity
  * TDS
  * Battery voltage
  * Suitability status
* **Remote reset** supported via ThingsBoard RPC

---

## 🔁 System Workflow

1. System initialization
2. Sensor readings collected continuously
3. Data stability checking
4. Water quality evaluation
5. Result displayed on TFT
6. Buzzer alert triggered
7. System waits for reset (physical button or RPC)

---

## 🔘 Reset Methods

* **Physical reset button**
* **Remote reset via ThingsBoard RPC**

RPC method:

```json
{
  "method": "reset",
  "params": {}
}
```

---

## 🔋 Power & Battery Monitoring

* Battery voltage monitored via voltage divider
* Low battery indicator LED
* On-screen low battery warning

---

## 🧰 Hardware Requirements

* ESP32
* ADS1115 (16-bit ADC)
* DS18B20 Temperature Sensor
* pH Sensor
* Turbidity Sensor
* TDS Sensor
* TFT Display (TFT_eSPI compatible)
* Buzzer
* Push Button
* Battery + Voltage Divider

---

## 📁 Code Structure (Main Branch)

* `main.ino` → Main system logic
* Sensor initialization & reading
* State machine for operation flow
* IoT communication handler
* Display & alert management

---

## ⚙️ Configuration

Update the following variables before uploading:

```cpp
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

const char* mqtt_server = "THINGSBOARD_SERVER";
const char* token = "DEVICE_ACCESS_TOKEN";
```

---

## 🧪 Calibration Notes

* pH calibration supported using slope, intercept, and pH 7 reference voltage
* TDS includes temperature compensation
* Calibration constants should be adjusted based on actual sensor calibration

---

## 📌 Version Information

* **Current branch:** `main`
* **Stable release:** Version 4.x
* Designed for academic projects, research, and prototype deployment

---

## 📜 License

This project is intended for **educational and research purposes**.
You are free to modify and extend it with proper attribution.
