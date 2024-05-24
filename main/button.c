/*
 * button.c
 *
 *  Created on: 11-Mar-2024
 *      Author: dell-emb
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "rom/gpio.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define GPIO_INPUT_IO 20
#define ESP_INTR_FLAG_DEFAULT 0
#define TAG "GPIO"

SemaphoreHandle_t xSemaphore = NULL;

char button_count = 0;
char count_flag = 0;


void IRAM_ATTR button_isr_handler(void* arg) {
    // notify the button task
    xSemaphoreGiveFromISR(xSemaphore, NULL);
}


/* Task that will react to button clicks */
void button_task(void *arg) {
	// infinite loop
	for (;;) {

		// wait for the notification from the ISR
		if (xSemaphoreTake(xSemaphore,portMAX_DELAY) == pdTRUE) {
			if (gpio_get_level(GPIO_INPUT_IO) == 0) {
				button_count++;
				ESP_LOGI(TAG, " Button Press ");
			}
		}
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

/* Task will monitor the button count and resets accordingly */
static void basic_task(void *arg) {
	while (1) {
		if (button_count > 0) {
			count_flag++;
			printf("\ncount_flag = %d\n", count_flag);
		}

		if (count_flag >= 5) {
			printf("\nButton count = %d\n", button_count);
			if (button_count == 5) {
				ESP_ERROR_CHECK(nvs_flash_erase()); // It will erase complete NVS storage
				esp_restart();
			}
			count_flag = 0;
			button_count = 0;
		}
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}

/* Initialize the push button on Development board */
void SW_init(void)
{
    xSemaphore = xSemaphoreCreateBinary();

    // configure button and led pins as GPIO pins
    gpio_pad_select_gpio(GPIO_INPUT_IO);

    // set the correct direction
    gpio_set_direction(GPIO_INPUT_IO, GPIO_MODE_INPUT);

    ///new line
    gpio_set_pull_mode(GPIO_INPUT_IO, GPIO_PULLUP_ONLY);

    // enable interrupt on falling (1->0) edge for button pin//GPIO_INTR_NEGEDGE
    gpio_set_intr_type(GPIO_INPUT_IO, GPIO_INTR_NEGEDGE);

    // start the task that will handle the button
    xTaskCreate(button_task, "button_task", 2*1024, NULL, 7, NULL);

    xTaskCreate(basic_task, "basic_task", 1024 * 2, NULL, 2, NULL);
    // install ISR service with default configuration
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    // attach the interrupt service routine
    gpio_isr_handler_add(GPIO_INPUT_IO, button_isr_handler, (void*) GPIO_INPUT_IO);
}
