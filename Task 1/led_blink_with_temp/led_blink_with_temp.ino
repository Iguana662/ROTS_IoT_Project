#include <Arduino.h>
#include <DHT20.h>

#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22
#define LED_GPIO 2

DHT20 DHT;

SemaphoreHandle_t temperatureFlag;
float sharedTemperature = 0.0;

// Task 1: Read Temperature
void read_temp_task(void *pvParameters) {
  Wire.begin(SDA_PIN, SCL_PIN);
  DHT.begin();

  while (1) {
    DHT.read();
    sharedTemperature = DHT.getTemperature();
    Serial.println(sharedTemperature, 1);

    // raise flag
    xSemaphoreGive(temperatureFlag);

    vTaskDelay(pdMS_TO_TICKS(2000)); 
  }
}

// Task 2: Blink LED based on Temperature
void led_blink_task(void *pvParameters) {
  pinMode(LED_GPIO, OUTPUT);

  while (1) {
    // wait for the flag
    if (xSemaphoreTake(temperatureFlag, portMAX_DELAY) == pdTRUE) {
      
      int delayTime = 0;
      
      // set blink speed based on the temperature
      if (sharedTemperature < 0) {
        delayTime = 2000;
      } else if (sharedTemperature >= 20 && sharedTemperature <= 30) {
        delayTime = 1000;
      } else {
        delayTime = 500;
      }

      // blink
      digitalWrite(LED_GPIO, HIGH);
      vTaskDelay(pdMS_TO_TICKS(delayTime));
      digitalWrite(LED_GPIO, LOW);
      vTaskDelay(pdMS_TO_TICKS(delayTime));
    }
  }
}

void setup() {
  Serial.begin(115200);

  // semaphore
  temperatureFlag = xSemaphoreCreateBinary();

  // tasks
  xTaskCreate(read_temp_task, "Read Temp", 2048, NULL, 1, NULL);
  xTaskCreate(led_blink_task, "Blink LED", 2048, NULL, 1, NULL);
}

void loop() {
}