#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "DHT20.h"
#include "Wire.h"
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

#define LED_PIN 2
#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22

// config
constexpr char WIFI_SSID[] = "NHA 138";
constexpr char WIFI_PASSWORD[] = "138nguyentrai";
constexpr char TOKEN[] = "7fd15ugptp5cs2yil8ad";
constexpr char THINGSBOARD_SERVER[] = "app.coreiot.io";
constexpr uint16_t THINGSBOARD_PORT = 1883U;

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

constexpr char BLINKING_INTERVAL_ATTR[] = "blinkingInterval";
constexpr char LED_MODE_ATTR[] = "ledMode";
constexpr char LED_STATE_ATTR[] = "ledState";

// flag to prevent crashes
SemaphoreHandle_t tbMutex;

volatile bool attributesChanged = false;
volatile int ledMode = 0;
volatile bool ledState = false;
constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

constexpr int16_t telemetrySendInterval = 10000U;

constexpr std::array<const char *, 2U> SHARED_ATTRIBUTES_LIST = {
  LED_STATE_ATTR,
  BLINKING_INTERVAL_ATTR
};

WiFiClient wifiClient;
Arduino_MQTT_Client tbAdapter(wifiClient);
ThingsBoard tb(tbAdapter, MAX_MESSAGE_SIZE);
DHT20 dht20;

// led control
RPC_Response setLedSwitchState(const RPC_Data &data) {
    Serial.println("Received Switch state");
    bool newState = data;
    Serial.print("Switch state change: ");
    Serial.println(newState);
    digitalWrite(LED_PIN, newState);
    attributesChanged = true;
    return RPC_Response("setLedSwitchValue", newState);
}

const std::array<RPC_Callback, 1U> callbacks = {
  RPC_Callback{ "setLedSwitchValue", setLedSwitchState }
};

void processSharedAttributes(const Shared_Attribute_Data &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), BLINKING_INTERVAL_ATTR) == 0) {
      const uint16_t new_interval = it->value().as<uint16_t>();
      if (new_interval >= BLINKING_INTERVAL_MS_MIN && new_interval <= BLINKING_INTERVAL_MS_MAX) {
        blinkingInterval = new_interval;
        Serial.print("Blinking interval is set to: ");
        Serial.println(new_interval);
      }
    } else if (strcmp(it->key().c_str(), LED_STATE_ATTR) == 0) {
      ledState = it->value().as<bool>();
      digitalWrite(LED_PIN, ledState);
      Serial.print("LED state is set to: ");
      Serial.println(ledState);
    }
  }
  attributesChanged = true;
}

const Shared_Attribute_Callback attributes_callback(
    &processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend()
);
const Attribute_Request_Callback attribute_shared_request_callback(
    &processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend()
);

// connect to wifi
void InitWiFi() {
  Serial.println("Connecting to AP ...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS); // Changed to vTaskDelay
    Serial.print(".");
  }
  Serial.println("\nConnected to AP");
}

// Task 1: Network (Handles Connection & Incoming Data)
void task_network(void *pvParameters) {
  for (;;) {
    // maintain connection
    if (WiFi.status() != WL_CONNECTED) {
      InitWiFi();
    }

    // raise flag to take the connection, avoid collision (cause hangs)
    if (xSemaphoreTake(tbMutex, portMAX_DELAY) == pdTRUE) {
        
        if (!tb.connected()) {
            Serial.print("Connecting to: ");
            Serial.print(THINGSBOARD_SERVER);
            Serial.print(" with token ");
            Serial.println(TOKEN);
            
            if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
                Serial.println("Failed to connect");
            } else {
                tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());

                Serial.println("Subscribing for RPC...");
                if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
                    Serial.println("Failed to subscribe for RPC");
                }

                if (!tb.Shared_Attributes_Subscribe(attributes_callback)) {
                    Serial.println("Failed to subscribe for shared attribute updates");
                }
                Serial.println("Subscribe done");

                if (!tb.Shared_Attributes_Request(attribute_shared_request_callback)) {
                    Serial.println("Failed to request for shared attributes");
                }
            }
        }

        if (attributesChanged && tb.connected()) {
            attributesChanged = false;
            tb.sendAttributeData(LED_STATE_ATTR, digitalRead(LED_PIN));
        }

        // keep connection alive
        if (tb.connected()) {
            tb.loop();
        }

        xSemaphoreGive(tbMutex); // lower flag
    }

    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

// TASK 2: Sensor (Reads DHT & Uploads)
void task_sensor(void *pvParameters) {
  for (;;) {
    // read sensor
    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();

    if (isnan(temperature) || isnan(humidity)) {
       Serial.println("Failed to read from DHT20 sensor!");
    } else {
       Serial.print("Temperature: "); Serial.print(temperature);
       Serial.print(" °C, Humidity: "); Serial.print(humidity);
       Serial.println(" %");

       // raise flag to take the connection, avoid collision (cause hangs)
       if (xSemaphoreTake(tbMutex, portMAX_DELAY) == pdTRUE) {
           if (tb.connected()) {
              // send data
              tb.sendTelemetryData("temperature", temperature);
              tb.sendTelemetryData("humidity", humidity);
               
              tb.sendAttributeData("rssi", WiFi.RSSI());
              tb.sendAttributeData("channel", WiFi.channel());
              tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
              tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
              tb.sendAttributeData("ssid", WiFi.SSID().c_str());
           }
           xSemaphoreGive(tbMutex); // lower flag
       }
    }

    vTaskDelay(telemetrySendInterval / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(SERIAL_DEBUG_BAUD);
  
  // configure watchdog to prevent network hangs
  esp_task_wdt_config_t twdt_config = {
      .timeout_ms = 30000,
      .idle_core_mask = (1 << 0), 
      .trigger_panic = true,
  };
  esp_task_wdt_init(&twdt_config);


  pinMode(LED_PIN, OUTPUT);
  delay(1000);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  dht20.begin();

  tbMutex = xSemaphoreCreateMutex();

  InitWiFi();

  xTaskCreatePinnedToCore(task_network, "Network", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(task_sensor, "Sensors", 4096, NULL, 1, NULL, 1);
  
  Serial.println("Tasks created. Deleting setup task...");
  
  // delete loop() task to avoid collision (cause hangs)
  vTaskDelete(NULL); 
}

void loop() {
}