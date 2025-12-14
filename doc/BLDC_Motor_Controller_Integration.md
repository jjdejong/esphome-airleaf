# BLDC Motor Controller Integration with ESP-NOW

## Overview

This document describes the integration of a 5-wire 310V variable frequency brushless motor control board with ESPHome using two Wemos D1 Mini devices communicating via ESP-NOW protocol.

## System Architecture

### Hardware Components

1. **Motor Control Board**: 5-wire 310V DC variable frequency BLDC motor driver
   - AC Input: 220V 
   - DC Bus Output: 310V (rectified)
   - Control Interface: 4-pin connector with isolated 12V supply
   - Motor Interface: 5-pin connector (+310V, HOT_GND, +15V, FG, VSP)

2. **Wemos Device #1 (Motor Controller)**: Receives speed setpoints via ESP-NOW and controls the motor
   
3. **Wemos Device #2 (Airleaf Controller)**: Sends speed setpoints and receives RPM feedback

### Signal Isolation

The motor control board includes three optocouplers providing galvanic isolation:
- **Optocoupler 1**: Isolates F-PWM input signal (low-voltage control → high-voltage motor drive)
- **Optocoupler 2**: Isolates F-FG tachometer output (high-voltage motor → low-voltage feedback)
- **Optocoupler 3**: Additional motor control/protection functions

This isolation allows safe direct connection of 3.3V Wemos logic to the control interface.

## Control Interface Pinout

**4-pin Control Connector (2.54mm spacing):**

| Pin | Signal | Wire Color | Function | Connection |
|-----|--------|------------|----------|------------|
| 1 | GND | Red | Ground | Wemos GND |
| 2 | +12V | Black | 12V 1.25A isolated supply | Buck converter input |
| 3 | F-PWM | White | PWM/Analog speed control input | Wemos GPIO (PWM output) |
| 4 | F-FG | Yellow | Tachometer pulse output | Wemos GPIO (pulse counter input) |

**Note**: The unusual wire color convention (red for GND, black for +12V) must be verified before connection.

## F-PWM Input Characteristics

The F-PWM pin accepts either:
1. **Analog voltage**: 0-6V DC (potentiometer mode shown in board documentation)
2. **PWM signal**: Digital pulses switched by optocoupler

### Optocoupler Operation

The control board uses an optocoupler for galvanic isolation:
- **Input side**: ESP8266 3.3V PWM signal activates the optocoupler LED
- **Output side**: Isolated transistor switches the board's internal control voltage
- **Key advantage**: PWM duty cycle is preserved regardless of input voltage level

When using PWM from ESP8266 (3.3V logic):
- 0% duty cycle → Optocoupler always OFF → Motor stopped
- 50% duty cycle → Optocoupler switches 50% of time → ~50% motor speed
- 100% duty cycle → Optocoupler always ON → ~100% motor speed

**Important**: The 3.3V ESP8266 output is sufficient to fully activate the optocoupler. There is **no inherent speed limitation** from using 3.3V logic levels. The optocoupler provides both isolation and voltage level adaptation, allowing full motor speed range control.

## ESP-NOW Communication Protocol

### Data Structure

```cpp
typedef struct struct_message {
  float speed_setpoint;  // RPM value for both directions
} struct_message;
```

### Communication Flow

1. **Airleaf → Motor Controller**: Target RPM setpoint from Airleaf's "Fan speed" sensor (register 015)
2. **Motor Controller → Home Assistant**: Actual motor RPM sent directly via WiFi/API
3. Update interval: Updates when RPM changes >5 RPM for setpoint transmission

### Speed Control Architecture

The system uses **one-way RPM communication** with **adjustable open-loop mapping**:

- **Airleaf controller** reads its internal fan speed (RPM) and sends this value via ESP-NOW
- **Motor controller** receives target RPM and converts it to PWM using an adjustable multiplier
- **Motor controller** reports actual RPM directly to Home Assistant via WiFi/API
- **Adjustable mapping** allows manual calibration based on actual motor performance
- **Future ready** for closed-loop PID control using target vs. actual RPM feedback

---

## Wemos #1: Motor Controller Configuration

### File: `slave_motor_control.yaml`

```yaml
esphome:
  name: motor-controller
  includes:
    - espnow_receiver.h

esp8266:
  board: d1_mini

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:

ota:
  - platform: esphome
    password: !secret ota_password

logger:
  level: DEBUG

# PWM output for motor speed control
output:
  - platform: esp8266_pwm
    pin: D5  # GPIO14 - white wire to F-PWM
    frequency: 1000 Hz  # 1kHz PWM, adjust if needed
    id: motor_pwm_output

# Fan platform for motor control
fan:
  - platform: speed
    output: motor_pwm_output
    name: "BLDC Motor"
    id: bldc_motor
    restore_mode: RESTORE_DEFAULT_OFF

# Tachometer pulse counter
sensor:
  - platform: pulse_counter
    pin:
      number: D6  # GPIO12 - yellow wire to F-FG
      mode:
        input: true
        pullup: true
    name: "Motor RPM"
    id: motor_rpm
    unit_of_measurement: 'RPM'
    accuracy_decimals: 0
    update_interval: 2s
    count_mode:
      rising_edge: INCREMENT
      falling_edge: DISABLE
    filters:
      # Calibration: adjust multiplier based on motor's pulses per revolution
      # Formula: 60 / (pulses_per_revolution * update_interval_seconds)
      # Example: For 2 pulses/rev with 2s update: 60 / (2 * 2) = 15
      - multiply: 30.0
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1

  # Target RPM setpoint from Airleaf controller
  - platform: template
    name: "Target RPM Setpoint"
    id: target_rpm_setpoint_sensor
    unit_of_measurement: 'RPM'
    accuracy_decimals: 0
    icon: "mdi:speedometer-slow"
    lambda: "return id(target_rpm_setpoint);"
    update_interval: 1s

  # Calculated PWM percentage (for monitoring)
  - platform: template
    name: "Motor PWM Percentage"
    id: motor_pwm_percent
    unit_of_measurement: '%'
    accuracy_decimals: 1
    icon: "mdi:percent"
    state_class: measurement

number:
  # RPM to PWM percentage mapping multiplier
  - platform: template
    name: "Speed Mapping Multiplier"
    id: speed_mapping_multiplier
    min_value: 0.01
    max_value: 0.10
    step: 0.001
    initial_value: 0.05
    mode: box
    optimistic: true
    icon: "mdi:tune"
    unit_of_measurement: "%/RPM"
    entity_category: config
    restore_value: true

  # Maximum RPM reference for percentage calculation
  - platform: template
    name: "Max RPM Reference"
    id: max_rpm_reference
    min_value: 500
    max_value: 3000
    step: 50
    initial_value: 2000
    mode: box
    optimistic: true
    icon: "mdi:speedometer"
    unit_of_measurement: "RPM"
    entity_category: config
    restore_value: true

# Global variables
globals:
  - id: target_rpm_setpoint
    type: int
    restore_value: no
    initial_value: '0'
  - id: sender_mac
    type: uint8_t[6]
    restore_value: no

# Custom component for ESP-NOW receiver
custom_component:
  - lambda: |-
      auto espnow_comp = new ESPNowComponent();
      return {espnow_comp};
```

### Adjustable Speed Mapping

The motor controller includes two configurable parameters for fine-tuning the RPM-to-PWM conversion:

#### Speed Mapping Multiplier (Active Method)
- **Formula**: `PWM% = Target_RPM × Multiplier`
- **Default**: 0.05 %/RPM (gives 100% PWM at 2000 RPM)
- **Range**: 0.01 to 0.10 %/RPM
- **Adjustment**: Available in Home Assistant, persists across reboots

**Calibration Example**:
1. Airleaf commands 1000 RPM target
2. Motor actually runs at 800 RPM (underspeed)
3. Increase multiplier from 0.05 to 0.0625
4. New PWM: 1000 × 0.0625 = 62.5% (was 50%)
5. Motor should now reach closer to 1000 RPM

#### Max RPM Reference (Alternative Method)
- **Formula**: `PWM% = (Target_RPM / Max_RPM) × 100`
- **Default**: 2000 RPM
- **Range**: 500 to 3000 RPM
- **Status**: Available but not currently active

To switch to this percentage-based method, modify `espnow_receiver.h` lines 35-40 to use the commented alternative formula.

#### Motor PWM Percentage Sensor
- Displays the currently calculated PWM duty cycle
- Updates in real-time as RPM setpoint changes
- Useful for monitoring and diagnostic purposes

### File: `espnow_receiver.h`

Create this file in the same directory as `slave_motor_control.yaml`:

```cpp
#include "esphome.h"

extern "C" {
  #include <espnow.h>
  #include <user_interface.h>
}

// Data structure for ESP-NOW communication
typedef struct struct_message {
  float speed_setpoint;  // RPM value for both directions
} struct_message;

struct_message incomingData;
struct_message outgoingData;

// Callback when data is received from Airleaf controller
void OnDataRecv(uint8_t *mac_addr, uint8_t *data, uint8_t data_len) {
  memcpy(&incomingData, data, sizeof(incomingData));

  ESP_LOGD("espnow", "Received from %02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  float target_rpm = incomingData.speed_setpoint;
  ESP_LOGD("espnow", "Target RPM setpoint: %.0f", target_rpm);

  // Store sender MAC for sending RPM feedback
  memcpy(id(sender_mac), mac_addr, 6);

  // Update target RPM sensor
  id(target_rpm_setpoint) = (int)target_rpm;

  // Adjustable RPM to PWM percentage mapping
  // Method 1: Use multiplier (default: 0.05 %/RPM)
  // speed_percent = target_rpm * multiplier
  float multiplier = id(speed_mapping_multiplier).state;
  float speed_percent = target_rpm * multiplier;

  // Alternative method: Use max RPM reference (commented out)
  // float max_rpm = id(max_rpm_reference).state;
  // float speed_percent = (target_rpm / max_rpm) * 100.0;

  // Clamp to 0-100%
  if (speed_percent < 0) speed_percent = 0;
  if (speed_percent > 100) speed_percent = 100;

  // Update PWM percentage sensor for monitoring
  id(motor_pwm_percent).publish_state(speed_percent);

  auto call = id(bldc_motor).make_call();
  if (target_rpm > 0) {
    call.set_state(true);
    call.set_speed((int)speed_percent);
  } else {
    call.set_state(false);
  }
  call.perform();
}

class ESPNowComponent : public Component {
 public:
  void setup() override {
    // Set WiFi channel (must match sender)
    wifi_set_channel(1);
    
    if (esp_now_init() != 0) {
      ESP_LOGE("espnow", "Error initializing ESP-NOW");
      return;
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(OnDataRecv);
    
    // Print MAC address for configuration
    uint8_t mac[6];
    wifi_get_macaddr(STATION_IF, mac);
    ESP_LOGI("espnow", "Motor Controller MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI("espnow", "ESP-NOW receiver initialized successfully");
  }
  
  void loop() override {
    // Send RPM feedback to Airleaf controller every second
    static unsigned long lastSendTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastSendTime >= 1000) {
      lastSendTime = currentTime;
      
      // Prepare RPM data
      outgoingData.speed_setpoint = id(motor_rpm).state;
      
      // Send to stored sender MAC address
      if (id(sender_mac)[0] != 0 || id(sender_mac)[1] != 0 || 
          id(sender_mac)[2] != 0 || id(sender_mac)[3] != 0 || 
          id(sender_mac)[4] != 0 || id(sender_mac)[5] != 0) {
        esp_now_send(id(sender_mac), (uint8_t*)&outgoingData, sizeof(outgoingData));
      }
    }
  }
};
```

---

## Wemos #2: Airleaf Controller Integration

### Integration into `Airleaf.yaml`

The Airleaf controller reads the internal fan speed (Modbus register 015: "Fan speed") and sends the RPM value directly to the motor controller via ESP-NOW.

**Important**: The motor controller integration is **optional**. The Airleaf device will function normally even if the motor controller is disabled or not configured. ESP-NOW will only be initialized when explicitly enabled and configured.

#### 1. Add ESP-NOW include file

```yaml
esphome:
  name: $devicename
  includes:
    - espnow_sender.h
```

#### 2. Add motor controller enable switch

```yaml
switch:
  # ... existing switches ...

  # Enable/disable motor controller ESP-NOW communication
  - platform: template
    name: "Motor Controller Enabled"
    id: motor_controller_enabled
    icon: "mdi:motor"
    entity_category: config
    restore_mode: RESTORE_DEFAULT_OFF
    optimistic: true
```

#### 3. Add MAC address configuration (adjustable in Home Assistant)

```yaml
text:
  - platform: template
    name: "Slave Motor Controller MAC Address"
    id: motor_controller_mac
    entity_category: config
    mode: text
    optimistic: true
    initial_value: "FF:FF:FF:FF:FF:FF"
    restore_value: true
    min_length: 17
    max_length: 17
```

#### 4. Add custom component for ESP-NOW sender

```yaml
# Custom component for ESP-NOW sender
custom_component:
  - lambda: |-
      auto espnow_sender = new ESPNowSender();
      return {espnow_sender};
```

### File: `espnow_sender.h`

Create this file in your Airleaf ESPHome directory:

```cpp
#include "esphome.h"

extern "C" {
  #include <espnow.h>
  #include <user_interface.h>
}

// Data structure for ESP-NOW communication
typedef struct struct_message {
  float speed_setpoint;
} struct_message;

struct_message outgoingData;

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    ESP_LOGD("espnow", "Motor setpoint delivered successfully");
  } else {
    ESP_LOGW("espnow", "Motor setpoint delivery failed");
  }
}

// Helper function to convert hex string to byte
uint8_t hexToByte(const std::string& hex) {
  return (uint8_t)strtol(hex.c_str(), nullptr, 16);
}

// Parse MAC address from "AA:BB:CC:DD:EE:FF" format
bool parseMacAddress(const std::string& mac_str, uint8_t* mac_array) {
  if (mac_str.length() != 17) {
    return false;
  }

  for (int i = 0; i < 6; i++) {
    int pos = i * 3;
    if (i > 0 && mac_str[pos - 1] != ':') {
      return false;
    }
    std::string byte_hex = mac_str.substr(pos, 2);
    mac_array[i] = hexToByte(byte_hex);
  }

  return true;
}

class ESPNowSender : public Component {
 private:
  bool espnow_initialized = false;
  bool peer_added = false;
  uint8_t motorControllerMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

 public:
  void setup() override {
    // Only initialize if slave motor controller is enabled
    if (!id(motor_controller_enabled).state) {
      ESP_LOGI("espnow", "Slave motor controller disabled - ESP-NOW not initialized");
      return;
    }

    // Parse MAC address from single text field
    std::string mac_str = id(motor_controller_mac).state;
    if (!parseMacAddress(mac_str, motorControllerMac)) {
      ESP_LOGE("espnow", "Invalid MAC address format: %s (expected AA:BB:CC:DD:EE:FF)", mac_str.c_str());
      return;
    }

    // Check if MAC is all FF (unconfigured)
    bool mac_configured = false;
    for (int i = 0; i < 6; i++) {
      if (motorControllerMac[i] != 0xFF) {
        mac_configured = true;
        break;
      }
    }

    if (!mac_configured) {
      ESP_LOGW("espnow", "Slave motor controller MAC not configured (all FF) - ESP-NOW not initialized");
      return;
    }

    // Set WiFi channel (must match receiver)
    wifi_set_channel(1);

    if (esp_now_init() != 0) {
      ESP_LOGE("espnow", "Error initializing ESP-NOW");
      return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
    esp_now_register_send_cb(OnDataSent);
    espnow_initialized = true;

    // Add motor controller as peer
    int result = esp_now_add_peer(motorControllerMac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    if (result == 0) {
      peer_added = true;
      ESP_LOGI("espnow", "Motor controller peer added: %02X:%02X:%02X:%02X:%02X:%02X",
               motorControllerMac[0], motorControllerMac[1], motorControllerMac[2],
               motorControllerMac[3], motorControllerMac[4], motorControllerMac[5]);
    } else {
      ESP_LOGE("espnow", "Failed to add motor controller peer (error %d)", result);
    }

    // Print this device's MAC address
    uint8_t mac[6];
    wifi_get_macaddr(STATION_IF, mac);
    ESP_LOGI("espnow", "Airleaf MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI("espnow", "ESP-NOW sender initialized successfully");
  }

  void loop() override {
    // Only send if motor controller is enabled and ESP-NOW is initialized
    if (!id(motor_controller_enabled).state || !espnow_initialized || !peer_added) {
      return;
    }

    // Send motor speed setpoint from "Fan speed" sensor as RPM
    static float lastRPM = -1;
    float currentRPM = id(fan_speed).state;

    // Send if RPM changed by more than 5 RPM to reduce traffic
    if (abs(currentRPM - lastRPM) > 5.0) {
      lastRPM = currentRPM;

      outgoingData.speed_setpoint = currentRPM;
      int result = esp_now_send(motorControllerMac, (uint8_t*)&outgoingData, sizeof(outgoingData));

      if (result != 0) {
        ESP_LOGW("espnow", "Failed to send motor speed setpoint (error %d)", result);
      } else {
        ESP_LOGD("espnow", "Sent motor speed setpoint: %.0f RPM", currentRPM);
      }
    }
  }
};
```

---

## Physical Wiring

### Motor Controller Board Connections

#### Control Interface (4-pin connector)
| Board Pin | Wire Color | Connection |
|-----------|------------|------------|
| GND | Red | Wemos GND |
| +12V | Black | 12V → 5V buck converter → Wemos 5V |
| F-PWM | White | Wemos D5 (GPIO14) |
| F-FG | Yellow | Wemos D6 (GPIO12) |

#### Motor Interface (5-pin connector)
Connect to your 5-wire BLDC motor according to motor manufacturer's pinout:
- +310V
- HOT_GND
- +15V
- FG (motor tachometer)
- VSP (motor speed control)

**WARNING**: Different motor manufacturers may use different pin arrangements even with the same connector. Verify motor pinout before connection.

#### AC Power Input
- 220V AC Line (L)
- 220V AC Neutral (N)

### Power Supply Options

**Option 1: Use board's isolated 12V supply**
- Board +12V (black wire) → 12V-to-5V buck converter → Wemos 5V pin
- Recommended buck converter: LM2596 or similar with 1A+ capacity
- Provides galvanic isolation from mains voltage

**Option 2: Separate isolated 5V supply**
- Use independent 5V power supply for Wemos
- Still connect board GND to Wemos GND for signal reference
- Preferred for enhanced safety isolation

---

## Setup and Configuration Procedure

### Step 1: Flash Motor Controller

1. Flash `motor-controller.yaml` to first Wemos device
2. Connect to WiFi and view logs
3. **Record the MAC address** printed in logs (format: `AA:BB:CC:DD:EE:FF`)

### Step 2: Configure Motor Controller in Home Assistant

1. Flash Airleaf configuration (motor controller integration starts disabled by default)
2. In Home Assistant, navigate to the Airleaf device
3. Enable the slave motor controller:
   - Toggle "Slave Motor Controller Enabled" switch to **ON**
4. Configure the MAC address from Step 1:
   - Enter the complete MAC address in the format `AA:BB:CC:DD:EE:FF`
   - Example: For MAC `A1:B2:C3:D4:E5:F6`, enter: `A1:B2:C3:D4:E5:F6`
   - Field name: "Slave Motor Controller MAC Address"
5. Restart the Airleaf device to apply changes

**Note**: The Airleaf device functions normally even when the motor controller is disabled or not configured.

### Step 3: Physical Wiring

1. **POWER OFF** all equipment
2. Connect motor to board's 5-pin connector (verify pinout)
3. Connect Wemos #1 to board's 4-pin control connector:
   - Verify wire colors (red=GND, black=+12V)
   - Double-check connections before applying power
4. Connect 220V AC input to board
5. Install buck converter if using board's 12V supply

### Step 4: Testing and Calibration

1. Power on system
2. Monitor logs on both devices
3. Verify ESP-NOW communication:
   - Send speed setpoint from Airleaf
   - Confirm motor controller receives setpoint
   - Check RPM feedback returns to Airleaf
4. **Calibrate tachometer multiplier**:
   - Measure actual motor RPM with external tachometer
   - Compare with reported RPM
   - Adjust `multiply` filter in `motor_rpm` sensor
   - Typical values: 15-60 (depends on motor pole count and hall sensor configuration)

### Step 5: Validate Operation

- [ ] Motor starts/stops on command
- [ ] Speed control responds proportionally
- [ ] RPM feedback updates regularly
- [ ] No unusual noise or vibration
- [ ] Temperature remains within normal range
- [ ] ESP-NOW delivery success rate >95%

---

## Troubleshooting

### Motor doesn't respond to speed changes

**Check:**
- F-PWM wire connection (white wire to D5/GPIO14)
- PWM signal generation (verify in logs)
- Motor power supply (220V AC connected)
- Motor connector pinout matches motor manufacturer's specification

### No RPM feedback

**Check:**
- F-FG wire connection (yellow wire to D6/GPIO12)
- Motor is actually running
- Tachometer multiplier calibration
- Pull-up resistor enabled on input pin

### ESP-NOW communication fails

**Check:**
- MAC address correctly configured in `espnow_sender.h`
- Both devices on same WiFi channel (channel 1)
- Devices within range (~100m line-of-sight)
- WiFi not disabled on either device
- Check logs for "delivery failed" messages

### Motor runs but RPM reading is incorrect

**Action:**
- Calibrate tachometer multiplier
- Verify motor's pulses-per-revolution specification
- Check for noise on F-FG signal line
- Adjust sliding window average filter

### Wemos resets or crashes

**Check:**
- Power supply capacity (minimum 500mA for Wemos)
- Buck converter output voltage (should be 5.0V ±0.25V)
- Ground connection integrity
- Decoupling capacitors on power supply

---

## Advanced Configuration Options

### PWM Frequency Optimization

Different motors may respond better to different PWM frequencies. Experiment with values between 500Hz and 25kHz:

```yaml
output:
  - platform: esp8266_pwm
    pin: D5
    frequency: 5000 Hz  # Try different values
    id: motor_pwm_output
```

### Voltage Level Shifting for Full Speed Range

To achieve full motor speed range (0-100% instead of 0-55%), add a voltage level shifter:

**Simple transistor-based level shifter:**
```
Wemos D5 (3.3V PWM) → 10kΩ → NPN transistor base
                                ↓ collector (pulled to 6V via 10kΩ)
                                ↓ emitter → GND
                                
Collector output → F-PWM (0-6V PWM)
```

Components needed:
- NPN transistor: 2N2222 or similar
- Two 10kΩ resistors
- 6V regulator (LM7806) powered from board's 12V supply

### Enhanced Monitoring

Add additional sensors for comprehensive monitoring:

```yaml
sensor:
  # Motor power estimation
  - platform: template
    name: "Motor Power Estimate"
    unit_of_measurement: "W"
    lambda: |-
      float rpm = id(motor_rpm).state;
      float speed_pct = id(motor_speed_setpoint).state;
      // Simplified power estimation - adjust coefficients for your motor
      return (rpm / 1000.0) * (speed_pct / 100.0) * 50.0;
    update_interval: 5s

  # Communication quality
  - platform: template
    name: "ESP-NOW Signal Quality"
    unit_of_measurement: "%"
    # Implement based on delivery success/failure rate
```

---

## Safety Considerations

### Electrical Safety

1. **Mains Voltage Hazard**: The motor control board operates with 220V AC input and 310V DC bus. Ensure proper insulation and enclosure.

2. **Galvanic Isolation**: While the optocouplers provide signal isolation, always maintain physical separation between low-voltage control circuits and high-voltage power circuits.

3. **Grounding**: Properly ground the motor chassis and control board mounting points.

4. **Fusing**: Install appropriate fuses on AC input (recommended: 2A slow-blow for typical BLDC fan motors).

### Operational Safety

1. **Motor Stall Protection**: Implement current sensing or thermal protection if motor may experience stall conditions.

2. **Emergency Stop**: Consider adding an emergency stop button that cuts AC power.

3. **Startup Sequence**: Implement gradual speed ramp-up to reduce mechanical stress:
   ```yaml
   script:
     - id: ramp_motor_speed
       parameters:
         target_speed: float
       then:
         - while:
             condition:
               lambda: 'return id(motor_speed_setpoint).state < target_speed;'
             then:
               - number.set:
                   id: motor_speed_setpoint
                   value: !lambda 'return id(motor_speed_setpoint).state + 5;'
               - delay: 200ms
   ```

4. **Thermal Monitoring**: Monitor motor temperature if continuous high-speed operation is expected.

---

## Performance Characteristics

### ESP-NOW Communication

- **Latency**: Typically 5-15ms
- **Range**: Up to 100m line-of-sight, 20-50m through walls
- **Reliability**: >95% delivery success in normal conditions
- **Data rate**: Sufficient for 100+ updates per second (though 1Hz is adequate for motor control)

### Motor Control

- **Speed Resolution**: 1% (0-100 steps via PWM duty cycle)
- **Actual Speed Range**: 0-55% of motor maximum (due to 3.3V PWM output)
- **Response Time**: 100-500ms depending on motor inertia
- **RPM Feedback Accuracy**: ±5 RPM (after calibration)

### Power Consumption

- **Wemos D1 Mini**: ~80mA @ 5V (WiFi active), ~15mA (deep sleep mode)
- **Motor Control Board**: ~20mA (quiescent), variable under load
- **12V Supply Output**: 1.25A maximum capacity

---

## Repository Integration Notes

### For jjdejong/esphome-airleaf Repository

1. **File Organization**:
   ```
   esphome-airleaf/
   ├── Airleaf.yaml              (modified with ESP-NOW integration)
   ├── espnow_sender.h           (new file)
   ├── motor-controller/
   │   ├── motor-controller.yaml (new file)
   │   └── espnow_receiver.h     (new file)
   └── docs/
       └── BLDC_Motor_Integration.md (this document)
   ```

2. **Version Control**: 
   - Tag motor controller MAC address configuration as requiring user modification
   - Include example MAC address format in comments
   - Document calibration values as user-specific

3. **Dependencies**:
   - ESPHome 2023.x or later
   - ESP8266 Arduino Core with ESP-NOW support
   - No external libraries required beyond standard ESP8266 SDK

4. **Testing Checklist**:
   - [ ] ESP-NOW pairing successful
   - [ ] Speed setpoint transmission verified
   - [ ] RPM feedback reception verified
   - [ ] Motor responds to full speed range (0-100%)
   - [ ] Communication maintained during WiFi activity
   - [ ] OTA updates work on both devices
   - [ ] Logs show no errors or warnings

---

## Future Enhancements

### Potential Improvements

1. **Closed-loop Speed Control**:
   - Implement PID controller using RPM feedback
   - Maintain target RPM despite load variations
   - Auto-calibrate for different motor characteristics

2. **Predictive Maintenance**:
   - Monitor vibration patterns via RPM stability
   - Track cumulative running hours
   - Alert on abnormal operation patterns

3. **Multi-Motor Control**:
   - Extend ESP-NOW protocol for multiple motor controllers
   - Implement master/slave configuration
   - Synchronize multiple motors

4. **Enhanced Diagnostics**:
   - Motor health scoring
   - Performance trending
   - Automatic calibration routines

5. **Power Monitoring**:
   - Add current sensor to measure actual motor power
   - Calculate energy consumption
   - Optimize efficiency

---

## References and Resources

### Hardware Documentation
- Motor Control Board: Chinese BLDC controller with isolated control interface
- Component datasheets available upon request

### ESPHome Documentation
- ESP-NOW: https://esphome.io/components/esp32_ble.html
- PWM Output: https://esphome.io/components/output/esp8266_pwm.html
- Pulse Counter: https://esphome.io/components/sensor/pulse_counter.html

### ESP-NOW Protocol
- ESP8266 ESP-NOW documentation: Espressif Systems
- Maximum payload: 250 bytes
- Maximum paired devices: 20

---

## Support and Contributions

For issues, questions, or contributions related to this integration:
- Repository: https://github.com/jjdejong/esphome-airleaf
- Create issues for bugs or feature requests
- Submit pull requests for improvements

---

## License

This integration documentation follows the same license as the esphome-airleaf repository.

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2024-12-14 | Initial documentation |

---

*Document prepared for integration with jjdejong/esphome-airleaf repository*
