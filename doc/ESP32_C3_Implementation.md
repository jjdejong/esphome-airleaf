# ESP32-C3 Super Mini Implementation for BLDC Motor Control

This document describes the ESP32-C3 implementations using ESPHome's native ESP-NOW component, and explains cross-compatibility with ESP8266 devices.

## Overview

The ESP32-C3 implementations provide an alternative to the ESP8266-based system, using ESPHome's native ESP-NOW component instead of custom C++ headers. This offers cleaner YAML-based configuration while maintaining full compatibility with ESP8266 devices.

## ESP8266 ↔ ESP32 Cross-Compatibility

### Protocol Compatibility

**YES - ESP8266 and ESP32 can communicate seamlessly via ESP-NOW**

According to Espressif's official documentation:
- ESP-NOW supports ESP8266, ESP32, ESP32-S, and ESP32-C series
- The protocol is identical across all platforms
- Maximum packet size: 250 bytes (compatible across all devices)
- Communication is bidirectional and works in mixed networks

### Practical Implementation

You can mix ESP8266 and ESP32 boards in the same ESP-NOW network:
- **ESP8266 Airleaf** → **ESP32-C3 Motor Controller** ✅
- **ESP32-C3 Airleaf** → **ESP8266 Motor Controller** ✅
- **ESP32-C3 Airleaf** → **ESP32-C3 Motor Controller** ✅
- **ESP8266 Airleaf** → **ESP8266 Motor Controller** ✅

The data structure (`struct { float speed_setpoint; }`) is transmitted as raw bytes and is platform-independent.

## File Overview

### ESP32-C3 Motor Controller

**File**: `slave_motor_control_esp32c3.yaml`

Uses native ESPHome `espnow` component with `on_receive` trigger.

**Key Features**:
- No custom C++ headers required
- Pure YAML configuration
- Native binary data parsing
- Compatible with ESP8266 senders

**Hardware Configuration**:
- Board: ESP32-C3-DevKitM-1 (or ESP32-C3 Super Mini)
- PWM Output: GPIO4 (adjustable)
- Tachometer Input: GPIO5 (adjustable)

**ESP-NOW Receive Handler**:
```yaml
espnow:
  on_receive:
    then:
      - lambda: |-
          // Parse float from 4-byte binary data
          if (size >= 4) {
            float target_rpm = *((float*)(data));

            // Process RPM setpoint...
            // (speed mapping, motor control logic)
          }
```

### ESP32-C3 Airleaf Controller

**File**: `Airleaf_esp32c3.yaml`

Uses native ESPHome `espnow.send` action with lambda data preparation.

**Key Features**:
- Native ESP-NOW send action
- MAC address configurable in Home Assistant
- Global variables for data passing
- Compatible with ESP8266 receivers

**ESP-NOW Send Implementation**:
```yaml
script:
  - id: send_rpm_script
    then:
      - espnow.send:
          address: !lambda |-
            static uint8_t mac[6];
            memcpy(mac, id(espnow_target_mac), 6);
            return mac;
          data: !lambda |-
            float rpm = id(espnow_rpm_data);
            // Convert float to 4 bytes (little-endian)
            return {(uint8_t)(*(uint32_t*)&rpm),
                    (uint8_t)((*(uint32_t*)&rpm) >> 8),
                    (uint8_t)((*(uint32_t*)&rpm) >> 16),
                    (uint8_t)((*(uint32_t*)&rpm) >> 24)};
```

## Pin Assignments for ESP32-C3 Super Mini

The ESP32-C3 Super Mini is a compact development board. Adjust pins as needed:

### Motor Controller (Receiver)
| Function | Recommended Pin | Notes |
|----------|----------------|-------|
| PWM Output (F-PWM) | GPIO4 | LEDC channel |
| Tachometer Input (F-FG) | GPIO5 | Pulse counter with pullup |
| Power | 5V/GND | Via USB-C or pins |

### Airleaf Controller (Sender)
| Function | Pin | Notes |
|----------|-----|-------|
| Modbus TX | GPIO21 (default UART) | Adjust based on actual Airleaf wiring |
| Modbus RX | GPIO20 (default UART) | Adjust based on actual Airleaf wiring |

**Note**: The example `Airleaf_esp32c3.yaml` uses a template sensor for demonstration. Replace with actual Modbus configuration from your Airleaf setup.

## Advantages of ESP32-C3 Implementation

### vs. ESP8266 Implementation

| Feature | ESP8266 | ESP32-C3 |
|---------|---------|----------|
| ESP-NOW Support | Custom C++ headers | Native ESPHome component |
| Configuration Complexity | Medium (C++ + YAML) | Low (YAML only) |
| Code Maintenance | Custom code updates | ESPHome handles updates |
| PWM Channels | Limited | 6x LEDC channels |
| Processing Power | 80 MHz | 160 MHz RISC-V |
| Memory | 80KB RAM | 400KB RAM |
| USB | External UART adapter | Native USB-C |
| Cost | ~$3-4 | ~$2-3 (Super Mini) |

### When to Use ESP32-C3

✅ **Use ESP32-C3 when:**
- You prefer pure YAML configuration
- You want native USB programming
- You need more processing power
- You want more PWM channels
- You're starting a new project

✅ **Keep ESP8266 when:**
- You already have ESP8266 hardware
- Cost is absolutely critical
- Your current system works fine

## Configuration Differences

### 1. Platform Declaration

**ESP8266**:
```yaml
esp8266:
  board: d1_mini
```

**ESP32-C3**:
```yaml
esp32:
  board: esp32-c3-devkitm-1
  variant: ESP32C3
  framework:
    type: arduino
```

### 2. PWM Output

**ESP8266**:
```yaml
output:
  - platform: esp8266_pwm
    pin: D5
    frequency: 1000 Hz
```

**ESP32-C3**:
```yaml
output:
  - platform: ledc
    pin: GPIO4
    frequency: 1000 Hz
```

### 3. ESP-NOW Implementation

**ESP8266**: Custom C++ headers (`espnow_sender.h`, `espnow_receiver.h`)

**ESP32-C3**: Native YAML component with `espnow:` block

## Data Structure Compatibility

Both implementations use the same binary data structure:

```cpp
typedef struct struct_message {
  float speed_setpoint;  // 4 bytes, IEEE 754 float
} struct_message;
```

**Binary Representation**:
- Size: 4 bytes
- Format: Little-endian IEEE 754 single-precision float
- Range: Supports RPM values 0-3000+
- Precision: ~7 decimal digits

**Parsing on ESP32-C3**:
```cpp
float rpm = *((float*)(data));  // Direct cast from byte pointer
```

**Parsing on ESP8266**:
```cpp
memcpy(&incomingData, data, sizeof(incomingData));
float rpm = incomingData.speed_setpoint;
```

Both methods produce identical results across platforms.

## Setup Procedure

### Option 1: Full ESP32-C3 System

1. Flash `slave_motor_control_esp32c3.yaml` to motor controller ESP32-C3
2. Record MAC address from logs
3. Flash `Airleaf_esp32c3.yaml` to Airleaf ESP32-C3
4. Configure MAC address in Home Assistant
5. Enable slave motor controller in Home Assistant

### Option 2: Mixed ESP8266/ESP32-C3 System

**ESP8266 Airleaf + ESP32-C3 Motor Controller:**
1. Flash `slave_motor_control_esp32c3.yaml` to ESP32-C3
2. Record MAC address
3. Flash `Airleaf.yaml` (ESP8266 version) to Airleaf
4. Configure MAC address in Home Assistant (format: AA:BB:CC:DD:EE:FF)
5. Enable slave motor controller

**ESP32-C3 Airleaf + ESP8266 Motor Controller:**
1. Flash `slave_motor_control.yaml` (ESP8266 version) to Wemos D1 Mini
2. Record MAC address
3. Flash `Airleaf_esp32c3.yaml` to ESP32-C3
4. Configure MAC address in Home Assistant
5. Enable slave motor controller

## Troubleshooting

### ESP-NOW Not Working

**Check WiFi Channel**:
- Both devices must be on same WiFi channel
- ESP8266: Uses `wifi_set_channel(1)` in C++ code
- ESP32-C3: ESPHome automatically handles channel sync when WiFi is active

**Verify MAC Address**:
- Use logs to confirm MAC addresses
- Format must be exactly 17 characters: `AA:BB:CC:DD:EE:FF`
- All colons required

**Check Enable Switch**:
- "Slave Motor Controller Enabled" must be ON
- Restart Airleaf device after enabling

### Data Not Received

**Check Packet Size**:
- Should be exactly 4 bytes for float
- ESP-NOW maximum is 250 bytes

**Verify Data Format**:
- Float must be IEEE 754 format
- Little-endian byte order

**Monitor Logs**:
- ESP8266: Uses `ESP_LOGD`, `ESP_LOGW`, `ESP_LOGE`
- ESP32-C3: Same logging functions available

## Performance Comparison

| Metric | ESP8266 | ESP32-C3 |
|--------|---------|----------|
| ESP-NOW Latency | <10ms | <10ms |
| Update Rate | >100 Hz | >100 Hz |
| Power Consumption (Active) | ~80mA | ~60mA |
| Power Consumption (Light Sleep) | ~15mA | ~3mA |
| WiFi Range | ~50m | ~100m (better antenna) |

**Note**: ESP-NOW latency is identical - the protocol is the same. Power consumption varies by board design.

## References

### Official Documentation
- [Espressif ESP-NOW Solutions](https://www.espressif.com/en/solutions/low-power-solutions/esp-now)
- [ESP-NOW User Guide (PDF)](https://www.espressif.com/sites/default/files/documentation/esp-now_user_guide_en.pdf)
- [ESPHome ESP-NOW Component](https://esphome.io/components/espnow/)

### Tutorials
- [Getting Started with ESP-NOW on ESP8266](https://randomnerdtutorials.com/esp-now-esp8266-nodemcu-arduino-ide/)
- [ESP-NOW on ESP32 with Arduino IDE](https://randomnerdtutorials.com/esp-now-esp32-arduino-ide/)
- [ESP-NOW Auto-Pairing ESP32/ESP8266](https://randomnerdtutorials.com/esp-now-auto-pairing-esp32-esp8266/)
- [ESP-NOW Mix ESP32 and ESP8266](https://www.programmingelectronics.com/espnow-esp32-and-esp8266/)
- [TinkerIOT ESP-NOW Guide](https://tinkeriot.com/esp-now-esp32-esp8266-guide/)

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-12-14 | Initial ESP32-C3 implementation documentation |

---

*Prepared for jjdejong/esphome-airleaf repository*
