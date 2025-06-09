#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Cola para pasar datos del ADC entre tareas
QueueHandle_t adc_queue;

// Tarea que lee del ADC (sensor de temperatura)
void task_read_adc(void *pvParameters) {
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);  // Canal 4 → sensor de temperatura interno

    while (true) {
        uint16_t adc_raw = adc_read();  // Valor crudo (0–4095)
        xQueueSend(adc_queue, &adc_raw, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1000));  // Cada 1 segundo
    }
}

// Tarea que convierte y muestra temperatura
void task_print_temperature(void *pvParameters) {
    uint16_t adc_raw;

    while (true) {
        if (xQueueReceive(adc_queue, &adc_raw, portMAX_DELAY) == pdTRUE) {
            // Convertir a voltaje
            const float V_REF = 3.3f;
            const float conversion_factor = V_REF / (1 << 12);  // 3.3 / 4096
            float voltage = adc_raw * conversion_factor;

            // Convertir a °C (según datasheet)
            float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;

            printf("Temperatura: %.2f °C\n", temperature);
        }
    }
}

int main() {
    stdio_init_all();

    // Crear la cola para transferir datos ADC (uint16_t)
    adc_queue = xQueueCreate(10, sizeof(uint16_t));
    if (adc_queue == NULL) {
        printf("Error al crear la cola\n");
        while (1);
    }

    // Crear tareas
    xTaskCreate(task_read_adc, "ReadADC", 256, NULL, 1, NULL);
    xTaskCreate(task_print_temperature, "PrintTemp", 256, NULL, 1, NULL);

    // Iniciar el scheduler de FreeRTOS
    vTaskStartScheduler();

    // Nunca debería llegar aquí
    while (1) {}
}
