#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stdio.h"

#define PWM_TAKE_PIN 1    // Pin de entrada para medir frecuencia
#define PWM_OUT_PIN  2    // Pin de salida para generar PWM
#define PERIODO_MEDICION_MS 1000

#define PWM_PERIOD_MS 10      // Periodo total del PWM 
#define PWM_DUTY_MS   5       // Ciclo activo (50% duty cycle)

void tarea_frecuencimetro_polling(void *pvParameters) {
    uint32_t contador = 0;
    bool last_state = false;
    bool current_state = false;

    while (1) {
        contador = 0;
        last_state = gpio_get(PWM_TAKE_PIN);

        TickType_t start = xTaskGetTickCount();
        while (xTaskGetTickCount() - start < pdMS_TO_TICKS(PERIODO_MEDICION_MS)) {
            current_state = gpio_get(PWM_TAKE_PIN);
            if (!last_state && current_state) {
                contador++;
            }
            last_state = current_state;
            vTaskDelay(pdMS_TO_TICKS(1)); // Pequeño delay para no saturar la CPU
        }

        printf("Frecuencia medida: %lu Hz\n", contador);
    }
}

void tarea_pwm_generador(void *pvParameters) {
    gpio_init(PWM_OUT_PIN);
    gpio_set_dir(PWM_OUT_PIN, GPIO_OUT);

    while (1) {
        gpio_put(PWM_OUT_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(PWM_DUTY_MS));
        gpio_put(PWM_OUT_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(PWM_PERIOD_MS - PWM_DUTY_MS));
    }
}

int main() {
    stdio_init_all();

    gpio_init(PWM_TAKE_PIN);
    gpio_set_dir(PWM_TAKE_PIN, GPIO_IN);
    gpio_pull_down(PWM_TAKE_PIN);

    xTaskCreate(tarea_frecuencimetro_polling, "Contador_Flancos_Polling", 1024, NULL, 1, NULL);
    xTaskCreate(tarea_pwm_generador, "PWM_Generador", 512, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1);
}