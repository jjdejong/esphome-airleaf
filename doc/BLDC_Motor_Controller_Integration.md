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

### Motor and Control Connectors

**Motor Connector (5-pin):**
- **Type**: KF2EDGR-3.96-5P (3.96mm pitch pluggable terminal block)
- **Alternative names**: KF396, DG396 connector
- **Pitch**: 3.96mm center-to-center spacing
- **Current rating**: Typically 8-10A per pin
- **Voltage rating**: 300V
- **Wire gauge**: Accepts 22-16 AWG wire

**Control Connector (4-pin):**
- **Type**: XH2.54 (2.54mm pitch pluggable terminal block)
- **Pitch**: 2.54mm (0.1" standard spacing)
- **Current rating**: Typically 3A per pin
- **Voltage rating**: 250V
- **Wire gauge**: Accepts 26-22 AWG wire

### Signal Isolation

The motor control board includes three optocouplers providing galvanic isolation:
- **Optocoupler 1**: Isolates F-PWM input signal (low-voltage control → high-voltage motor drive)
- **Optocoupler 2**: Isolates F-FG tachometer output (high-voltage motor → low-voltage feedback)
- **Optocoupler 3**: Additional motor control/protection functions

This isolation allows safe direct connection of 3.3V Wemos logic to the control interface.

## Control Interface Pinout

**4-pin Control Connector:**
- **Type**: XH2.54 / 2.54mm pitch pluggable terminal block
- **Pitch**: 2.54mm (0.1" standard breadboard spacing)
- **Current rating**: Typically 3A per pin
- **Voltage rating**: 250V

| Pin | Signal | Wire Color | Function | Wemos Connection |
|-----|--------|------------|----------|------------------|
| 1 | GND | Black | Ground | GND |
| 2 | +12V | Red | 12V 1.25A isolated supply | Via buck converter to 5V |
| 3 | F-PWM | White | PWM/Analog speed control input | D5 (GPIO14) |
| 4 | F-FG | Yellow | Tachometer pulse output | D6 (GPIO12) |

**Note**: Standard wire color convention is used (black for GND, red for +12V).

## F-PWM Input Characteristics

The F-PWM pin accepts either:
1. **Analog voltage**: 0-6V DC (potentiometer mode shown in board documentation)
2. **PWM signal**: Digital pulses filtered to equivalent analog voltage

When using PWM from ESP8266 (0-3.3V output):
- 0% duty cycle → 0V → Motor stopped
- 50% duty cycle → ~1.65V → ~27% motor speed
- 100% duty cycle → ~3.3V → ~55% motor speed

The board includes an RC filter that converts PWM pulses to averaged DC voltage. The 3.3V maximum from ESP8266 limits the achievable motor speed to approximately 55% of full capability. For full-range control, a voltage level shifter to 0-6V would be required.

## ESP-NOW Communication Protocol

### Data Structure

```cpp
typedef struct struct_message {
  float speed_setpoint;  // Speed percentage (0-100%) or RPM value
} struct_message;
```

### Communication Flow

1. **Airleaf → Motor Controller**: Speed setpoint (0-100%)
2. **Motor Controller → Airleaf**: Current RPM feedback
3. Update interval: ~1 second for RPM feedback, immediate for setpoint changes

---

## Wemos #1: Motor Controller Configuration

The complete motor controller configuration is available in the repository at:
- `motor-controller/motor-controller.yaml` - Main ESPHome configuration
- `motor-controller/espnow_receiver.h` - ESP-NOW receiver implementation

**Key Configuration Points:**

### Hardware Connections
- **PWM Output**: D5 (GPIO14) → F-PWM (white wire)
- **Tachometer Input**: D6 (GPIO12) → F-FG (yellow wire)
- **Ground**: GND → GND (black wire)
- **Power**: 5V ← Buck converter ← +12V (red wire)

### PWM Configuration
- Frequency: 1000 Hz (adjustable based on motor response)
- Output range: 0-3.3V (limits motor to ~55% of maximum speed)

### Tachometer Calibration
The `multiply` filter value in the pulse counter sensor requires calibration:
- Formula: `60 / (pulses_per_revolution × update_interval_seconds)`
- Default value: 30.0 (assumes 2 pulses/rev with 2s update interval)
- Adjust after measuring actual motor RPM with external tachometer

### ESP-NOW Setup
On first boot, the motor controller will print its MAC address to the log. Record this address for configuring the Airleaf controller.

---

## Wemos #2: Airleaf Controller Integration

The Airleaf controller integration requires modifications to your existing `Airleaf.yaml` configuration and the addition of an ESP-NOW sender header file.

**Repository Files:**
- `Airleaf.yaml` - Modified to include ESP-NOW motor control
- `espnow_sender.h` - ESP-NOW sender implementation

### Integration Steps

#### 1. Add ESP-NOW Include
Add the include directive to your existing `esphome:` section in `Airleaf.yaml`:
```yaml
esphome:
  name: airleaf
  # ... existing configuration ...
  includes:
    - espnow_sender.h
```

#### 2. Add Motor Control Components
Add these sections to `Airleaf.yaml` (integrate with existing sensors, numbers, and globals sections):

**Motor Speed Setpoint** (add to `number:` section):
```yaml
number:
  # ... existing numbers ...
  
  - platform: template
    name: "Motor Speed Setpoint"
    id: motor_speed_setpoint
    min_value: 0
    max_value: 100
    step: 5
    unit_of_measurement: "%"
    mode: slider
    optimistic: true
    icon: "mdi:fan"
    on_value:
      then:
        - lambda: |-
            ESP_LOGD("motor", "Setting motor speed to %.1f%%", x);
```

**RPM Feedback Sensor** (add to `sensor:` section):
```yaml
sensor:
  # ... existing sensors ...
  
  - platform: template
    name: "Motor RPM Feedback"
    id: motor_rpm_feedback
    unit_of_measurement: 'RPM'
    accuracy_decimals: 0
    icon: "mdi:speedometer"
    state_class: measurement
```

**Motor Controller MAC Address** (add to `globals:` section):
```yaml
globals:
  # ... existing globals ...
  
  - id: motor_controller_mac
    type: uint8_t[6]
    restore_value: no
    # IMPORTANT: Replace with actual MAC address from motor controller
    # Format: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
```

#### 3. Configure Motor Controller MAC Address
In the `espnow_sender.h` file, update the `motorControllerMac[]` array with the actual MAC address obtained from the motor controller's log output:
```cpp
uint8_t motorControllerMac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}; // Replace with actual MAC
```

### Communication Protocol
- **Airleaf → Motor Controller**: Speed setpoint (0-100%) sent when value changes
- **Motor Controller → Airleaf**: RPM feedback sent every 1 second
- **Channel**: WiFi channel 1 (must match on both devices)
- **Update behavior**: Immediate for setpoint changes, periodic for RPM feedback

---

## Physical Wiring

### Wemos D1 Mini Pin Mapping

The Wemos D1 Mini uses different labeling for pins compared to the underlying ESP8266 GPIO numbering. Here's the mapping for the pins used in this project:

| Wemos Label | ESP8266 GPIO | Function in This Project | Connection |
|-------------|--------------|--------------------------|------------|
| D5 | GPIO14 | PWM Output | F-PWM (white wire) |
| D6 | GPIO12 | Pulse Counter Input | F-FG (yellow wire) |
| GND | GND | Ground Reference | GND (red wire) |
| 5V | Power | 5V Power Input | From buck converter |

**Additional Wemos D1 Mini Pin Reference:**

| Wemos Label | ESP8266 GPIO | Notes |
|-------------|--------------|-------|
| D0 | GPIO16 | No PWM, no interrupts |
| D1 | GPIO5 | I2C SCL |
| D2 | GPIO4 | I2C SDA |
| D3 | GPIO0 | Boot mode pin, pull-up |
| D4 | GPIO2 | Built-in LED, pull-up |
| D5 | GPIO14 | **Used for F-PWM** |
| D6 | GPIO12 | **Used for F-FG** |
| D7 | GPIO13 | |
| D8 | GPIO15 | Boot mode pin, pull-down |
| RX | GPIO3 | Serial RX |
| TX | GPIO1 | Serial TX |

**Pin Selection Notes:**
- D5 (GPIO14) chosen for PWM output - supports hardware PWM
- D6 (GPIO12) chosen for pulse counter - supports interrupts for accurate counting
- Avoid D0 (GPIO16) for PWM as it doesn't support hardware PWM
- Avoid D3, D4, D8 for critical functions as they affect boot mode

### Motor Controller Board Connections

#### Control Interface (4-pin connector)
| Board Pin | Wire Color | Connection |
|-----------|------------|------------|
| GND | Red | Wemos GND |
| +12V | Black | 12V → 5V buck converter → Wemos 5V |
| F-PWM | White | Wemos D5 (GPIO14) |
| F-FG | Yellow | Wemos D6 (GPIO12) |

#### Motor Interface (5-pin connector)

**Connector Type**: KF2EDGR-3.96-5P (3.96mm pitch pluggable terminal block)

Connect to your 5-wire BLDC motor according to motor manufacturer's pinout:
- +310V (high-voltage DC positive bus)
- HOT_GND (high-voltage ground return)
- +15V (motor control power)
- FG (motor tachometer feedback to board)
- VSP (motor speed control from board)

**WARNING**: Different motor manufacturers may use different pin arrangements even with the same KF2EDGR connector. The pin functions may be in a different order. Always verify motor pinout documentation before connection. Incorrect wiring can damage the motor or control board.

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

### Bill of Materials

**Required Components:**
1. Motor control board (5-wire 310V BLDC controller with isolated control interface)
2. Two (2) Wemos D1 Mini boards (ESP8266)
3. 5-wire BLDC motor (310V, compatible with board)
4. 12V to 5V buck converter (LM2596 or similar, 1A capacity minimum)
5. 4-pin XH2.54 connector cable (for control interface)
6. 5-pin KF2EDGR-3.96mm connector cable (for motor connection)
7. 220V AC power cable with appropriate plug
8. Fuse holder and 2A slow-blow fuse
9. Project enclosure (rated for mains voltage)
10. Wire: 22-16 AWG for motor connections, 26-22 AWG for control signals

**Optional Components:**
- External tachometer for RPM calibration
- Terminal blocks for easier connection management
- Heat shrink tubing for insulation
- Cable ties for cable management
- Multimeter for testing

### Step 1: Flash Motor Controller

1. Flash `motor-controller.yaml` to first Wemos device
2. Connect to WiFi and view logs
3. **Record the MAC address** printed in logs (format: `AA:BB:CC:DD:EE:FF`)

### Step 2: Update Airleaf Configuration

1. Edit `espnow_sender.h` in your Airleaf directory
2. Replace the placeholder MAC address in `motorControllerMac[]` with the actual MAC from Step 1:
   ```cpp
   uint8_t motorControllerMac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
   ```
3. Flash updated Airleaf configuration

### Step 3: Physical Wiring

**WARNING**: Work on the system only when ALL power is disconnected. The motor control board operates at dangerous voltages (220V AC input, 310V DC bus).

#### Wemos #1 to Motor Control Board - Control Connector

Using the 4-pin XH2.54 control connector:

| Control Board Pin | Wire Color | Signal | Wemos D1 Mini Pin |
|-------------------|------------|--------|-------------------|
| Pin 1 | Black | GND | GND |
| Pin 2 | Red | +12V | Via buck converter to 5V pin |
| Pin 3 | White | F-PWM | D5 |
| Pin 4 | Yellow | F-FG | D6 |

**Wiring Steps:**

1. **POWER OFF** all equipment
2. Prepare the 12V to 5V buck converter:
   - Input: Board +12V (red wire) and GND (black wire)
   - Output: Set to 5.0V ±0.1V before connecting to Wemos
   - Capacity: Minimum 500mA (1A recommended)
3. Connect control signals:
   - Black wire (GND) → Wemos GND
   - Red wire (+12V) → Buck converter input positive
   - Black wire (GND) → Buck converter input negative
   - Buck converter output positive → Wemos 5V pin
   - Buck converter output negative → Wemos GND
   - White wire (F-PWM) → Wemos D5
   - Yellow wire (F-FG) → Wemos D6
4. Double-check all connections before applying power

#### Motor to Motor Control Board - Motor Connector

Using the 5-pin KF2EDGR-3.96 motor connector:

1. **Verify your motor's pinout** - do not assume the pin order matches the board labels
2. Consult motor manufacturer documentation for the 5-wire pinout
3. Connect motor wires to corresponding board pins:
   - Match motor +310V to board +310V
   - Match motor HOT_GND to board HOT_GND  
   - Match motor +15V to board +15V
   - Match motor FG to board FG
   - Match motor VSP to board VSP
4. Ensure connections are tight (terminal blocks require secure screw connections)

#### AC Power to Motor Control Board

1. Connect 220V AC to board AC input connector (typically 2-pin terminal block):
   - Line (L) → Board L terminal
   - Neutral (N) → Board N terminal
2. Install appropriate fuse (2A slow-blow recommended for typical BLDC fan motors)
3. Ensure proper earth/ground connection to motor chassis if metallic

#### Safety Checklist Before Power-On

- [ ] All connections verified against wiring diagram
- [ ] Wire colors verified (standard: black=GND, red=+12V)
- [ ] Buck converter output voltage set to 5.0V and tested
- [ ] Motor connector pinout verified against motor documentation
- [ ] No loose wires or connections
- [ ] Insulation appropriate for 220V/310V voltages
- [ ] Enclosure provides protection from accidental contact
- [ ] Emergency shutoff readily accessible

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

Different motors may respond better to different PWM frequencies. The default 1000 Hz (1 kHz) works for most applications, but you can experiment with values between 500Hz and 25kHz in the motor controller configuration file. Higher frequencies produce smoother DC voltage from the RC filter but may increase switching losses.

### Voltage Level Shifting for Full Speed Range

To achieve full motor speed range (0-100% instead of 0-55%), add a voltage level shifter circuit between the Wemos and the F-PWM input. A simple transistor-based level shifter can scale the 0-3.3V output from the Wemos to 0-6V:

**Components needed:**
- NPN transistor: 2N2222 or similar
- Two 10kΩ resistors
- 6V regulator (LM7806) powered from board's 12V supply

The level shifter inverts and amplifies the PWM signal to provide full voltage range control.

### Enhanced Monitoring

Additional template sensors can be added to both configurations for:
- **Motor power estimation**: Calculate approximate power based on RPM and speed setpoint
- **Communication quality monitoring**: Track ESP-NOW delivery success/failure rates
- **Running time tracking**: Log cumulative motor operation hours
- **RPM stability analysis**: Detect vibration or load variations

See the repository configuration files for implementation examples.

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

3. **Startup Sequence**: Implement gradual speed ramp-up to reduce mechanical stress on the motor. This can be done through the motor speed setpoint control by incrementing in small steps with delays.

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

**Repository**: https://github.com/jjdejong/esphome-airleaf

1. **File Organization**:
   ```
   esphome-airleaf/
   ├── Airleaf.yaml              (modified with ESP-NOW integration)
   ├── espnow_sender.h           (new file for Airleaf ESP-NOW sender)
   ├── motor-controller/
   │   ├── motor-controller.yaml (new file - motor controller config)
   │   └── espnow_receiver.h     (new file - ESP-NOW receiver)
   └── docs/
       └── BLDC_Motor_Integration.md (this document)
   ```

2. **Configuration Files**:
   - All ESPHome YAML configurations and C++ header files are available in the repository
   - Refer to the repository files for complete, tested code
   - This document provides integration guidance and hardware specifications

3. **Version Control**: 
   - Motor controller MAC address configuration requires user modification
   - Example MAC address format included in code comments
   - Calibration values (tachometer multiplier) are motor-specific

4. **Dependencies**:
   - ESPHome 2023.x or later
   - ESP8266 Arduino Core with ESP-NOW support
   - No external libraries required beyond standard ESP8266 SDK

5. **Testing Checklist**:
   - [ ] ESP-NOW pairing successful
   - [ ] Speed setpoint transmission verified
   - [ ] RPM feedback reception verified
   - [ ] Motor responds to speed control (0-100%)
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
| 1.1 | 2024-12-24 | Added connector type identification (KF2EDGR-3.96, XH2.54), Wemos D1 Mini pin mapping, detailed wiring instructions, and bill of materials |

---

## Appendix: Connector Specifications

### Control Connector (XH2.54)

**Specifications:**
- Pitch: 2.54mm (0.1")
- Positions: 4 pins
- Wire-to-board connection
- Typically uses crimped terminals
- Compatible with standard 2.54mm pitch headers

**Ordering Information:**
- Search terms: "XH2.54 4 pin connector", "2.54mm JST-XH 4 pin"
- Mating header: 2.54mm straight or right-angle pin header
- Crimp terminals: XH-style 2.54mm crimp pins

### Motor Connector (KF2EDGR-3.96)

**Specifications:**
- Pitch: 3.96mm
- Positions: 5 pins
- Pluggable screw terminal block
- Current: 8-10A per pin
- Voltage: 300V
- Wire range: 22-16 AWG (0.5-1.5mm²)

**Ordering Information:**
- Search terms: "KF2EDGR 3.96mm 5 pin", "KF396 terminal block 5 pin", "3.96mm pitch pluggable terminal 5P"
- Alternatives: DG396, KF396 series
- Includes: Plug housing with screw terminals + mating header for PCB

**Wiring tip**: For permanent installations, consider using ferrules on wire ends before inserting into screw terminals for better connection reliability.

---

*Document prepared for integration with jjdejong/esphome-airleaf repository*
