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

// Global variable to track last received time
unsigned long lastRecvTime = 0;
bool espnow_receiver_initialized = false;

// Callback when data is received from Airleaf controller
void OnDataRecv(uint8_t *mac_addr, uint8_t *data, uint8_t data_len) {
  memcpy(&incomingData, data, sizeof(incomingData));

  float target_rpm = incomingData.speed_setpoint;

  // Store sender MAC for sending RPM feedback
  memcpy(id(sender_mac), mac_addr, 6);

  // Update master MAC address display
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  id(master_mac_address).publish_state(mac_str);

  // Update connection status
  id(espnow_connection_status).publish_state("Connected");
  id(master_connected).publish_state(true);

  // Update last received time for timeout detection
  lastRecvTime = millis();

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

void espnow_receiver_init() {
  if (espnow_receiver_initialized) {
    return;
  }
  espnow_receiver_initialized = true;

  // Set WiFi channel (must match sender)
  wifi_set_channel(1);

  if (esp_now_init() != 0) {
    ESP_LOGE("espnow", "Error initializing ESP-NOW");
    id(espnow_connection_status).publish_state("Init Failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);

  id(espnow_connection_status).publish_state("Waiting for Master");
  id(master_connected).publish_state(false);
}

void espnow_receiver_loop() {
  // Initialize on first loop
  espnow_receiver_init();

  unsigned long currentTime = millis();

  // Check for connection timeout (10 seconds without data)
  static bool was_connected = false;
  if (lastRecvTime > 0 && (currentTime - lastRecvTime > 10000)) {
    if (was_connected) {
      id(espnow_connection_status).publish_state("Connection Lost");
      id(master_connected).publish_state(false);
      was_connected = false;
    }
  } else if (lastRecvTime > 0) {
    was_connected = true;
  }

  // Send RPM feedback to Airleaf controller every second
  static unsigned long lastSendTime = 0;
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
