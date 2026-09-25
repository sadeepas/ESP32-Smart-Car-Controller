# ESP32-Smart-Car-Controller
Advanced WiFi-controlled ESP32 robot car with web UI, ultrasonic obstacle avoidance, autopilot, movement patterns, LED effects, battery monitoring, and OTA firmware updates.


![ESP32](https://img.shields.io/badge/Platform-ESP32-blue)
![Arduino](https://img.shields.io/badge/Framework-Arduino-00979D)
![License](https://img.shields.io/badge/License-MIT-green)
![OTA](https://img.shields.io/badge/OTA-Supported-orange)

---

## Overview


<img width="2160" height="3240" alt="20260915_223222-COLLAGE" src="https://github.com/user-attachments/assets/ec930fac-1dca-43b8-92c9-15c07bb78626" />

This project turns an ESP32 into a fully-featured smart car controller. It creates its own WiFi Access Point (and can also connect to your home WiFi), serves a modern mobile-friendly web interface, and supports:

- Manual driving with hold-to-move controls
- Ultrasonic obstacle avoidance (active only while moving forward)
- Autopilot mode with intelligent obstacle handling
- Predefined movement patterns (Square, ZigZag, Circles, Spin, Back-and-Forth)
- Individual + pattern-based LED control (4 LEDs)
- Direction inversion (F/B and L/R)
- Independent left/right motor speed control
- Real-time battery percentage, distance, and WiFi signal strength
- Over-the-Air (OTA) firmware updates via the web UI

---

## Features

| Feature                    | Description                                                                 |
|---------------------------|-----------------------------------------------------------------------------|
| **Web UI**                | Beautiful dark-themed, mobile-optimized interface with 4 tabs               |
| **Manual Drive**          | Touch-friendly D-pad with hold-to-move + dead-man timeout                   |
| **Ultrasonic Safety**     | Obstacle detection only while moving forward (configurable distance)        |
| **Autopilot**             | Automatic obstacle avoidance with reverse + random turn                     |
| **Movement Patterns**     | Square, ZigZag, Circle Left/Right, Spin Left/Right, Back-Forth              |
| **LED System**            | 4 individual LEDs + patterns (Blink, Alternate, Chase, Strobe/Police)       |
| **Direction Invert**      | Flip Forward/Backward or Left/Right from the web UI                         |
| **Speed Control**         | Independent left & right motor speed sliders (80–255)                       |
| **Battery Monitor**       | Real-time battery percentage (calibrated for 2S Li-ion)                     |
| **WiFi Dual Mode**        | SoftAP + Station mode (connects to home WiFi when available)                |
| **OTA Updates**           | Upload `.bin` firmware wirelessly from the browser                          |
| **Safety Timeout**        | Motors automatically stop if no command is received for 1.2 seconds         |

---

## Hardware Requirements

### Core Components
- ESP32 development board (any common module)
- Dual H-bridge motor driver (e.g. L298N, DRV8833, or TB6612)
- 2 × DC geared motors + wheels
- Chassis + caster wheel (or 4-wheel drive setup)
- HC-SR04 Ultrasonic sensor
- 4 × LEDs (or LED modules) for front/back lights
- Battery (recommended: 2S Li-ion / 7.4 V)
- Voltage divider for battery monitoring (2 × equal resistors)
- Optional: Level shifter if using 5 V ultrasonic with 3.3 V ESP32

### Pinout

| Function          | GPIO | Notes                                      |
|-------------------|------|--------------------------------------------|
| Motor Left  IN1   | 2    | PWM capable                                |
| Motor Left  IN2   | 3    | PWM capable                                |
| Motor Right IN3   | 6    | PWM capable                                |
| Motor Right IN4   | 7    | PWM capable                                |
| Front LED 1       | 0    |                                            |
| Front LED 2       | 1    |                                            |
| Back LED 1        | 4    |                                            |
| Back LED 2        | 5    |                                            |
| Ultrasonic TRIG   | 9    |                                            |
| Ultrasonic ECHO   | 10   |                                            |
| Battery ADC       | 8    | Through voltage divider (see below)        |

> **Battery Voltage Divider**  
> Connect battery positive → Resistor R1 → GPIO 8 → Resistor R2 → GND  
> Use two equal resistors (e.g. 10 kΩ + 10 kΩ) so the maximum voltage at GPIO stays under 3.3 V.

---

## Software Setup

### 1. Install Arduino IDE / PlatformIO
- Arduino IDE 2.x or PlatformIO
- Install **ESP32 board support** (Espressif Systems)

### 2. Required Libraries
All used libraries are included with the ESP32 Arduino core:
- `WiFi`
- `WebServer`
- `Update`
- `esp_wifi`

### 3. Configure WiFi Credentials
Open the sketch and change these lines:

```cpp
const char* ssid     = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const char* ap_ssid  = "ESP32-Car";       // Access Point name
const char* ap_pass  = "12345678";        // Access Point password (min 8 chars)
```

### 4. Upload
1. Select your ESP32 board and correct COM port.
2. Upload the sketch.
3. Open Serial Monitor at **115200** baud to see IP addresses.

---

## How to Use

### Connecting
1. Power on the car.
2. Connect your phone/laptop to the WiFi network **`ESP32-Car`** (password: `12345678`).
3. Open a browser and go to:
   - `http://192.168.4.1` (Access Point mode)
   - or the Station IP shown in Serial Monitor (if connected to home WiFi)

### Web Interface Tabs

| Tab       | Purpose                                      |
|-----------|----------------------------------------------|
| **Drive** | Manual control + Ultrasonic + Autopilot      |
| **Motors**| Direction invert, speed sliders, patterns    |
| **LEDs**  | Individual LED control + lighting patterns   |
| **OTA**   | Wireless firmware update                     |

### Controls Summary
- Hold the directional buttons to move continuously.
- Release to stop (or press **STOP**).
- Toggle **Ultrasonic** on/off.
- Toggle **Autopilot** for autonomous driving.
- Adjust safe distance (15–50 cm).
- Invert F/B or L/R if your motor wiring is reversed.
- Start any movement pattern and stop it anytime.
- Control each of the 4 LEDs individually or run lighting effects.

---

## Autopilot Behavior

When Autopilot is enabled:
1. Car drives forward.
2. If an obstacle is detected closer than the safe distance:
   - Stops immediately
   - Reverses briefly
   - Turns left or right randomly
   - Continues forward
3. When approaching an obstacle slowly, speed is automatically reduced.

Ultrasonic protection is **only active while moving forward**. Backward movement is never blocked by the sensor.

---

## Movement Patterns

| Pattern ID | Name          | Description                          |
|------------|---------------|--------------------------------------|
| 0          | Square        | Drive in a square path               |
| 1          | ZigZag        | Alternating left/right turns         |
| 2          | Circle Left   | Continuous left-turning circles      |
| 3          | Circle Right  | Continuous right-turning circles     |
| 4          | Spin Left     | Spin in place left                   |
| 5          | Spin Right    | Spin in place right                  |
| 6          | Back-Forth    | Forward and reverse repeatedly       |

Patterns automatically stop and reverse if an obstacle is detected while moving forward.

---

## LED Patterns

| ID | Name              | Effect                              |
|----|-------------------|-------------------------------------|
| 0  | Static (Off)      | Manual LED control only             |
| 1  | Full Lights       | All 4 LEDs permanently on           |
| 2  | All Blink         | All LEDs blink together             |
| 3  | Alternate F/B     | Front and back alternate            |
| 4  | Chase (Circle)    | Sequential chase around the car     |
| 5  | Strobe / Police   | Fast alternating strobe effect      |

---

## OTA Firmware Update

1. Go to the **OTA** tab.
2. Select a compiled `.bin` file (from Arduino IDE: Sketch → Export compiled Binary).
3. Click **Upload & Flash**.
4. Wait for the progress bar to finish.
5. The ESP32 will automatically restart with the new firmware.

---

## Safety Features

- **Dead-man switch**: Motors stop automatically after 1.2 s without a command.
- **Forward-only ultrasonic protection**: Prevents collisions only when moving forward.
- **Autopilot / Pattern interruption**: Both modes respect the ultrasonic sensor.
- **Immediate stop** on obstacle detection during patterns or autopilot.

---

## Customization Tips

- **Motor direction wrong?** Use the Invert F/B or Invert L/R buttons in the Motors tab.
- **Different motor driver?** Adjust the `motorRaw*` functions.
- **Different battery?** Recalibrate the `getBatteryPercent()` function (currently set for 6.0 V – 8.4 V range).
- **More patterns?** Add new `MoveStep` arrays and extend the `/pattern` handler.
- **Change AP name/password** in the top configuration section.

---

## Project Structure

```
ESP32-Smart-Car/
├── ESP32_Smart_Car.ino     # Main sketch (this file)
└── README.md               # This file
```

---

## License

This project is released under the **MIT License**.  
Feel free to use, modify, and distribute.


**Happy Driving!** 🚗💨
