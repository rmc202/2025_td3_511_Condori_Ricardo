#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>   // Para kthreads
#include <linux/delay.h>     // Para msleep
#include <linux/sched.h>

static struct task_struct *thread1;
static struct task_struct *thread2;

static int thread_fn1(void *data)
{
    while (!kthread_should_stop()) {
        pr_info("Hola desde el kernel!\n");
        msleep(500);
    }
    pr_info("Hilo 1 finalizado\n");
    return 0;
}

static int thread_fn2(void *data)
{
    while (!kthread_should_stop()) {
        pr_info("Chau desde el kernel!\n");
        msleep(500);
    }
    pr_info("Hilo 2 finalizado\n");
    return 0;
}

static int __init blinky_init(void)
{
    pr_info("Iniciando módulo blinky...\n");

    // Crear hilo 1
    thread1 = kthread_run(thread_fn1, NULL, "blinky_thread1");
    if (IS_ERR(thread1)) {
        pr_err("Error al crear hilo 1\n");
        return PTR_ERR(thread1);
    }

    // Crear hilo 2
    thread2 = kthread_run(thread_fn2, NULL, "blinky_thread2");
    if (IS_ERR(thread2)) {
        pr_err("Error al crear hilo 2\n");
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    pr_info("Módulo blinky cargado correctamente\n");
    return 0;
}

static void __exit blinky_exit(void)
{
    pr_info("Deteniendo hilos...\n");

    if (thread1)
        kthread_stop(thread1);
    if (thread2)
        kthread_stop(thread2);

    pr_info("Módulo blinky descargado\n");
}

module_init(blinky_init);
module_exit(blinky_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tu Nombre");
MODULE_DESCRIPTION("Primer blinky kernel con dos hilos");
