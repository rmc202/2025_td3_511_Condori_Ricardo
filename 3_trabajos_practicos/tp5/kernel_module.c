#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/gpio.h>  // Por compatibilidad
#include "gpio_driver.h" // Librería de ayuda

// Parámetro del módulo: número de GPIO
static int gpio_num = 17;
module_param(gpio_num, int, 0644);
MODULE_PARM_DESC(gpio_num, "Número de GPIO para el LED");

static struct task_struct *thread_on;
static struct task_struct *thread_off;

static int thread_fn_on(void *data)
{
    while (!kthread_should_stop()) {
        gpio_set_value(gpio_num, 1);
        pr_info("LED ON (GPIO %d)\n", gpio_num);
        msleep(500);
    }
    return 0;
}

static int thread_fn_off(void *data)
{
    while (!kthread_should_stop()) {
        gpio_set_value(gpio_num, 0);
        pr_info("LED OFF (GPIO %d)\n", gpio_num);
        msleep(500);
    }
    return 0;
}

static int __init blinky_init(void)
{
    int ret;

    pr_info("Cargando módulo blinky_led con GPIO=%d\n", gpio_num);

    // Inicializar el GPIO
    ret = gpio_driver_init(gpio_num, GPIO_DIR_OUT);
    if (ret < 0) {
        pr_err("Error al inicializar el GPIO %d\n", gpio_num);
        return ret;
    }

    // Crear los dos hilos
    thread_on = kthread_run(thread_fn_on, NULL, "blinky_thread_on");
    if (IS_ERR(thread_on)) {
        pr_err("Error al crear hilo ON\n");
        gpio_driver_release(gpio_num);
        return PTR_ERR(thread_on);
    }

    thread_off = kthread_run(thread_fn_off, NULL, "blinky_thread_off");
    if (IS_ERR(thread_off)) {
        pr_err("Error al crear hilo OFF\n");
        kthread_stop(thread_on);
        gpio_driver_release(gpio_num);
        return PTR_ERR(thread_off);
    }

    pr_info("Módulo blinky_led cargado correctamente\n");
    return 0;
}

static void __exit blinky_exit(void)
{
    pr_info("Descargando módulo blinky_led...\n");

    if (thread_on)
        kthread_stop(thread_on);
    if (thread_off)
        kthread_stop(thread_off);

    gpio_driver_release(gpio_num);

    pr_info("Módulo blinky_led descargado correctamente\n");
}

module_init(blinky_init);
module_exit(blinky_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tu Nombre");
MODULE_DESCRIPTION("Blinky con GPIO configurable y dos hilos kernel