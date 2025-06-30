#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stdio.h"
#include "lcd.h" 

#define PWM_TAKE_PIN 1    // Pin de entrada para medir frecuencia
#define PWM_OUT_PIN  2    // Pin de salida para generar PWM
#define PERIODO_MEDICION_MS 1000

#define PWM_PERIOD_MS 10      // Periodo total del PWM 
#define PWM_DUTY_MS   5       // Ciclo activo (50% duty cycle)

// Pines y dirección para I2C LCD
#define I2C_PORT i2c0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define LCD_ADDR 0x27 

volatile uint32_t contador_flancos = 0;

void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == PWM_TAKE_PIN && (events & GPIO_IRQ_EDGE_RISE)) {
        contador_flancos++;
    }
}

void tarea_frecuencimetro_irq(void *pvParameters) {
    uint32_t contador_local = 0;
    char buffer[17];

    while (1) {
        taskENTER_CRITICAL();
        contador_local = contador_flancos;
        contador_flancos = 0;
        taskEXIT_CRITICAL();

        printf("Frecuencia medida: %lu Hz\n", contador_local);

        // Mostrar en LCD
        snprintf(buffer, sizeof(buffer), "%10lu Hz", contador_local);
        lcd_set_cursor(0, 1);
        lcd_string(buffer);

        vTaskDelay(pdMS_TO_TICKS(PERIODO_MEDICION_MS));
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

    // Inicializa I2C y LCD
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    
    lcd_init(I2C_PORT, LCD_ADDR);
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string("Frecuencia:");

    gpio_init(PWM_TAKE_PIN);
    gpio_set_dir(PWM_TAKE_PIN, GPIO_IN);
    gpio_pull_down(PWM_TAKE_PIN);

    // Configura la interrupción por flanco ascendente
    gpio_set_irq_enabled_with_callback(PWM_TAKE_PIN, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    xTaskCreate(tarea_frecuencimetro_irq, "Frecuencimetro_IRQ", 1024, NULL, 1, NULL);
    xTaskCreate(tarea_pwm_generador, "PWM_Generador", 512, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1);