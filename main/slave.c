/*
 * SPDX-FileCopyrightText: 2016-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// FreeModbus Slave Example ESP32

#include <stdio.h>
#include <stdint.h>
#include "esp_err.h"
#include "mbcontroller.h"       // for mbcontroller defines and api
#include "modbus_params.h"      // for modbus parameters structures
#include "esp_log.h"            // for log_write
#include "sdkconfig.h"
#include "cJSON.h"

#define MB_PORT_NUM_2     (CONFIG_MB_UART_PORT_NUM)   // Number of UART port used for Modbus connection
#define MB_PORT_NUM_1     1   // Number of UART port used for Modbus connection

//#define MB_SLAVE_ADDR   (CONFIG_MB_SLAVE_ADDR)      // The address of device in Modbus network
#define MB_SLAVE_ADDR_1   1      // The address of device in Modbus network
#define MB_SLAVE_ADDR_2   2      // The address of device in Modbus network
#define MB_DEV_SPEED    (CONFIG_MB_UART_BAUD_RATE)  // The communication speed of the UART
#define RX2 18
#define TX2 17
#define RX1 3
#define TX1 2

// Note: Some pins on target chip cannot be assigned for UART communication.
// Please refer to documentation for selected board and target to configure pins using Kconfig.

// Defines below are used to define register start address for each type of Modbus registers
#define HOLD_OFFSET(field) ((uint16_t)(offsetof(holding_reg_params_t, field) >> 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) >> 1))
#define MB_REG_DISCRETE_INPUT_START         (0x0000)
#define MB_REG_COILS_START                  (0x0000)
#define MB_REG_INPUT_START_AREA0            (INPUT_OFFSET(input_data0)) // register offset input area 0
#define MB_REG_INPUT_START_AREA1            (INPUT_OFFSET(input_data4)) // register offset input area 1
#define MB_REG_HOLDING_START_AREA0          0 //(HOLD_OFFSET(holding_data0))
#define MB_REG_HOLDING_START_AREA1          0 //(HOLD_OFFSET(holding_data4))

#define MB_PAR_INFO_GET_TOUT                (10) // Timeout for get parameter info
#define MB_CHAN_DATA_MAX_VAL                (6)
#define MB_CHAN_DATA_OFFSET                 (0.2f)
#define MB_READ_MASK                        (MB_EVENT_INPUT_REG_RD \
                                                | MB_EVENT_HOLDING_REG_RD \
                                                | MB_EVENT_DISCRETE_RD \
                                                | MB_EVENT_COILS_RD)
#define MB_WRITE_MASK                       (MB_EVENT_HOLDING_REG_WR \
                                                | MB_EVENT_COILS_WR)
#define MB_READ_WRITE_MASK                  (MB_READ_MASK | MB_WRITE_MASK)

static const char *TAG = "SLAVE_TEST";
static const char *TAG1 = "SLAVE1_TEST";
static const char *TAG2 = "SLAVE2_TEST";

static void* mbc_slave_handle1 = NULL, *mbc_slave_handle2 = NULL;

static portMUX_TYPE param_lock = portMUX_INITIALIZER_UNLOCKED;

mb_param_info_t reg_info; // keeps the Modbus registers access information
uint8_t MAX_ADDRESSES=40,MAX_SIZE_ARRAY=40;
uint16_t MAX_DATA_SIZE=500;
bool mod_in = 0;

extern uint8_t md_flag;
EXT_RAM_BSS_ATTR uint16_t md_addr[40] = {41000,42000,43000,44000,45000},addrCnt=0;
EXT_RAM_BSS_ATTR uint16_t md_size[40] = {100,100,100,100,100},sizeCnt=0,dataCnt[40];
EXT_RAM_BSS_ATTR uint8_t md_hold_data[40][500] = {{0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09}, {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19}};

extern EXT_RAM_BSS_ATTR char modbus_write[10000];

extern int modbus_write_count;


void set_descr_func(){
	mb_register_area_descriptor_t reg_area; // Modbus register area descriptor structure

	for(int i=0;i<addrCnt;i++){
    	for(int j=0; j<dataCnt[i];j++){
			holding_reg_params.holding_data[i][j] = md_hold_data[i][j];
		}
		reg_area.type = MB_PARAM_HOLDING; // Set type of register area
		reg_area.start_offset = md_addr[i]; //MB_REG_HOLDING_START_AREA0; // Offset of register area in Modbus protocol
		reg_area.address = (void*)&holding_reg_params.holding_data[i][0]; // Set pointer to storage instance
		// Set the size of register storage instance = 150 holding registers
		reg_area.size = md_size[i]*2;//(size_t)(HOLD_OFFSET(holding_data4) - HOLD_OFFSET(test_regs));
		printf("\nset_descriptor:%d",i);
		mbc_slave_set_descriptor(mbc_slave_handle1, reg_area);
		mbc_slave_set_descriptor(mbc_slave_handle2, reg_area);
    }
}


/* An example application of Modbus slave. It is based on freemodbus stack.
   See deviceparams.h file for more information about assigned Modbus parameters.
   These parameters can be accessed from main application and also can be changed
   by external Modbus master host.*/
void modbus_init(void)
{
	// Set UART log level
	esp_log_level_set(TAG, ESP_LOG_INFO);

	// Initialize Modbus controller
	mb_communication_info_t comm_config = { .ser_opts.port = MB_PORT_NUM_2,
#if CONFIG_MB_COMM_MODE_ASCII
	        .ser_opts.mode = MB_ASCII,
	#elif CONFIG_MB_COMM_MODE_RTU
			.ser_opts.mode = MB_RTU,
#endif
			.ser_opts.baudrate = MB_DEV_SPEED,
			.ser_opts.parity = MB_PARITY_NONE, .ser_opts.uid = MB_SLAVE_ADDR_2,
			.ser_opts.data_bits = UART_DATA_8_BITS, .ser_opts.stop_bits =
					UART_STOP_BITS_1 };

	mb_communication_info_t comm_config1 = { .ser_opts.port = MB_PORT_NUM_1,
#if CONFIG_MB_COMM_MODE_ASCII
	            .ser_opts.mode = MB_ASCII,
	    #elif CONFIG_MB_COMM_MODE_RTU
			.ser_opts.mode = MB_RTU,
#endif
			.ser_opts.baudrate = MB_DEV_SPEED,
			.ser_opts.parity = MB_PARITY_NONE, .ser_opts.uid = MB_SLAVE_ADDR_1,
			.ser_opts.data_bits = UART_DATA_8_BITS, .ser_opts.stop_bits =
					UART_STOP_BITS_1 };

	ESP_ERROR_CHECK(mbc_slave_create_serial(&comm_config, &mbc_slave_handle1)); // Initialization of Modbus controller

	ESP_ERROR_CHECK(mbc_slave_create_serial(&comm_config1, &mbc_slave_handle2)); // Initialization of Modbus controller

	set_descr_func();

	// Starts of modbus controller and stack
	ESP_ERROR_CHECK(mbc_slave_start(mbc_slave_handle1));
	// Starts of modbus controller and stack
	ESP_ERROR_CHECK(mbc_slave_start(mbc_slave_handle2));

	// Set UART2 pin numbers 17-Rx & 18-Tx
	ESP_ERROR_CHECK(uart_set_pin(MB_PORT_NUM_2, TX2, RX2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	// Set UART1 pin numbers 2-Rx & 3-Tx
	ESP_ERROR_CHECK(uart_set_pin(MB_PORT_NUM_1, TX1, RX1, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

	ESP_LOGI(TAG, "Modbus slave stack initialized.");
	ESP_LOGI(TAG, "Start modbus test...");
}


void slave_destroy(void) {
	// Destroy of Modbus controller on alarm
	ESP_LOGI(TAG, "Modbus1 controller destroyed.");
//	ESP_ERROR_CHECK(mbc_slave_delete(mbc_slave_handle1));
//	ESP_ERROR_CHECK(mbc_slave_stop(mbc_slave_handle1);
	ESP_ERROR_CHECK(mbc_slave_delete(mbc_slave_handle1));

	// Destroy of Modbus controller on alarm
//	ESP_LOGI(TAG, "Modbus2 controller destroyed.");
	ESP_ERROR_CHECK(mbc_slave_delete(mbc_slave_handle2));
	printf("\nModbus slave destroyed\n");
}


void modbuscomm1(void *parameter)
{
	mb_param_info_t reg_info;
	while (1) {
//		if(destroy == 0){
		// The cycle below will be terminated when parameter holdingRegParams.dataChan0
		// incremented each access cycle reaches the CHAN_DATA_MAX_VAL value.
//		    for(;holding_reg_params.holding_data0 < MB_CHAN_DATA_MAX_VAL;) {
		// Check for read/write events of Modbus master for certain events
		mb_event_group_t event = mbc_slave_check_event(mbc_slave_handle1,
		MB_READ_WRITE_MASK);
		const char *rw_str = (event & MB_READ_MASK) ? "READ" : "WRITE";

		// Filter events and process them accordingly
		if (event & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
			// Get parameter information from parameter queue
			ESP_ERROR_CHECK(
					mbc_slave_get_param_info(mbc_slave_handle1, &reg_info, MB_PAR_INFO_GET_TOUT));
			ESP_LOGI(TAG1,
					"HOLDING %s (%ld us), ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
					rw_str, (uint32_t )reg_info.time_stamp,
					(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
					(uint32_t )reg_info.address, (uint32_t )reg_info.size);
			if (reg_info.address
					== (uint8_t*) &holding_reg_params.holding_data0) {
				portENTER_CRITICAL(&param_lock);
				holding_reg_params.holding_data0 += MB_CHAN_DATA_OFFSET;
				if (holding_reg_params.holding_data0
						>= (MB_CHAN_DATA_MAX_VAL - MB_CHAN_DATA_OFFSET)) {
					coil_reg_params.coils_port1 = 0xFF;
				}
				portEXIT_CRITICAL(&param_lock);
			} else {
				printf("Addr - %ld %ld ", (uint32_t) reg_info.address,
						(uint32_t) ((uint8_t*) &holding_reg_params.holding_data0));
			}
		} else if (event & MB_EVENT_INPUT_REG_RD) {
			ESP_ERROR_CHECK(
					mbc_slave_get_param_info(mbc_slave_handle1, &reg_info, MB_PAR_INFO_GET_TOUT));
			ESP_LOGI(TAG1,
					"INPUT READ (%ld us), ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
					(uint32_t )reg_info.time_stamp,
					(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
					(uint32_t )reg_info.address, (uint32_t )reg_info.size);
		} else if (event & MB_EVENT_DISCRETE_RD) {
			ESP_ERROR_CHECK(
					mbc_slave_get_param_info(mbc_slave_handle1, &reg_info, MB_PAR_INFO_GET_TOUT));
			ESP_LOGI(TAG,
					"DISCRETE READ (%ld us): ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
					(uint32_t )reg_info.time_stamp,
					(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
					(uint32_t )reg_info.address, (uint32_t )reg_info.size);
		} else if (event & (MB_EVENT_COILS_RD | MB_EVENT_COILS_WR)) {
			ESP_ERROR_CHECK(
					mbc_slave_get_param_info(mbc_slave_handle1, &reg_info, MB_PAR_INFO_GET_TOUT));
			ESP_LOGI(TAG1,
					"COILS %s (%ld us), ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
					rw_str, (uint32_t )reg_info.time_stamp,
					(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
					(uint32_t )reg_info.address, (uint32_t )reg_info.size);
			if (coil_reg_params.coils_port1 == 0xFF)
				break;
		}
//								}

		// Get parameter information from parameter queue
//		        ESP_ERROR_CHECK(mbc_slave_get_param_info(mbc_slave_handle1, &reg_info, MB_PAR_INFO_GET_TOUT));
//		        const char* rw_str = (reg_info.type & MB_READ_MASK) ? "READ" : "WRITE";

//		}
		vTaskDelay(100);
	}
	// Destroy of Modbus controller on alarm
	ESP_LOGI(TAG, "Modbus1 controller destroyed.");
	ESP_ERROR_CHECK(mbc_slave_delete(NULL));
}


void modbuscomm2(void *parameter)
{
	mb_param_info_t reg_info;
		while (1) {
	//		if(destroy == 0){
			// The cycle below will be terminated when parameter holdingRegParams.dataChan0
			// incremented each access cycle reaches the CHAN_DATA_MAX_VAL value.
	//		    for(;holding_reg_params.holding_data0 < MB_CHAN_DATA_MAX_VAL;) {
			// Check for read/write events of Modbus master for certain events
			mb_event_group_t event = mbc_slave_check_event(mbc_slave_handle2,
			MB_READ_WRITE_MASK);
			const char *rw_str = (event & MB_READ_MASK) ? "READ" : "WRITE";

			// Filter events and process them accordingly
			if (event & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
				// Get parameter information from parameter queue
				ESP_ERROR_CHECK(
						mbc_slave_get_param_info(mbc_slave_handle2, &reg_info, MB_PAR_INFO_GET_TOUT));
				ESP_LOGI(TAG2,
						"HOLDING %s (%ld us), ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
						rw_str, (uint32_t )reg_info.time_stamp,
						(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
						(uint32_t )reg_info.address, (uint32_t )reg_info.size);
				if (reg_info.address
						== (uint8_t*) &holding_reg_params.holding_data0) {
					portENTER_CRITICAL(&param_lock);
					holding_reg_params.holding_data0 += MB_CHAN_DATA_OFFSET;
					if (holding_reg_params.holding_data0
							>= (MB_CHAN_DATA_MAX_VAL - MB_CHAN_DATA_OFFSET)) {
						coil_reg_params.coils_port1 = 0xFF;
					}
					portEXIT_CRITICAL(&param_lock);
				} else {
					printf("Addr - %ld %ld ", (uint32_t) reg_info.address,
							(uint32_t) ((uint8_t*) &holding_reg_params.holding_data0));
				}
			} else if (event & MB_EVENT_INPUT_REG_RD) {
				ESP_ERROR_CHECK(
						mbc_slave_get_param_info(mbc_slave_handle2, &reg_info, MB_PAR_INFO_GET_TOUT));
				ESP_LOGI(TAG2,
						"INPUT READ (%ld us), ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
						(uint32_t )reg_info.time_stamp,
						(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
						(uint32_t )reg_info.address, (uint32_t )reg_info.size);
			} else if (event & MB_EVENT_DISCRETE_RD) {
				ESP_ERROR_CHECK(
						mbc_slave_get_param_info(mbc_slave_handle2, &reg_info, MB_PAR_INFO_GET_TOUT));
				ESP_LOGI(TAG,
						"DISCRETE READ (%ld us): ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
						(uint32_t )reg_info.time_stamp,
						(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
						(uint32_t )reg_info.address, (uint32_t )reg_info.size);
			} else if (event & (MB_EVENT_COILS_RD | MB_EVENT_COILS_WR)) {
				ESP_ERROR_CHECK(
						mbc_slave_get_param_info(mbc_slave_handle2, &reg_info, MB_PAR_INFO_GET_TOUT));
				ESP_LOGI(TAG2,
						"COILS %s (%ld us), ADDR:%ld, TYPE:%ld, INST_ADDR:0x%ld, SIZE:%ld",
						rw_str, (uint32_t )reg_info.time_stamp,
						(uint32_t )reg_info.mb_offset, (uint32_t )reg_info.type,
						(uint32_t )reg_info.address, (uint32_t )reg_info.size);
				if (coil_reg_params.coils_port1 == 0xFF)
					break;
			}
	//								}

			// Get parameter information from parameter queue
	//		        ESP_ERROR_CHECK(mbc_slave_get_param_info(mbc_slave_handle1, &reg_info, MB_PAR_INFO_GET_TOUT));
	//		        const char* rw_str = (reg_info.type & MB_READ_MASK) ? "READ" : "WRITE";

	//		}
			vTaskDelay(100);
		}
		// Destroy of Modbus controller on alarm
		ESP_LOGI(TAG, "Modbus2 controller destroyed.");
		ESP_ERROR_CHECK(mbc_slave_delete(mbc_slave_handle2));
}



void update_json_resp(unsigned short address, unsigned short n_reg, unsigned char *data_buf){
	 cJSON *root = cJSON_CreateObject();
	 cJSON_AddNumberToObject(root, "st_address", address);
	 cJSON_AddNumberToObject(root, "total_reg", n_reg);
	 cJSON *modbus_data_array = cJSON_AddArrayToObject(root, "reg_data");

	 for (int i = 0; i < n_reg * 2; i++) {
		 char data[10];
		 snprintf(data, sizeof(data), "%02X", data_buf[i]);
		cJSON_AddItemToArray(modbus_data_array, cJSON_CreateString(data));
	}
	 char *str = cJSON_Print(root);
	 strcpy(modbus_write, str);
	 printf("\nJSON data = %s\n", str);
	 cJSON_Delete(root);
	 free(str);
	 modbus_write_count++;
}


void print_modbus_data_hex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

void update_json_modbus(uint16_t address, uint8_t *reg_buffer, uint16_t n_regs) {
	printf("\nReceived address = %d\n", address);
	printf("\nReceived nregs = %d\n", n_regs);
	// Print received data in hex
	print_modbus_data_hex(reg_buffer, n_regs * 2);
	update_json_resp(address, n_regs, reg_buffer);
}



