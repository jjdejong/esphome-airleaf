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

// Global state
bool espnow_initialized = false;
bool peer_added = false;
bool setup_attempted = false;
uint8_t motorControllerMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
sensor::Sensor *fan_speed_sensor = nullptr;

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  // Delivery confirmation received
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

void attempt_espnow_init() {
  if (setup_attempted) {
    return;
  }
  setup_attempted = true;

  // Get the Fan speed sensor by object_id
  for (auto *obj : App.get_sensors()) {
    if (obj->get_object_id() == "fan_speed") {
      fan_speed_sensor = obj;
      break;
    }
  }

  if (fan_speed_sensor == nullptr) {
    id(espnow_send_status).publish_state("Fan sensor not found");
    return;
  }

  // Only initialize if slave motor controller is enabled
  if (!id(motor_controller_enabled).state) {
    id(espnow_send_status).publish_state("Disabled");
    return;
  }

  // Parse MAC address from single text field
  std::string mac_str = id(motor_controller_mac).state;
  if (!parseMacAddress(mac_str, motorControllerMac)) {
    ESP_LOGE("espnow", "Invalid MAC address format: %s (expected AA:BB:CC:DD:EE:FF)", mac_str.c_str());
    id(espnow_send_status).publish_state("Invalid MAC format");
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
    id(espnow_send_status).publish_state("MAC not configured");
    return;
  }

  // Set WiFi channel (must match receiver)
  wifi_set_channel(1);

  if (esp_now_init() != 0) {
    ESP_LOGE("espnow", "Error initializing ESP-NOW");
    id(espnow_send_status).publish_state("Init failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_send_cb(OnDataSent);
  espnow_initialized = true;

  // Add slave motor controller as peer
  int result = esp_now_add_peer(motorControllerMac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  if (result == 0) {
    peer_added = true;
    char status_msg[64];
    snprintf(status_msg, sizeof(status_msg), "Ready - Target: %02X:%02X:%02X:%02X:%02X:%02X",
             motorControllerMac[0], motorControllerMac[1], motorControllerMac[2],
             motorControllerMac[3], motorControllerMac[4], motorControllerMac[5]);
    id(espnow_send_status).publish_state(status_msg);
  } else {
    ESP_LOGE("espnow", "Failed to add slave motor controller peer (error %d)", result);
    char error_msg[32];
    snprintf(error_msg, sizeof(error_msg), "Peer add failed (error %d)", result);
    id(espnow_send_status).publish_state(error_msg);
  }
}

void espnow_loop() {
  // Attempt initialization on first loop (after all states are restored)
  attempt_espnow_init();

  // Only send if slave motor controller is enabled and ESP-NOW is initialized
  if (!id(motor_controller_enabled).state || !espnow_initialized || !peer_added) {
    return;
  }

  // Send motor speed setpoint from "Fan speed" sensor as RPM
  if (fan_speed_sensor == nullptr || !fan_speed_sensor->has_state()) {
    return;
  }

  static float lastRPM = -1;
  static unsigned long lastHeartbeatTime = 0;
  unsigned long currentTime = millis();
  float currentRPM = fan_speed_sensor->state;

  // Send if RPM changed by more than 5 RPM, or send heartbeat every 5 seconds
  bool shouldSend = false;
  bool isHeartbeat = false;
  if (abs(currentRPM - lastRPM) > 5.0) {
    shouldSend = true;
    lastRPM = currentRPM;
    lastHeartbeatTime = currentTime;
  } else if (currentTime - lastHeartbeatTime >= 5000) {
    // Send heartbeat to maintain connection status
    shouldSend = true;
    isHeartbeat = true;
    lastHeartbeatTime = currentTime;
  }

  if (shouldSend) {
    outgoingData.speed_setpoint = currentRPM;
    int result = esp_now_send(motorControllerMac, (uint8_t*)&outgoingData, sizeof(outgoingData));

    static unsigned long lastStatusUpdate = 0;
    if (result != 0) {
      char error_msg[48];
      snprintf(error_msg, sizeof(error_msg), "Send failed (error %d)", result);
      id(espnow_send_status).publish_state(error_msg);
    } else {
      // Update status every 10 seconds to avoid flooding HA
      if (currentTime - lastStatusUpdate >= 10000) {
        char status_msg[48];
        snprintf(status_msg, sizeof(status_msg), "Sending: %.0f RPM", currentRPM);
        id(espnow_send_status).publish_state(status_msg);
        lastStatusUpdate = currentTime;
      }
    }
  }
}
