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
  sensor::Sensor *fan_speed_sensor = nullptr;

 public:
  void setup() override {
    // Get the Fan speed sensor by object_id
    for (auto *obj : App.get_sensors()) {
      if (obj->get_object_id() == "fan_speed") {
        fan_speed_sensor = obj;
        break;
      }
    }

    if (fan_speed_sensor == nullptr) {
      ESP_LOGW("espnow", "Fan speed sensor not found - ESP-NOW not initialized");
      return;
    }

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

    // Add slave motor controller as peer
    int result = esp_now_add_peer(motorControllerMac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    if (result == 0) {
      peer_added = true;
      ESP_LOGI("espnow", "Slave motor controller peer added: %02X:%02X:%02X:%02X:%02X:%02X",
               motorControllerMac[0], motorControllerMac[1], motorControllerMac[2],
               motorControllerMac[3], motorControllerMac[4], motorControllerMac[5]);
    } else {
      ESP_LOGE("espnow", "Failed to add slave motor controller peer (error %d)", result);
    }

    // Print this device's MAC address
    uint8_t mac[6];
    wifi_get_macaddr(STATION_IF, mac);
    ESP_LOGI("espnow", "Airleaf MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI("espnow", "ESP-NOW sender initialized successfully");
  }

  void loop() override {
    // Only send if slave motor controller is enabled and ESP-NOW is initialized
    if (!id(motor_controller_enabled).state || !espnow_initialized || !peer_added) {
      return;
    }

    // Send motor speed setpoint from "Fan speed" sensor as RPM
    if (fan_speed_sensor == nullptr || !fan_speed_sensor->has_state()) {
      return;
    }

    static float lastRPM = -1;
    float currentRPM = fan_speed_sensor->state;

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
