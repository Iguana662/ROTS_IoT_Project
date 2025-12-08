#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT20.h>

#define SDA_PIN 21
#define SCL_PIN 22

LiquidCrystal_I2C lcd(0x27, 20, 4); // LCD2004 (20 columns, 4 rows)
DHT20 DHT;

// similar to a flag but can store data
QueueHandle_t sensorQueue;

struct SensorData {
  float temperature;
  float humidity;
};

// TASK 1: Sensor (Producer) 
void task_sensor(void *pvParameters) {
  SensorData data; // temp & humid package to send

  while(1) {
    DHT.read();
    data.temperature = DHT.getTemperature();
    data.humidity = DHT.getHumidity();

    // send data to queue
    xQueueSend(sensorQueue, &data, portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// --- TASK 2: LCD Update (Consumer) ---
void task_lcd(void *pvParameters) {
  SensorData receivedData; // temp & humid package recieved

  lcd.init();
  lcd.backlight();
  
  // print static labels once
  lcd.setCursor(1, 0);
  lcd.print("Le Phan Nhat Minh");
  lcd.setCursor(0, 2); 
  lcd.print("Temp: ");
  lcd.setCursor(0, 3);
  lcd.print("Humid: ");

  while(1) {
    // wait for flag (carry data)
    if (xQueueReceive(sensorQueue, &receivedData, portMAX_DELAY) == pdTRUE) {
      // update temperature
      lcd.setCursor(6, 2);
      lcd.print(receivedData.temperature, 1); // Use local 'receivedData'
      lcd.print(" C "); 

      // update condition <-temperature
      lcd.setCursor(0, 1);
      lcd.print("Condition: ");
      
      if (receivedData.temperature > 30.0) {
        lcd.print("Hot     ");
      } 
      else if (receivedData.temperature >= 20.0 && receivedData.temperature <= 30.0) {
        lcd.print("Good    ");
      } 
      else {
        lcd.print("Cool    ");
      }

      // update humidity
      lcd.setCursor(7, 3);
      lcd.print(receivedData.humidity, 1); // Use local 'receivedData'
      lcd.print(" % ");
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  DHT.begin();

  sensorQueue = xQueueCreate(1, sizeof(SensorData));

  xTaskCreate(task_sensor, "Sensor", 2048, NULL, 1, NULL);
  xTaskCreate(task_lcd, "LCD", 2048, NULL, 1, NULL);
}

void loop() {}