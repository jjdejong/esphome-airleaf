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

// Motor controller MAC address - UPDATE THIS AFTER FLASHING MOTOR CONTROLLER
// Get the MAC from motor controller's logs and update here
uint8_t motorControllerMac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// Callback when data is sent
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    ESP_LOGD("espnow", "Motor setpoint delivered successfully");
  } else {
    ESP_LOGW("espnow", "Motor setpoint delivery failed");
  }
}

class ESPNowSender : public Component {
 public:
  void setup() override {
    // Set WiFi channel (must match receiver)
    wifi_set_channel(1);

    if (esp_now_init() != 0) {
      ESP_LOGE("espnow", "Error initializing ESP-NOW");
      return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
    esp_now_register_send_cb(OnDataSent);

    // Add motor controller as peer
    esp_now_add_peer(motorControllerMac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

    // Print this device's MAC address
    uint8_t mac[6];
    wifi_get_macaddr(STATION_IF, mac);
    ESP_LOGI("espnow", "Airleaf MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI("espnow", "ESP-NOW sender initialized");
  }

  void loop() override {
    // Send motor speed setpoint from "Fan speed" sensor as RPM
    static float lastRPM = -1;
    float currentRPM = id(fan_speed).state;

    // Send if RPM changed by more than 5 RPM to reduce traffic
    if (abs(currentRPM - lastRPM) > 5.0) {
      lastRPM = currentRPM;

      outgoingData.speed_setpoint = currentRPM;
      esp_now_send(motorControllerMac, (uint8_t*)&outgoingData, sizeof(outgoingData));

      ESP_LOGD("espnow", "Sent motor speed setpoint: %.0f RPM", currentRPM);
    }
  }
};
