#include <FreeRTOS.h>
#include <can2040.h>
#include <hardware/regs/intctrl.h>
#include <pico/stdlib.h>
#include <queue.h>
#include <stdio.h>
#include <task.h>

static struct can2040 cbus;

QueueHandle_t msgs;
static void can2040_cb(struct can2040 *cd, uint32_t notify,
                       struct can2040_msg *msg) {
    if (notify == CAN2040_NOTIFY_RX) {
        BaseType_t higher_priority_task_woken = pdFALSE;

        xQueueSendFromISR(
            msgs,
            msg,
            &higher_priority_task_woken
        );

        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

static void PIOx_IRQHandler(void) { can2040_pio_irq_handler(&cbus); }

void canbus_setup(void) {
  uint32_t pio_num = 0;
  uint32_t sys_clock = 125000000, bitrate = 500000;
  uint32_t gpio_rx = 4, gpio_tx = 5;

  // Setup canbus
  can2040_setup(&cbus, pio_num);
  can2040_callback_config(&cbus, can2040_cb);

  // Enable irqs
  irq_set_exclusive_handler(PIO0_IRQ_0, PIOx_IRQHandler);
  irq_set_priority(PIO0_IRQ_0, PICO_DEFAULT_IRQ_PRIORITY);
  irq_set_enabled(PIO0_IRQ_0, 1);

  // Start canbus
  can2040_start(&cbus, sys_clock, bitrate, gpio_rx, gpio_tx);
}

void main_task(__unused void *params) {
    struct can2040_msg data;

    while (1) {
        if (xQueueReceive(msgs, &data, portMAX_DELAY) == pdTRUE) {
            printf("Received CAN ID: 0x%03lX\n",
                   (unsigned long)data.id);

            printf("Data: ");
            for (uint32_t i = 0; i < data.dlc && i < 8; i++) {
                printf("%02X ", data.data[i]);
            }
            printf("\n");
        }
    }
}
void sender_task(void *params) {
    struct can2040_msg message = {0};

    message.id = 0x123;
    message.dlc = 2;

    uint8_t counter = 0;

    while (1) {
        message.data[0] = counter;
        message.data[1] = 0x55;

        int result = can2040_transmit(&cbus, &message);

        if (result == 0) {
            printf("CAN message queued: %u\n", counter);
            counter++;
        } else {
            printf("CAN transmit queue full\n");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
int main(void) {
    stdio_init_all();

    msgs = xQueueCreate(100, sizeof(struct can2040_msg));

    if (msgs == NULL) {
        while (1) {
            tight_loop_contents();
        }
    }

    canbus_setup();

    xTaskCreate(
        main_task,
        "Receiver",
        1024,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    xTaskCreate(
        sender_task,
        "Sender",
        1024,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    vTaskStartScheduler();

    while (1) {
        tight_loop_contents();
    }
}
