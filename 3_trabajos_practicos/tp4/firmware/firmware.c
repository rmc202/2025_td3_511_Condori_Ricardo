#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stdio.h"
#include "lcd.h"
#include "bmp280.h"

#define I2C_PORT i2c0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define LCD_ADDR 0x27

QueueHandle_t xColaBMP280;
SemaphoreHandle_t xI2CMutex;

typedef struct {
    float temperatura;
    float presion;
} bmp280_data_t;

void tarea_bmp280(void *pvParameters) {
    struct bmp280_calib_param calib;
    int32_t raw_temp, raw_pres;
    bmp280_data_t datos;

    bmp280_init(I2C_PORT); // Inicializa el sensor
    bmp280_get_calib_params(&calib);

    while (1) {
        if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            bmp280_read_raw(&raw_temp, &raw_pres);
            datos.temperatura = bmp280_convert_temp(raw_temp, &calib);
            datos.presion = bmp280_convert_pressure(raw_pres, raw_temp, &calib) / 1000.0f; // kPa

            // Mostrar por consola
            printf("BMP280 -> Temp: %.2f C, Pres: %.2f kPa\n", datos.temperatura, datos.presion);

            xSemaphoreGive(xI2CMutex);
            xQueueSend(xColaBMP280, &datos, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void tarea_lcd(void *pvParameters) {
    bmp280_data_t datos;
    char linea1[17], linea2[17];

    while (1) {
        if (xQueueReceive(xColaBMP280, &datos, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                lcd_clear();
                snprintf(linea1, sizeof(linea1), "Temp: %5.1f C", datos.temperatura);
                snprintf(linea2, sizeof(linea2), "Pres: %6.1f kPa", datos.presion);
                lcd_set_cursor(0, 0);
                lcd_string(linea1);
                lcd_set_cursor(0, 1);
                lcd_string(linea2);
                xSemaphoreGive(xI2CMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main() {
    stdio_init_all();
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    lcd_init(I2C_PORT, LCD_ADDR);
    lcd_clear();

    xI2CMutex = xSemaphoreCreateMutex();
    xColaBMP280 = xQueueCreate(2, sizeof(bmp280_data_t));

    xTaskCreate(tarea_bmp280, "BMP280", 1024, NULL, 2, NULL);
    xTaskCreate(tarea_lcd, "LCD", 1024, NULL, 1, NULL);

    vTaskStartScheduler();
    while (1);
}