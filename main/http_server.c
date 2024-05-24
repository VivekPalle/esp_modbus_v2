/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include "mdns.h"
#include "esp_mac.h"
#include "cJSON.h"
//#include "esp_modbus_slave.h"
#include "generic_api.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"


cJSON *root = NULL;
httpd_handle_t server = NULL;
static esp_err_t echo_post_handler(httpd_req_t *req);
static esp_err_t ota_post_handler(httpd_req_t *req);
//esp_err_t mbc_slave_destroy(void);
void slave_destroy(void);
void ota_init(void);
void modbus_init(void);

#define FLIE_SIZE 27000
EXT_RAM_BSS_ATTR char buf1[FLIE_SIZE] = {'\0',};
EXT_RAM_BSS_ATTR char buf2[FLIE_SIZE] = {'\0',};
EXT_RAM_BSS_ATTR char modbus_write[10000] = {'\0',};
int buf_cnt = 0;
extern uint8_t md_flag;

char ssid_config_global[35] = {0,};
char pass_config_global[65] = {0,};
extern char firmware_version[20];
/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

int modbus_write_count = 0;
char mac_address[13] = {0,};

int index_pos = 0;

bool json_val_flag =0;
extern char data_count;
extern uint8_t mb_destroy;
extern uint8_t MAX_ADDRESSES,MAX_SIZE_ARRAY;
extern uint16_t MAX_DATA_SIZE;
static const char *TAG = "example";


extern uint16_t md_addr[40],addrCnt;
extern uint16_t md_size[40],sizeCnt,dataCnt[40];
extern uint8_t md_hold_data[40][500];
//extern uint8_t hold_data2[100];

/* To store the JSON data in NVS */
void store_file_data(void){
	save_run_time_str(blaze_mem, "JSON_DATA", buf1);
}


/* To retrieve JSON data from NVS */
void retrieve_file(void){
	get_what_saved_str(blaze_mem, "JSON_DATA",buf2);
	printf("\nStored data = %s\n\n", buf2);
}


/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */
#if CONFIG_EXAMPLE_BASIC_AUTH

typedef struct {
    char    *username;
    char    *password;
} basic_auth_info_t;

#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

static char *http_auth_basic(const char *username, const char *password)
{
    int out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    asprintf(&user_info, "%s:%s", username, password);
    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
    */
    digest = calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

/* An HTTP GET handler */
static esp_err_t basic_auth_get_handler(httpd_req_t *req)
{
    char *buf = NULL;
    size_t buf_len = 0;
    basic_auth_info_t *basic_auth_info = req->user_ctx;

    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            return ESP_ERR_NO_MEM;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Authorization: %s", buf);
        } else {
            ESP_LOGE(TAG, "No auth value received");
        }

        char *auth_credentials = http_auth_basic(basic_auth_info->username, basic_auth_info->password);
        if (!auth_credentials) {
            ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
            free(buf);
            return ESP_ERR_NO_MEM;
        }

        if (strncmp(auth_credentials, buf, buf_len)) {
            ESP_LOGE(TAG, "Not authenticated");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
            httpd_resp_send(req, NULL, 0);
        } else {
            ESP_LOGI(TAG, "Authenticated!");
            char *basic_auth_resp = NULL;
            httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            asprintf(&basic_auth_resp, "{\"authenticated\": true,\"user\": \"%s\"}", basic_auth_info->username);
            if (!basic_auth_resp) {
                ESP_LOGE(TAG, "No enough memory for basic authorization response");
                free(auth_credentials);
                free(buf);
                return ESP_ERR_NO_MEM;
            }
            httpd_resp_send(req, basic_auth_resp, strlen(basic_auth_resp));
            free(basic_auth_resp);
        }
        free(auth_credentials);
        free(buf);
    } else {
        ESP_LOGE(TAG, "No auth header received");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
        httpd_resp_send(req, NULL, 0);
    }

    return ESP_OK;
}

static httpd_uri_t basic_auth = {
    .uri       = "/basic_auth",
    .method    = HTTP_GET,
    .handler   = basic_auth_get_handler,
};

static void httpd_register_basic_auth(httpd_handle_t server)
{
    basic_auth_info_t *basic_auth_info = calloc(1, sizeof(basic_auth_info_t));
    if (basic_auth_info) {
        basic_auth_info->username = CONFIG_EXAMPLE_BASIC_AUTH_USERNAME;
        basic_auth_info->password = CONFIG_EXAMPLE_BASIC_AUTH_PASSWORD;

        basic_auth.user_ctx = basic_auth_info;
        httpd_register_uri_handler(server, &basic_auth);
    }
}
#endif


/* This API will return the device details in JSON format */
char *get_details(void){
	printf("get detail free heap size: %ld bytes\n", esp_get_free_heap_size());
//	char time_buff[64] = {0,};
     int rssi = get_parent_rssi();
     uint32_t count = get_restart_count();
     printf("\nRSSI = %d\n", rssi);
     printf("\nSSID = %s\n", ssid_config_global);
     char mac[13] = {0,};
     get_mac(mac);
     printf("\nWi-Fi MAC ID = %s\n", mac);
//     get_latest_time(time_buff);
//     printf("\n Current time = %s\n", time_buff);


     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "Mac_ID", mac);
     cJSON_AddStringToObject(root, "SSID", ssid_config_global);
     cJSON_AddNumberToObject(root, "rssi", rssi);
     cJSON_AddNumberToObject(root, "Re-count", count);
     cJSON_AddNumberToObject(root, "Count", modbus_write_count);
     cJSON_AddStringToObject(root, "Fw_ver", firmware_version);

     char *str = cJSON_Print(root);
     cJSON_Delete(root);
     return(str);
}



/* An HTTP GET handler for checking device online status */
static esp_err_t health_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*)req->user_ctx;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);


    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}


/* An HTTP GET handler for sending device details when request made by client */
static esp_err_t details_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }
    /* Send response with custom headers and body set as the
     * string passed in user context*/
    char *data = get_details();
    printf("\nJSON data 2 = \n%s\n", data);

    printf("\n after get details free heap size: %ld bytes\n", esp_get_free_heap_size());
    const char* resp_str = (const char*)data;

    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(data);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}


/* An HTTP GET handler for sending received JSON file */
static esp_err_t file_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    retrieve_file();
    printf("\n after get data request free heap size: %ld bytes\n", esp_get_free_heap_size());

    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, buf2, HTTPD_RESP_USE_STRLEN);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}


static esp_err_t modbus_get_handler(httpd_req_t *req)
{
	char *buf;
	size_t buf_len;

	/* Get header value string length and allocate memory for length + 1,
	 * extra byte for null termination */
	buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
	if (buf_len > 1) {
		buf = malloc(buf_len);
		/* Copy null terminated value string into buffer */
		if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
			ESP_LOGI(TAG, "Found header => Host: %s", buf);
		}
		free(buf);
	}

//	char *data = get_details();
//	printf("\nJSON data 2 = \n%s\n", data);
//
//	printf("\n after get details free heap size: %ld bytes\n",
//			esp_get_free_heap_size());
	//    const char* resp_str = (const char*)req->user_ctx;// default
	if (modbus_write_count != 0) {
		modbus_write_count = 0;
		const char *resp_str = (const char*) modbus_write;
		httpd_resp_set_status(req, HTTPD_200);
		httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
	}
	else{
		const char *resp_str = "NULL";
		httpd_resp_set_status(req, HTTPD_200);
		httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
	}

//	free(data);

	/* After sending the HTTP response the old HTTP request
	 * headers are lost. Check if HTTP request headers can be read now. */
	if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
		ESP_LOGI(TAG, "Request headers lost");
	}
	return ESP_OK;
}

static const httpd_uri_t health = {
    .uri       = "/health",
    .method    = HTTP_GET,
    .handler   = health_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "OK"
};

static const httpd_uri_t modbus_write_event = {
    .uri       = "/get_event",
    .method    = HTTP_GET,
    .handler   = modbus_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "OK"
};
static const httpd_uri_t get_data = {
    .uri       = "/get_data",
    .method    = HTTP_GET,
    .handler   = file_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "OK"
};

static const httpd_uri_t details = {
    .uri       = "/details",
    .method    = HTTP_GET,
    .handler   = details_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};

static const httpd_uri_t files = {
    .uri       = "/set_data",
    .method    = HTTP_POST,
    .handler   = echo_post_handler,
    .user_ctx  = "OK"
};

static const httpd_uri_t ota = {
    .uri       = "/set_ota",
    .method    = HTTP_POST,
    .handler   = ota_post_handler,
    .user_ctx  = "OK"
};


/* API will convert the File data to JSON format and updates the modbus registers */
void parse_modbus_json(char flag) {
	size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	printf("Free PSRAM memory 1: %d bytes\n", free_psram);
	int cnt = 0, pos = 0;
	// Parse JSON string
	if (flag == 0) {
		root = cJSON_Parse(buf2);
	} else if (flag == 1) {
		root = cJSON_Parse(buf1);
	}
	if (root == NULL) {
		printf("Error parsing JSON: %s\n", cJSON_GetErrorPtr());
		size_t psram_size = esp_psram_get_size();
		printf("\n\nPSRAM size 2: %d bytes\n", psram_size);
		return;
	}
	printf("\nJSON parsing success\n\n");

// Extract md_addr_info array
	cJSON *addr_info = cJSON_GetObjectItem(root, "md_addr_info");
	if (addr_info == NULL || !cJSON_IsArray(addr_info)) {
		printf("Error parsing md_addr_info\n");
		cJSON_Delete(root);
		return;
	}

	cJSON *addr_item;
	cJSON_ArrayForEach(addr_item, addr_info)
	{
		if (cnt < MAX_ADDRESSES)
			md_addr[cnt++] = addr_item->valueint;
	}
	addrCnt = cnt;

// Extract md_size_info array
	cJSON *size_info = cJSON_GetObjectItem(root, "md_size_info");
	if (size_info == NULL || !cJSON_IsArray(size_info)) {
		printf("Error parsing md_size_info\n");
		cJSON_Delete(root);
		return;
	}

	// Print md_size_info array
	cnt = 0;
	cJSON *size_item;
	cJSON_ArrayForEach(size_item, size_info)
	{
		if (cnt < MAX_SIZE_ARRAY)
			md_size[cnt++] = size_item->valueint;
	}
	sizeCnt = cnt;

// Extract md_data array
	cJSON *data = cJSON_GetObjectItem(root, "md_data");
	if (data == NULL || !cJSON_IsArray(data)) {
		printf("Error parsing md_data\n");
		cJSON_Delete(root);
		return;
	}

	cnt = 0;
	cJSON *data_item;
	cJSON_ArrayForEach(data_item, data)
	{
		if (pos < MAX_ADDRESSES) {
			cJSON *inner_array;
			cJSON_ArrayForEach(inner_array, data_item)
			{
				if (cnt < MAX_DATA_SIZE)
					md_hold_data[pos][cnt++] = inner_array->valueint;
			}
			dataCnt[pos] = cnt;
		}
		pos++;
		cnt = 0;
	}

	cJSON_Delete(root);
	free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	printf("Free PSRAM memory 2: %d bytes\n", free_psram);
}


/* An HTTP POST handler for receiving files from desktop emulator */
static esp_err_t echo_post_handler(httpd_req_t *req)
{
	size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	printf("Free PSRAM memory: %d bytes\n", free_psram);

	char temp_buf[1024*4] = {0,};
	index_pos = 0,buf_cnt = 0;

	size_t content_length = req->content_len;
	ESP_LOGI(TAG, "[APP] Free memorys: %ld bytes", esp_get_free_heap_size());
	printf("\nContent length = %d\n", content_length);
	size_t received_size = 0;
	while (received_size < content_length) {
		int chunk_size = httpd_req_recv(req, temp_buf, sizeof(temp_buf));
		if (chunk_size <= 0) {
			// Handle error or end of data
			break;
		}
		received_size += chunk_size;

		for(int i = 0; i < chunk_size; i++){
			buf1[index_pos++] = temp_buf[i];
		}
	}

	 const char* resp_str = (const char*)req->user_ctx;
	 httpd_resp_set_status(req, HTTPD_200);
	 httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

	 printf("\nReceived data size = %d\n", received_size);
	 printf("\nbuf1 = %s\n", buf1);

	 store_file_data();
	 parse_modbus_json(1);

//	 ESP_ERROR_CHECK(mbc_slave_destroy());
	 slave_destroy();
	 modbus_init();

	 ESP_LOGI(TAG, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
	 return ESP_OK;
}


/* An HTTP POST handler to initiate OTA */
static esp_err_t ota_post_handler(httpd_req_t *req)
{
	size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	printf("Free PSRAM memory: %d bytes\n", free_psram);

	char temp_buf[1024*4] = {0,};

	size_t content_length = req->content_len;
	ESP_LOGI(TAG, "[APP] Free memorys: %ld bytes", esp_get_free_heap_size());
	printf("\nContent length = %d\n", content_length);
	size_t received_size = 0;
	while (received_size < content_length) {
		int chunk_size = httpd_req_recv(req, temp_buf, sizeof(temp_buf));
		if (chunk_size <= 0) {
			// Handle error or end of data
			break;
		}
	}
	 const char* resp_str = (const char*)req->user_ctx;
	 httpd_resp_set_status(req, HTTPD_200);
	 httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

	 printf("\nReceived data size = %d\n", received_size);
	 printf("\nReceived data = %s\n", temp_buf);

	 ESP_LOGI(TAG, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
	 ota_init();
	 return ESP_OK;
}



/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}


/* Initializes HTTP server */
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config .stack_size = 1024*10;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 2000;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &health);
        httpd_register_uri_handler(server, &get_data);
        httpd_register_uri_handler(server, &details);
        httpd_register_uri_handler(server, &modbus_write_event);
        httpd_register_uri_handler(server, &ota);
        esp_err_t er1 = httpd_register_uri_handler(server, &files);
        printf("\ne1 = %x\n", er1);
//        httpd_register_uri_handler(server, &ctrl);
        #if CONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(server);
        #endif
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

/* Stope HTTP server */
static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}


/* Station disconnect handler */
void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
            esp_err_t err = mdns_service_remove_all();
            mdns_free();
            if(err == 0){
            	printf("\nStopping mdns services\n");
            }
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}


/* API will be called after getting IP from Access point */
int connect_http()
{
	printf("\nCalling mdns\n");
	esp_log_level_set("mdns", ESP_LOG_DEBUG);  // Enable mDNS debugging
	init_mdns();
	if (server == NULL) {
		server = start_webserver();

		if (server != NULL) {
			printf("\nserver not null\n\n");
			register_mdns_service();
			return 0;
		}
		else
			return -1;
	}
	printf("mdns free heap size: %ld bytes\n", esp_get_free_heap_size());
	return -1;
}


void register_mdns_service() {

	printf("\nregister_mdns_service\n");
	esp_err_t err =  0;
	size_t size = 0;
		err = mdns_service_add(mac_address, "_http", "_tcp", 80, NULL, size);
		printf("service name non_mesh:%s", mac_address);

	if (err == ESP_OK) {
		printf("\n mdns_service_add success\n");
	} else {
		printf("\n mdns_service_add failure\n");
	}
}


void init_mdns(void) {
	esp_err_t err2;

//	mdns_result_t *result = NULL;
//	esp_err_t err = mdns_query_ptr("_wss._tcp.local", MDNS_QUERY_AXFR, 3000,  &result);
//	if (err == ESP_OK) {
//	    // Process the mDNS query result
//	    // The result may contain information about available services
//	    // ...
//	    mdns_query_results_free(result);
//	}

	err2 = mdns_init();
	if (err2 == ESP_OK) {
		printf("\n mdns_init success\n");
	} else {
		printf("\n mdns_init failure\n");
	}
	esp_err_t err, err1;
	;

	err = mdns_hostname_set(mac_address);
//	printf("host_name non_mesh:%s", mac_address);
//	err1 = mdns_instance_name_set(mac_address); // Set a human-readable instance name
//	printf("\nInstance name = %s\n", mac_address);
//	}
	if (err == ESP_OK) {
		printf("\nmdns_hostname_set success\n");
	} else {
		printf("\nmdns_hostname_set failure\n");
	}
	err1 = mdns_instance_name_set("BLAZE-SERVER"); // Set a human-readable instance name
	if (err1 == ESP_OK) {
		printf("\n mdns_instance_name_set success\n");
	} else {
		printf("\n mdns_instance_name_set failure\n");
	}
}


void http_init(void)
{
    uint8_t mac[6] = {0,};

	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	snprintf(mac_address, 13, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
	mac[2], mac[3], mac[4], mac[5]);

	printf("\nBT mac address of the device = %s\n", mac_address);
	int res_count = update_restart_count(1);
	printf("\nRestart count = %d\n", res_count);
	get_what_saved_str(blaze_mem, ssid_nvs_key, ssid_config_global);
	get_what_saved_str(blaze_mem, pass_nvs_key, pass_config_global);
	printf("\nSaved SSID = %s and Password = %s\n", ssid_config_global, pass_config_global);

	/* Start the server for the first time */
	connect_http();
}

void update_modbus_in_local(){

}














