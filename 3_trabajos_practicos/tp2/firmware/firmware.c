#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Cola para pasar datos del ADC entre tareas
QueueHandle_t adc_queue;

// Prototipo del handler de interrupción
void adc_irq_handler(void);

// Tarea que dispara la conversión ADC periódicamente
void task_trigger_adc(void *pvParameters) {
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);  // Canal 4 → sensor de temperatura interno

    // Configurar FIFO e interrupciones
    adc_fifo_setup(
        true,    // Write each result to the FIFO
        true,    // Enable DMA data request (no usado aquí)
        1,       // DREQ (DMA) cuando al menos 1 muestra presente
        false,   // No error bit
        false    // No byte-shifting
    );
    adc_irq_set_enabled(true);
    irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_irq_handler);
    irq_set_enabled(ADC_IRQ_FIFO, true);

    while (true) {
        adc_run(true);
        adc_hw->cs |= ADC_CS_START_ONCE_BITS; // Disparar conversión
        vTaskDelay(pdMS_TO_TICKS(1000));      // Cada 1 segundo
    }
}

// Handler de interrupción del ADC
void adc_irq_handler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    while (adc_fifo_get_level() > 0) {
        uint16_t adc_raw = adc_fifo_get();
        xQueueSendFromISR(adc_queue, &adc_raw, &xHigherPriorityTaskWoken);
    }
    adc_fifo_drain(); // Limpiar FIFO
    adc_run(false);   // Detener ADC hasta próximo disparo
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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
    xTaskCreate(task_trigger_adc, "TriggerADC", 256, NULL, 1, NULL);
    xTaskCreate(task_print_temperature, "PrintTemp", 256, NULL, 1, NULL);

    // Iniciar el scheduler de FreeRTOS
    vTaskStartScheduler();

    // Nunca debería llegar aquí
    while (1) {}
}
