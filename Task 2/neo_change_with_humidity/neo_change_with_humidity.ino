#include <Arduino.h>
#include <DHT20.h>
#include <Adafruit_NeoPixel.h>

#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22
#define LED_PIN GPIO_NUM_0
#define NUM_LEDS 1

DHT20 DHT;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

SemaphoreHandle_t humidityFlag;
float sharedHumidity = 0.0;

// Task 1: Read Temperature
void read_temp_task(void *pvParameters){
  while(1){
    DHT.read(); 
    sharedHumidity = DHT.getHumidity(); 
    
    Serial.print("Sensor Task: Read Humidity ");
    Serial.println(sharedHumidity, 1);

    // raise flag
    xSemaphoreGive(humidityFlag);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Task 2: Update Neo Led 
void neo_update_task(void *pvParameters){
  strip.begin();
  strip.clear();
  strip.show();

  while(1){
    if (xSemaphoreTake(humidityFlag, portMAX_DELAY) == pdTRUE) { // wait for flag
      if (sharedHumidity < 40) {
        strip.setPixelColor(0, strip.Color(0, 0, 255)); // blue
      }
      else if (sharedHumidity >= 40 && sharedHumidity <= 55) {
        strip.setPixelColor(0, strip.Color(0, 255, 0)); // green
      }
      else {
        strip.setPixelColor(0, strip.Color(255, 0, 0)); // red
      }
      
      strip.show();
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  DHT.begin();

  humidityFlag = xSemaphoreCreateBinary();

  xTaskCreate(read_temp_task, "Sensor Task", 2048, NULL, 1, NULL);
  xTaskCreate(neo_update_task, "LED Task", 2048, NULL, 1, NULL);
}

void loop() {
}