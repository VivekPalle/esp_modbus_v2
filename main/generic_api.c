/*
 * sntp_time.c
 *
 *  Created on: 04-Mar-2024
 *      Author: blaze
 */

//#include "time_esp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <time.h>
#include "generic_api.h"

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

time_t now;
struct tm timeinfo;


/* Time sync notification callback */
void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI("time", "Notification of a time synchronization event");
}


/* Initializing SNTP server */
static void initialize_sntp(void)
{
    ESP_LOGI("time", "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

/*
 * If 'NTP over DHCP' is enabled, we set dynamic pool address
 * as a 'secondary' server. It will act as a fallback server in case that address
 * provided via NTP over DHCP is not accessible
 */
#if LWIP_DHCP_GET_NTP_SRV && SNTP_MAX_SERVERS > 1
    sntp_setservername(1, "pool.ntp.org");

#if LWIP_IPV6 && SNTP_MAX_SERVERS > 2          // statically assigned IPv6 address is also possible
    ip_addr_t ip6;
    if (ipaddr_aton("2a01:3f7::1", &ip6)) {    // ipv6 ntp source "ntp.netnod.se"
        sntp_setserver(2, &ip6);
    }
#endif  /* LWIP_IPV6 */

#else   /* LWIP_DHCP_GET_NTP_SRV && (SNTP_MAX_SERVERS > 1) */
    // otherwise, use DNS address from a pool
    sntp_setservername(0, "pool.ntp.org");

    sntp_setservername(1, "pool.ntp.org");     // set the secondary NTP server (will be used only if SNTP_MAX_SERVERS > 1)
#endif

    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();

    ESP_LOGI("time", "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        if (sntp_getservername(i)){
            ESP_LOGI("time", "server %d: %s", i, sntp_getservername(i));
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI("time", "server %d: %s", i, buff);
        }
    }
}


void obtain_time(void)
{
    /**
     * NTP server address could be aquired via DHCP,
     * see following menuconfig options:
     * 'LWIP_DHCP_GET_NTP_SRV' - enable STNP over DHCP
     * 'LWIP_SNTP_DEBUG' - enable debugging messages
     *
     * NOTE: This call should be made BEFORE esp aquires IP address from DHCP,
     * otherwise NTP option would be rejected by default.
     */
#ifdef LWIP_DHCP_GET_NTP_SRV
    sntp_servermode_dhcp(0);      // accept NTP offers from DHCP server, if any
#endif

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */

    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI("time", "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
}


/* This API will fetch feed the latest time from SNTP*/
void get_latest_time(char *time_buff) {
	char strftime_buf[64] = {0,};
	time(&now);
	setenv("TZ", "UTC-5:30", 1);
	tzset();
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	strcpy(time_buff, strftime_buf);
}


/* Initialize the time sync with SNTP server */
void initialize_time(void){
	char time_buff[64] = {0,};
	char strftime_buf[64] = {0,};
	time(&now);
	localtime_r(&now, &timeinfo);
	// Is time set? If not, tm_year will be (1970 - 1900).
	if (timeinfo.tm_year < (2016 - 1900)) {   // 2016 - 1900
		ESP_LOGI("time",
				"Time is not set yet. Connecting to WiFi and getting time over NTP.");
		obtain_time();
		// update 'now' variable with current time
		time(&now);
	}


	setenv("TZ", "UTC-5:30", 1);
//	setenv("TZ", "IST-5:30", 1);
	tzset();
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI("time", "The current date/time in India is: %s", strftime_buf);
	strcpy(time_buff, strftime_buf);
	printf("\nTime1 = %s\n", time_buff);

	if (sntp_get_sync_mode() == SNTP_SYNC_MODE_SMOOTH) {
		struct timeval outdelta;
		while (sntp_get_sync_status() == SNTP_SYNC_STATUS_IN_PROGRESS) {
			adjtime(NULL, &outdelta);
			ESP_LOGI("time",
					"Waiting for adjusting time ... outdelta = %jd sec: %li ms: %li us",
					(intmax_t )outdelta.tv_sec, outdelta.tv_usec / 1000,
					outdelta.tv_usec % 1000);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
	}
}


/* To store integer value in NVS */
esp_err_t save_run_time_uint32(const char* STORAGE_NAMESPACE, char* key_str, uint32_t read_value_uint32)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	} else {
		err = nvs_set_u32(my_handle, key_str, read_value_uint32);
		printf((err != ESP_OK) ? "set-Failed!\n" : "set-Done\n");
		err = nvs_commit(my_handle);
		printf((err != ESP_OK) ? "com-Failed!\n" : "com-Done\n");
	}
    // Close
    nvs_close(my_handle);
    return err;
}


/* To get the stored integer value from NVS */
esp_err_t get_what_saved_uint32(const char* STORAGE_NAMESPACE, char* key_value,uint32_t* read_value_u32)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);

    if (err != ESP_OK) {
	   printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	} else {
		err = nvs_get_u32(my_handle, key_value, read_value_u32);
        switch (err) {
			case ESP_OK:
				break;
			case ESP_ERR_NVS_NOT_FOUND:
				printf("The value is not initialized yet!\n");
				break;
			default :
				printf("Error (%s) reading!\n", esp_err_to_name(err));
        }

	}
    nvs_close(my_handle);
    return err;
}


/* To store character string in NVS */
esp_err_t get_what_saved_str(const char* STORAGE_NAMESPACE, char* key_value,char* data_ptr)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    size_t required_size = 0;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

     // obtain required memory space to store blob being read from NVS
    err = nvs_get_blob(my_handle, key_value, NULL, &required_size);

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (required_size == 0) {
        printf("Nothing saved yet!\n");
    } else {
        char* run_time = malloc(required_size);
        err = nvs_get_blob(my_handle, key_value, run_time, &required_size);
        if (err != ESP_OK) {
            free(run_time);
            return err;
        }
        for (int i = 0; i < required_size; i++) {
            data_ptr[i] = run_time[i];
        }
        data_ptr[required_size] = '\0';
        free(run_time);
    }
    // Close
    nvs_close(my_handle);
    return err;
}


/* To get the character string from NVS */
esp_err_t save_run_time_str(const char* STORAGE_NAMESPACE, char* key_str, char* run_time)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    // Read the size of memory space required for blob
    size_t required_size = 0;  // value will default to 0, if not set yet in NVS

    // Write value including previously saved blob if available
    required_size = strlen(run_time);
    err = nvs_set_blob(my_handle, key_str, run_time, required_size);

    if (err != ESP_OK) return err;

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK) return err;

    // Close
    nvs_close(my_handle);
    return err;
}


/* To erase single NVS key */
esp_err_t erase_single_nvs_key(const char* storage_name,const char* nvs_key){

    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(storage_name, NVS_READWRITE, &my_handle);
    if (err != ESP_OK){
//    	return err;
    	printf("nvs_open fail\n");
    	return err;
    }
    err = nvs_erase_key(my_handle,nvs_key);
    if (err != ESP_OK){
//    	return err;
    	printf("erase_fail\n");
    	return err;
    }
    return err;
}


/* To get the restart count of device from NVS */
uint32_t get_restart_count(){
	uint32_t Restart_count = 0;
	get_what_saved_uint32(blaze_mem, restart_nvs_key,&Restart_count);
	return Restart_count;
}


/* To store the restart count in NVS */
uint32_t update_restart_count(uint8_t get_update_restart_count){
	uint32_t Restart_count = get_restart_count();
	if(get_update_restart_count){
		Restart_count++;
		save_run_time_uint32(blaze_mem, restart_nvs_key, Restart_count);
	}
	return Restart_count;
}

/* To get Stored SSDI */
void get_ssid(char *ssid_data){
    strcpy(ssid_data,ssid_config_global);
}

/* To get the RSSI of connected Access point */
int get_parent_rssi(void){
	wifi_ap_record_t ap_info = { 0 };
//	char buffer[50];
	int parent_rssi;
	esp_wifi_sta_get_ap_info(&ap_info);
	parent_rssi = ap_info.rssi;
	return parent_rssi;
}


/* To get the station mac address of device */
void get_mac(char *wifi_mac) {
	uint8_t mac[6] = { 0, };
	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	snprintf(wifi_mac, 13, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
			mac[3], mac[4], mac[5]);
}
