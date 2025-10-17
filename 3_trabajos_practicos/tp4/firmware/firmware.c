#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "stdio.h"
#include "math.h"
#include "lcd.h"
#include "bmp280.h"

#define I2C_PORT i2c0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define LCD_ADDR 0x27

// New hardware pins
#define BUTTON_PIN 14      // pulsador que cambia pantallas
#define LED_PWM_PIN 16     // LED (GPIO16) con salida PWM

// Application constants
#define SETPOINT_TEMP 20.54f
#define MAX_ERROR 10.0f    // error máximo que saturará la ley del PWM
#define PWM_WRAP 999       // resolución del PWM (0..PWM_WRAP)
#define PWM_CLKDIV 125.0f  // para obtener ~1kHz con wrap=999: 125MHz/(1000*125)=1000Hz

QueueHandle_t xColaBMP280;
SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xButtonSem;      // semaphore dado por la ISR del pulsador
SemaphoreHandle_t xErrorMutex;     // protege current_error

typedef struct {
    float temperatura;
    float presion;
} bmp280_data_t;

volatile uint8_t screen_mode = 0;  // 0: pantalla principal (temp/pres) , 1: setpoint + error
static float current_error = 0.0f; // error absoluto (C). protegido por xErrorMutex

// ISR para el pulsador: solo da el semáforo desde ISR
void button_gpio_callback(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio == BUTTON_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        xSemaphoreGiveFromISR(xButtonSem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void tarea_bmp280(void *pvParameters) {
    struct bmp280_calib_param calib;
    int32_t raw_temp, raw_pres;
    bmp280_data_t datos;

    bmp280_init(I2C_PORT); // Inicializa el sensor
    bmp280_get_calib_params(&calib);

    while (1) {
        if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bmp280_read_raw(&raw_temp, &raw_pres);
            datos.temperatura = bmp280_convert_temp(raw_temp, &calib);
            datos.presion = bmp280_convert_pressure(raw_pres, raw_temp, &calib) / 1000.0f; // kPa

            // Mostrar por consola
            printf("BMP280 -> Temp: %.2f C, Pres: %.2f kPa\n", datos.temperatura, datos.presion);

            xSemaphoreGive(xI2CMutex);

            // actualizar error compartido
            if (xSemaphoreTake(xErrorMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                float err = fabsf(SETPOINT_TEMP - datos.temperatura);
                current_error = err;
                xSemaphoreGive(xErrorMutex);
            }

            // enviar datos a la cola para la tarea LCD
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
            // Para acceder al LCD hay que tomar el mutex I2C
            if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (screen_mode == 0) {
                    // pantalla principal: temp + presion
                    snprintf(linea1, sizeof(linea1), "Temp: %5.1f C", datos.temperatura);
                    snprintf(linea2, sizeof(linea2), "Pres: %6.1f kPa", datos.presion);
                } else {
                    // pantalla secundaria: setpoint y error
                    float err = 0.0f;
                    if (xSemaphoreTake(xErrorMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        err = current_error;
                        xSemaphoreGive(xErrorMutex);
                    }
                    snprintf(linea1, sizeof(linea1), "Set: %5.1f C", SETPOINT_TEMP);
                    snprintf(linea2, sizeof(linea2), "Err: %5.2f C", err);
                }

                lcd_clear();
                lcd_set_cursor(0, 0);
                lcd_string(linea1);
                lcd_set_cursor(0, 1);
                lcd_string(linea2);

                xSemaphoreGive(xI2CMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // refresco LCD moderado
    }
}

// tarea que espera el semáforo dado por la ISR y cambia pantalla (con debounce)
void tarea_button(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(xButtonSem, portMAX_DELAY) == pdTRUE) {
            // debounce sencillo: esperar 50 ms y consumir posibles rebotes
            vTaskDelay(pdMS_TO_TICKS(50));
            // confirmar que el botón sigue presionado (nivel bajo)
            if (!gpio_get(BUTTON_PIN)) {
                // alternar modo de pantalla (protegido)
                taskENTER_CRITICAL();
                screen_mode = (screen_mode == 0) ? 1 : 0;
                taskEXIT_CRITICAL();
            }
            // pequeña espera para evitar re-bloqueo inmediato
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

// tarea que lee current_error y actualiza PWM correspondientemente
void tarea_pwm_control(void *pvParameters) {
    int slice_num = pwm_gpio_to_slice_num(LED_PWM_PIN);
    // configuración inicial (ya hecha en main, pero por si acaso)
    pwm_set_clkdiv(slice_num, PWM_CLKDIV);
    pwm_set_wrap(slice_num, PWM_WRAP);
    pwm_set_enabled(slice_num, true);

    while (1) {
        float err = 0.0f;
        if (xSemaphoreTake(xErrorMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            err = current_error;
            xSemaphoreGive(xErrorMutex);
        }

        // regla: cuanto menor el error -> mayor brillo.
        // Apagar totalmente cuando el error es (prácticamente) 0.
        const float EPS_ZERO = 0.01f;
        uint32_t level = 0;

        if (err <= EPS_ZERO) {
            level = 0; // apagar cuando error nulo (según enunciado)
        } else {
            float norma = fminf(err / MAX_ERROR, 1.0f);
            float duty_frac = 1.0f - norma; // menor error -> mayor duty
            // Si se quiere evitar brillo máximo justo en err->0, podría aplicarse una rampa.
            level = (uint32_t)(duty_frac * (float)PWM_WRAP);
        }

        pwm_set_gpio_level(LED_PWM_PIN, level);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main() {
    stdio_init_all();

    // I2C inicialización (LCD, BMP280)
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // inicializar LCD
    lcd_init(I2C_PORT, LCD_ADDR);
    lcd_clear();

    // inicializar periféricos: boton y PWM LED
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN); // pulsador a GND -> activa al presionar (falling edge)
    // configurar interrupción: flanco de bajada (presionar)
    gpio_set_irq_enabled_with_callback(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &button_gpio_callback);

    // PWM LED
    gpio_set_function(LED_PWM_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(LED_PWM_PIN);
    pwm_set_clkdiv(slice, PWM_CLKDIV);
    pwm_set_wrap(slice, PWM_WRAP);
    pwm_set_gpio_level(LED_PWM_PIN, 0);
    pwm_set_enabled(slice, true);

    // recursos FreeRTOS
    xI2CMutex = xSemaphoreCreateMutex();
    xColaBMP280 = xQueueCreate(2, sizeof(bmp280_data_t));
    xButtonSem = xSemaphoreCreateBinary();
    xErrorMutex = xSemaphoreCreateMutex();

    // crear tareas
    xTaskCreate(tarea_bmp280, "BMP280", 1024, NULL, 2, NULL);
    xTaskCreate(tarea_lcd, "LCD", 1024, NULL, 1, NULL);
    xTaskCreate(tarea_button, "BUTTON", 512, NULL, 3, NULL);
    xTaskCreate(tarea_pwm_control, "PWM_CTRL", 512, NULL, 2, NULL);

    vTaskStartScheduler();

    while (1);
}