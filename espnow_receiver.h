#include "esphome.h"

extern "C" {
  #include <espnow.h>
  #include <user_interface.h>
}

// Data structure for ESP-NOW communication
typedef struct struct_message {
  float speed_setpoint;
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
