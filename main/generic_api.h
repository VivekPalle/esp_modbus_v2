/*
 * generic_api.h
 *
 *  Created on: 11-Mar-2024
 *      Author: blaze
 */

#ifndef MAIN_GENERIC_API_H_
#define MAIN_GENERIC_API_H_


#include "esp_mac.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"


#define restart_nvs_key "restart_nvs_key"
#define blaze_mem "BLAZE_MEM"
#define ssid_nvs_key "SSID"
#define pass_nvs_key "PASSWORD"

extern char ssid_config_global[35];
extern char pass_config_global[65];

void http_init(void);
void json_validate(void);

esp_err_t get_what_saved_uint32(const char* STORAGE_NAMESPACE, char* key_value,uint32_t* read_value_u32);
esp_err_t save_run_time_uint32(const char* STORAGE_NAMESPACE, char* key_str, uint32_t read_value_uint32);
esp_err_t get_what_saved_str(const char* STORAGE_NAMESPACE, char* key_value,char* data_ptr);
esp_err_t save_run_time_str(const char* STORAGE_NAMESPACE, char* key_str, char* run_time);
esp_err_t erase_single_nvs_key(const char* storage_name,const char* nvs_key);


void get_latest_time(char *time_buff);
void register_mdns_service();
void init_mdns(void);
void initialize_time(void);

uint32_t get_restart_count();
uint32_t update_restart_count(uint8_t get_update_restart_count);
void get_ssid(char *ssid_data);
int get_parent_rssi(void);
void get_mac(char *wifi_mac);


#endif /* MAIN_GENERIC_API_H_ */
