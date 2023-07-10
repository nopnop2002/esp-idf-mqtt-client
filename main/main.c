/*
	 MQTT client example using WEB Socket.
	 This example code is in the Public Domain (or CC0 licensed, at your option.)
	 Unless required by applicable law or agreed to in writing, this
	 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	 CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "esp_sntp.h"
#include "mdns.h"
#include "lwip/dns.h"

#include "websocket_server.h"

#include "mqtt.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#define sntp_setoperatingmode esp_sntp_setoperatingmode
#define sntp_setservername esp_sntp_setservername
#define sntp_init esp_sntp_init
#endif

static QueueHandle_t client_queue;
MessageBufferHandle_t xMessageBufferMain;
MessageBufferHandle_t xMessageBufferMqtt;

const static int client_queue_size = 10;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "main";

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

void wifi_init_sta(void)
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&event_handler,
		NULL,
		&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
		IP_EVENT_STA_GOT_IP,
		&event_handler,
		NULL,
		&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD,
			/* Setting a password implies station will connect to all security modes including WEP/WPA.
			 * However these modes are deprecated and not advisable to be used. Incase your Access point
			 * doesn't support WPA2, these mode can be enabled by commenting below line */
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				.capable = true,
				.required = false
			},
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,
		pdFALSE,
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	/* The event will not be processed after unregister */
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group);
}

void initialise_mdns(void)
{
	//initialize mDNS
	ESP_ERROR_CHECK( mdns_init() );
	//set mDNS hostname (required if you want to advertise services)
	ESP_ERROR_CHECK( mdns_hostname_set(CONFIG_MDNS_HOSTNAME) );
	ESP_LOGI(TAG, "mdns hostname set to: [%s]", CONFIG_MDNS_HOSTNAME);

#if 0
	//set default mDNS instance name
	ESP_ERROR_CHECK( mdns_instance_name_set("ESP32 with mDNS") );
#endif
}

void time_sync_notification_cb(struct timeval *tv)
{
	ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
	ESP_LOGI(TAG, "Initializing SNTP");
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	//sntp_setservername(0, "pool.ntp.org");
	ESP_LOGI(TAG, "Your NTP Server is %s", CONFIG_NTP_SERVER);
	sntp_setservername(0, CONFIG_NTP_SERVER);
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();
}

static esp_err_t obtain_time(void)
{
	initialize_sntp();
	// wait for time to be set
	int retry = 0;
	const int retry_count = 10;
	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}

	if (retry == retry_count) return ESP_FAIL;
	return ESP_OK;
}


// handles websocket events
void websocket_callback(uint8_t num,WEBSOCKET_TYPE_t type,char* msg,uint64_t len) {
	const static char* TAG = "websocket_callback";
	//int value;

	switch(type) {
		case WEBSOCKET_CONNECT:
			ESP_LOGI(TAG,"client %i connected!",num);
			break;
		case WEBSOCKET_DISCONNECT_EXTERNAL:
			ESP_LOGI(TAG,"client %i sent a disconnect message",num);
			break;
		case WEBSOCKET_DISCONNECT_INTERNAL:
			ESP_LOGI(TAG,"client %i was disconnected",num);
			break;
		case WEBSOCKET_DISCONNECT_ERROR:
			ESP_LOGI(TAG,"client %i was disconnected due to an error",num);
			break;
		case WEBSOCKET_TEXT:
			if(len) { // if the message length was greater than zero
				ESP_LOGI(TAG, "got message length %i: %s", (int)len, msg);
				size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, msg, len, portMAX_DELAY);
				if (xBytesSent != len) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			}
			break;
		case WEBSOCKET_BIN:
			ESP_LOGI(TAG,"client %i sent binary message of size %"PRIu32":\n%s",num,(uint32_t)len,msg);
			break;
		case WEBSOCKET_PING:
			ESP_LOGI(TAG,"client %i pinged us with message of size %"PRIu32":\n%s",num,(uint32_t)len,msg);
			break;
		case WEBSOCKET_PONG:
			ESP_LOGI(TAG,"client %i responded to the ping",num);
			break;
	}
}

// serves any clients
static void http_serve(struct netconn *conn) {
	const static char* TAG = "http_server";
	const static char HTML_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/html\n\n";
	const static char ERROR_HEADER[] = "HTTP/1.1 404 Not Found\nContent-type: text/html\n\n";
	const static char JS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/javascript\n\n";
	const static char CSS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/css\n\n";
	//const static char PNG_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/png\n\n";
	const static char ICO_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/x-icon\n\n";
	//const static char PDF_HEADER[] = "HTTP/1.1 200 OK\nContent-type: application/pdf\n\n";
	//const static char EVENT_HEADER[] = "HTTP/1.1 200 OK\nContent-Type: text/event-stream\nCache-Control: no-cache\nretry: 3000\n\n";
	struct netbuf* inbuf;
	static char* buf;
	static uint16_t buflen;
	static err_t err;

	// default page
	extern const uint8_t root_html_start[] asm("_binary_root_html_start");
	extern const uint8_t root_html_end[] asm("_binary_root_html_end");
	const uint32_t root_html_len = root_html_end - root_html_start;

	// main.js
	extern const uint8_t main_js_start[] asm("_binary_main_js_start");
	extern const uint8_t main_js_end[] asm("_binary_main_js_end");
	const uint32_t main_js_len = main_js_end - main_js_start;

	// main.css
	extern const uint8_t main_css_start[] asm("_binary_main_css_start");
	extern const uint8_t main_css_end[] asm("_binary_main_css_end");
	const uint32_t main_css_len = main_css_end - main_css_start;

	// favicon.ico
	extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
	extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");
	const uint32_t favicon_ico_len = favicon_ico_end - favicon_ico_start;

	// error page
	extern const uint8_t error_html_start[] asm("_binary_error_html_start");
	extern const uint8_t error_html_end[] asm("_binary_error_html_end");
	const uint32_t error_html_len = error_html_end - error_html_start;

	netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second
	ESP_LOGI(TAG,"reading from client...");
	err = netconn_recv(conn, &inbuf);
	ESP_LOGI(TAG,"read from client");
	if(err==ERR_OK) {
		netbuf_data(inbuf, (void**)&buf, &buflen);
		if(buf) {

			ESP_LOGD(TAG, "buf=[%s]", buf);
			// default page
			if (strstr(buf,"GET / ")
					&& !strstr(buf,"Upgrade: websocket")) {
				ESP_LOGI(TAG,"Sending /");
				netconn_write(conn, HTML_HEADER, sizeof(HTML_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, root_html_start,root_html_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			// default page websocket
			else if(strstr(buf,"GET / ")
					&& strstr(buf,"Upgrade: websocket")) {
				ESP_LOGI(TAG,"Requesting websocket on /");
				ws_server_add_client(conn,buf,buflen,"/",websocket_callback);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /main.js ")) {
				ESP_LOGI(TAG,"Sending /main.js");
				netconn_write(conn, JS_HEADER, sizeof(JS_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, main_js_start, main_js_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /main.css ")) {
				ESP_LOGI(TAG,"Sending /main.css");
				netconn_write(conn, CSS_HEADER, sizeof(CSS_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, main_css_start, main_css_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"GET /favicon.ico ")) {
				ESP_LOGI(TAG,"Sending favicon.ico");
				netconn_write(conn,ICO_HEADER,sizeof(ICO_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn,favicon_ico_start,favicon_ico_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else if(strstr(buf,"POST /post ")) {
				ESP_LOGI(TAG,"Sending post");
#if 0
				netconn_write(conn, HTML_HEADER, sizeof(HTML_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, root_html_start,root_html_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
#endif
			}

			else if(strstr(buf,"GET /")) {
				ESP_LOGE(TAG,"Unknown request, sending error page: %s",buf);
				netconn_write(conn, ERROR_HEADER, sizeof(ERROR_HEADER)-1,NETCONN_NOCOPY);
				netconn_write(conn, error_html_start, error_html_len,NETCONN_NOCOPY);
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}

			else {
				ESP_LOGE(TAG,"Unknown request");
				netconn_close(conn);
				netconn_delete(conn);
				netbuf_delete(inbuf);
			}
		}
		else {
			ESP_LOGI(TAG,"Unknown request (empty?...)");
			netconn_close(conn);
			netconn_delete(conn);
			netbuf_delete(inbuf);
		}
	}
	else { // if err==ERR_OK
		ESP_LOGI(TAG,"error on read, closing connection");
		netconn_close(conn);
		netconn_delete(conn);
		netbuf_delete(inbuf);
	}
}

// handles clients when they first connect. passes to a queue
static void server_task(void* pvParameters) {
	const static char* TAG = "server_task";
	char *task_parameter = (char *)pvParameters;
	ESP_LOGI(TAG, "Start task_parameter=%s", task_parameter);
	char url[64];
	sprintf(url, "http://%s", task_parameter);
	ESP_LOGI(TAG, "Starting server on %s", url);

	struct netconn *conn, *newconn;
	static err_t err;
	client_queue = xQueueCreate(client_queue_size,sizeof(struct netconn*));
	configASSERT( client_queue );

	conn = netconn_new(NETCONN_TCP);
	netconn_bind(conn,NULL,80);
	netconn_listen(conn);
	ESP_LOGI(TAG,"server listening");
	do {
		err = netconn_accept(conn, &newconn);
		ESP_LOGI(TAG,"new client");
		if(err == ERR_OK) {
			xQueueSendToBack(client_queue,&newconn,portMAX_DELAY);
			//http_serve(newconn);
		}
	} while(err == ERR_OK);
	netconn_close(conn);
	netconn_delete(conn);
	ESP_LOGE(TAG,"task ending, rebooting board");
	esp_restart();
}

// receives clients from queue, handles them
static void server_handle_task(void* pvParameters) {
	const static char* TAG = "server_handle_task";
	struct netconn* conn;
	ESP_LOGI(TAG,"task starting");
	for(;;) {
		xQueueReceive(client_queue,&conn,portMAX_DELAY);
		if(!conn) continue;
		http_serve(conn);
	}
	vTaskDelete(NULL);
}

/*
v1:ID/NAME
v2:id/name
v3:propaty
v4:value
*/
static int makeSendText(char* buf, char* v1, char* v2, char* v3, char* v4)
{
	char DEL = 0x04;
	sprintf(buf,"%s%c%s%c%s%c%s", v1, DEL, v2, DEL, v3, DEL, v4);
	ESP_LOGD(TAG, "buf=[%s]", buf);
	return strlen(buf);
}

static void time_task(void* pvParameters) {
	const static char* TAG = "time_task";
	ESP_LOGI(TAG,"starting task");

	for(;;) {
		time_t now;
		time(&now);
		now = now + (CONFIG_LOCAL_TIMEZONE*60*60);
		struct tm timeinfo;
		char strftime_buf[64];
		localtime_r(&now, &timeinfo);
		//strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
		strftime(strftime_buf, sizeof(strftime_buf), "%H:%M:%S", &timeinfo);
		ESP_LOGD(TAG, "The current time is: %s", strftime_buf);

		char out[64];
		int len = makeSendText(out, "ID", "datetime", "value", strftime_buf);
		int clients = ws_server_send_text_all(out,len);
		if(clients > 0) {
			//ESP_LOGI(TAG,"sent: \"%s\" to %i clients",out,clients);
		}
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

esp_err_t query_mdns_host(const char * host_name, char *ip)
{
	ESP_LOGD(__FUNCTION__, "Query A: %s", host_name);

	struct esp_ip4_addr addr;
	addr.addr = 0;

	esp_err_t err = mdns_query_a(host_name, 10000,	&addr);
	if(err){
		if(err == ESP_ERR_NOT_FOUND){
			ESP_LOGW(__FUNCTION__, "%s: Host was not found!", esp_err_to_name(err));
			return ESP_FAIL;
		}
		ESP_LOGE(__FUNCTION__, "Query Failed: %s", esp_err_to_name(err));
		return ESP_FAIL;
	}

	ESP_LOGD(__FUNCTION__, "Query A: %s.local resolved to: " IPSTR, host_name, IP2STR(&addr));
	sprintf(ip, IPSTR, IP2STR(&addr));
	return ESP_OK;
}

void convert_mdns_host(char * from, char * to)
{
	ESP_LOGI(__FUNCTION__, "from=[%s]",from);
	strcpy(to, from);
	char *sp;
	sp = strstr(from, ".local");
	if (sp == NULL) return;

	int _len = sp - from;
	ESP_LOGD(__FUNCTION__, "_len=%d", _len);
	char _from[128];
	strcpy(_from, from);
	_from[_len] = 0;
	ESP_LOGI(__FUNCTION__, "_from=[%s]", _from);

	char _ip[128];
	esp_err_t ret = query_mdns_host(_from, _ip);
	ESP_LOGI(__FUNCTION__, "query_mdns_host=%d _ip=[%s]", ret, _ip);
	if (ret != ESP_OK) return;

	strcpy(to, _ip);
	ESP_LOGI(__FUNCTION__, "to=[%s]", to);
}

void mqtt(void *pvParameters);

void app_main() {
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// Start WiFi
	wifi_init_sta();

	// Initialize mDNS
	initialise_mdns();

	// Get current time
	ret = obtain_time();
	if(ret != ESP_OK) {
		ESP_LOGE(TAG, "Fail to getting time over NTP.");
		while(1) {
			vTaskDelay(1);
		}
	}

#if 0
	// update 'now' variable with current time
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	now = now + (CONFIG_LOCAL_TIMEZONE*60*60);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
#endif

	xMessageBufferMain = xMessageBufferCreate(1024);
	xMessageBufferMqtt = xMessageBufferCreate(1024);
	configASSERT( xMessageBufferMain );
	configASSERT( xMessageBufferMqtt );

	/* Get the local IP address */
	esp_netif_ip_info_t ip_info;
	ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info));
	char cparam0[64];
	sprintf(cparam0, IPSTR, IP2STR(&ip_info.ip));

	ws_server_start();
	xTaskCreate(&server_task, "server_task", 1024*2, (void *)cparam0, 9, NULL);
	xTaskCreate(&server_handle_task, "server_handle_task", 1024*3, NULL, 6, NULL);
	xTaskCreate(&time_task, "time_task", 1024*2, NULL, 2, NULL);
	xTaskCreate(mqtt, "mqtt_task", 1024*4, NULL, 2, NULL);

	char cRxBuffer[512];

	while(1) {
		size_t readBytes = xMessageBufferReceive(xMessageBufferMain, cRxBuffer, sizeof(cRxBuffer), portMAX_DELAY );
		ESP_LOGI(TAG, "readBytes=%d", readBytes);
		cJSON *root = cJSON_Parse(cRxBuffer);
		if (cJSON_GetObjectItem(root, "id")) {
			char *id = cJSON_GetObjectItem(root,"id")->valuestring;
			ESP_LOGI(TAG, "id=%s",id);

			if ( strcmp (id, "init") == 0) {
				size_t sentBytes = xMessageBufferSend(xMessageBufferMqtt, cRxBuffer, readBytes, portMAX_DELAY);
				if (sentBytes != readBytes) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			} // end of init

			if ( strcmp (id, "connect-request") == 0) {
				size_t sentBytes = xMessageBufferSend(xMessageBufferMqtt, cRxBuffer, readBytes, portMAX_DELAY);
				if (sentBytes != readBytes) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			} // end of connect-request

			if ( strcmp (id, "connect-response") == 0) {
				char *result = cJSON_GetObjectItem(root,"result")->valuestring;
				ESP_LOGI(TAG, "result=%s",result);
				if (strcmp(result, "OK") == 0) {
					char out[64];
					int len;
					len = makeSendText(out, "ID", "connectBtn", "value", "Connected");
					ws_server_send_text_all(out,len);
				}
			} // end of connect-response

			if ( strcmp (id, "disconnect-request") == 0) {
				size_t sentBytes = xMessageBufferSend(xMessageBufferMqtt, cRxBuffer, readBytes, portMAX_DELAY);
				if (sentBytes != readBytes) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			} // end of disconnect-request

			if ( strcmp (id, "disconnect-response") == 0) {
				char *result = cJSON_GetObjectItem(root,"result")->valuestring;
				ESP_LOGI(TAG, "result=%s",result);
				if (strcmp(result, "OK") == 0) {
					char out[64];
					int len;
					len = makeSendText(out, "ID", "connectBtn", "value", "Connect");
					ws_server_send_text_all(out,len);
				}
			} // end of connect-response

			if ( strcmp (id, "subscribe-request") == 0) {
				size_t sentBytes = xMessageBufferSend(xMessageBufferMqtt, cRxBuffer, readBytes, portMAX_DELAY);
				if (sentBytes != readBytes) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			} // end of subscribe-request

			if ( strcmp (id, "unsubscribe-request") == 0) {
				size_t sentBytes = xMessageBufferSend(xMessageBufferMqtt, cRxBuffer, readBytes, portMAX_DELAY);
				if (sentBytes != readBytes) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			} // end of unsubscribe-request

			if ( strcmp (id, "publish-request") == 0) {
				size_t sentBytes = xMessageBufferSend(xMessageBufferMqtt, cRxBuffer, readBytes, portMAX_DELAY);
				if (sentBytes != readBytes) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
			} // end of publish-request

			if ( strcmp (id, "subscribe-data") == 0) {
				char *topic = cJSON_GetObjectItem(root,"topic")->valuestring;
				char *payload = cJSON_GetObjectItem(root,"payload")->valuestring;
				ESP_LOGI(TAG, "topic=[%s] payload=[%s]",topic, payload);
				char out[128];
				int len;
				len = makeSendText(out, "MQTT", topic, payload, "");
				ws_server_send_text_all(out,len);
			} // end of subscribe-data
		} // end if

		// Delete a cJSON structure
		cJSON_Delete(root);

	} // end while

}
