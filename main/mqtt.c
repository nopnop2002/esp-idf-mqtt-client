/* MQTT (over TCP) Example

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"

#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include "mqtt.h"

static const char *TAG = "MQTT";

/* FreeRTOS event group to signal when we are connected*/
EventGroupHandle_t status_event_group;

/* - are we connected to the MQTT Server? */
int MQTT_CONNECTED_BIT = BIT2;
int MQTT_DISCONNECTED_BIT = BIT4;
int MQTT_ERROR_BIT = BIT6;

extern MessageBufferHandle_t xMessageBufferMain;
extern MessageBufferHandle_t xMessageBufferMqtt;

static void log_error_if_nonzero(const char *message, int error_code)
{
	if (error_code != 0) {
		ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
	}
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
#else
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
#endif
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
	esp_mqtt_event_handle_t event = event_data;
#endif
	switch (event->event_id) {
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
			xEventGroupSetBits(status_event_group, MQTT_CONNECTED_BIT);
			xEventGroupClearBits(status_event_group, MQTT_DISCONNECTED_BIT);
			break;
		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
			xEventGroupClearBits(status_event_group, MQTT_CONNECTED_BIT);
			xEventGroupSetBits(status_event_group, MQTT_DISCONNECTED_BIT);
			break;
		case MQTT_EVENT_SUBSCRIBED:
			ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_UNSUBSCRIBED:
			ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_PUBLISHED:
			ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_DATA:
			ESP_LOGI(TAG, "MQTT_EVENT_DATA");
			ESP_LOGI(TAG, "TOPIC=[%.*s] DATA=[%.*s]\r", event->topic_len, event->topic, event->data_len, event->data);

			char * topic;
			topic = malloc(event->topic_len+1);
			memset(topic, 0, event->topic_len+1);
			memcpy(topic, event->topic, event->topic_len);
			char * payload;
			payload = malloc(event->data_len+1);
			memset(payload, 0, event->data_len+1);
			memcpy(payload, event->data, event->data_len);

			cJSON *response;
			response = cJSON_CreateObject();
			cJSON_AddStringToObject(response, "id", "subscribe-data");
			cJSON_AddStringToObject(response, "topic", topic);
			cJSON_AddStringToObject(response, "payload", payload);
			char *my_json_string = cJSON_Print(response);
			ESP_LOGI(TAG, "my_json_string\n%s",my_json_string);
			size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, my_json_string, strlen(my_json_string), portMAX_DELAY);
			if (xBytesSent != strlen(my_json_string)) {
				ESP_LOGE(TAG, "xMessageBufferSend fail");
			}
			cJSON_Delete(response);
			cJSON_free(my_json_string);

			free(topic);
			free(payload);
			break;
		case MQTT_EVENT_ERROR:
			ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
			if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
				log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
				log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
				log_error_if_nonzero("captured as transport's socket errno",	event->error_handle->esp_transport_sock_errno);
				ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
			}
			xEventGroupSetBits(status_event_group, MQTT_ERROR_BIT);
			break;
		default:
			ESP_LOGI(TAG, "Other event id:%d", event->event_id);
			break;
	}
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
	return ESP_OK;
#endif
}

char *JSON_Types(int type) {
	if (type == cJSON_Invalid) return ("cJSON_Invalid");
	if (type == cJSON_False) return ("cJSON_False");
	if (type == cJSON_True) return ("cJSON_True");
	if (type == cJSON_NULL) return ("cJSON_NULL");
	if (type == cJSON_Number) return ("cJSON_Number");
	if (type == cJSON_String) return ("cJSON_String");
	if (type == cJSON_Array) return ("cJSON_Array");
	if (type == cJSON_Object) return ("cJSON_Object");
	if (type == cJSON_Raw) return ("cJSON_Raw");
	return NULL;
}

int array2text(const cJSON * const item, int index, char * buf) {
	if (cJSON_IsArray(item)) {
		cJSON *current_element = NULL;
		int itemNumber = 0;
		cJSON_ArrayForEach(current_element, item) {
			ESP_LOGD(TAG, "current_element->type=%s", JSON_Types(current_element->type));
			if (cJSON_IsNumber(current_element)) {
				int valueint = current_element->valueint;
				double valuedouble = current_element->valuedouble;
				ESP_LOGD(TAG, "valueint[%d]=%d valuedouble[%d]=%f",itemNumber, valueint, itemNumber, valuedouble);
			}
			if (cJSON_IsString(current_element)) {
				const char* string = current_element->valuestring;
				ESP_LOGD(TAG, "string[%d]=%s",itemNumber, string);
				if (itemNumber == index) {
					strcpy(buf, string);
				}
			}
			itemNumber++;
		}
		return cJSON_GetArraySize(item);
	} else {
		return 0;
	}
}

void object2text(cJSON * request, TEXT_t *textBuf) {
	cJSON *array;
	array = cJSON_GetObjectItem(request,"host");
	array2text(array, 0, (char *)textBuf->host);
	array = cJSON_GetObjectItem(request,"port");
	array2text(array, 0, (char *)textBuf->port);
	array = cJSON_GetObjectItem(request,"clientId");
	array2text(array, 0, (char *)textBuf->clientId);
	array = cJSON_GetObjectItem(request,"username");
	array2text(array, 0, (char *)textBuf->username);
	array = cJSON_GetObjectItem(request,"password");
	array2text(array, 0, (char *)textBuf->password);
	array = cJSON_GetObjectItem(request,"topic");
	array2text(array, 0, (char *)textBuf->topicSub);
	array2text(array, 1, (char *)textBuf->topicPub);
	array = cJSON_GetObjectItem(request,"qos");
	array2text(array, 0, (char *)textBuf->qosSub);
	array2text(array, 1, (char *)textBuf->qosPub);
	array = cJSON_GetObjectItem(request,"payload");
	array2text(array, 0, (char *)textBuf->payload);

	ESP_LOGD(TAG, "textBuf.host=[%s]", textBuf->host);
	ESP_LOGD(TAG, "textBuf.port=[%s]", textBuf->port);
	ESP_LOGD(TAG, "textBuf.clientId=[%s]", textBuf->clientId);
	ESP_LOGD(TAG, "textBuf.username=[%s]", textBuf->username);
	ESP_LOGD(TAG, "textBuf.password=[%s]", textBuf->password);
	ESP_LOGD(TAG, "textBuf.topicSub=[%s]", textBuf->topicSub);
	ESP_LOGD(TAG, "textBuf.topicPub=[%s]", textBuf->topicPub);
	ESP_LOGD(TAG, "textBuf.qosSub=[%s]", textBuf->qosSub);
	ESP_LOGD(TAG, "textBuf.qosPub=[%s]", textBuf->qosPub);
	ESP_LOGD(TAG, "textBuf.payload=[%s]", textBuf->payload);
}

esp_err_t query_mdns_host(const char * host_name, char *ip);
void convert_mdns_host(char * from, char * to);

void mqtt(void *pvParameters)
{
	ESP_LOGI(TAG, "Start MQTT");
	status_event_group = xEventGroupCreate();

	char cRxBuffer[512];
	TEXT_t textBuf;
	bool connected = false;
	esp_mqtt_client_handle_t mqtt_client = NULL;

	while(1) {
		size_t readBytes = xMessageBufferReceive(xMessageBufferMqtt, cRxBuffer, sizeof(cRxBuffer), portMAX_DELAY );
		ESP_LOGI(TAG, "readBytes=%d", readBytes);
		cJSON *request = cJSON_Parse(cRxBuffer);
		if (cJSON_GetObjectItem(request, "id")) {
			char *id = cJSON_GetObjectItem(request,"id")->valuestring;
			ESP_LOGI(TAG, "id=%s",id);

			if ( strcmp (id, "init") == 0) {
				ESP_LOGI(TAG, "init connected=%d", connected);
				if (connected == false) continue;
				//esp_mqtt_client_stop(mqtt_client);
				esp_mqtt_client_disconnect(mqtt_client);
				EventBits_t uxBits = xEventGroupWaitBits(status_event_group, MQTT_DISCONNECTED_BIT | MQTT_ERROR_BIT, true, false, portMAX_DELAY);
				ESP_LOGI(TAG, "xEventGroupWaitBits uxBits=0x%"PRIx32, uxBits);
				if( ( uxBits & MQTT_DISCONNECTED_BIT ) != 0 ) {
					ESP_LOGI(TAG, "Disconnect Success");
					esp_mqtt_client_stop(mqtt_client);
					connected = false;
				} else if( ( uxBits & MQTT_ERROR_BIT ) != 0 ) {
					ESP_LOGW(TAG, "Disconnect Fail");
				}
			} // end of init

			if ( strcmp (id, "connect-request") == 0) {
				ESP_LOGI(TAG, "connect-request connected=%d", connected);
				if (connected == true) continue;
				object2text(request, &textBuf);

#if 0
				cJSON *array;
				array = cJSON_GetObjectItem(request,"host");
				array2text(array, 0, (char *)textBuf.host);
				array = cJSON_GetObjectItem(request,"port");
				array2text(array, 0, (char *)textBuf.port);
				array = cJSON_GetObjectItem(request,"clientId");
				array2text(array, 0, (char *)textBuf.clientId);
				array = cJSON_GetObjectItem(request,"username");
				array2text(array, 0, (char *)textBuf.username);
				array = cJSON_GetObjectItem(request,"password");
				array2text(array, 0, (char *)textBuf.password);
				array = cJSON_GetObjectItem(request,"topic");
				array2text(array, 0, (char *)textBuf.topicSub);
				array2text(array, 1, (char *)textBuf.topicPub);
				array = cJSON_GetObjectItem(request,"qos");
				array2text(array, 0, (char *)textBuf.qosSub);
				array2text(array, 1, (char *)textBuf.qosPub);
				array = cJSON_GetObjectItem(request,"payload");
				array2text(array, 0, (char *)textBuf.payload);

				ESP_LOGI(TAG, "textBuf.host=[%s]", textBuf.host);
				ESP_LOGI(TAG, "textBuf.port=[%s]", textBuf.port);
				ESP_LOGI(TAG, "textBuf.clientId=[%s]", textBuf.clientId);
				ESP_LOGI(TAG, "textBuf.username=[%s]", textBuf.username);
				ESP_LOGI(TAG, "textBuf.password=[%s]", textBuf.password);
				ESP_LOGI(TAG, "textBuf.topicSub=[%s]", textBuf.topicSub);
				ESP_LOGI(TAG, "textBuf.topicPub=[%s]", textBuf.topicPub);
				ESP_LOGI(TAG, "textBuf.qosSub=[%s]", textBuf.qosSub);
				ESP_LOGI(TAG, "textBuf.qosPub=[%s]", textBuf.qosPub);
				ESP_LOGI(TAG, "textBuf.payload=[%s]", textBuf.payload);
#endif

				// Resolve mDNS host name
				char ip[128];
				ESP_LOGI(TAG, "textBuf.host=[%s]", textBuf.host);
				convert_mdns_host(textBuf.host, ip);
				ESP_LOGI(TAG, "ip=[%s]", ip);
				char uri[138];
				sprintf(uri, "mqtt://%s", ip);
				//sprintf(uri,"mqtt://%s", textBuf.host);
				uint32_t port = strtol( textBuf.port, NULL, 10 );
				ESP_LOGI(TAG, "uri=[%s] port=%"PRIu32, uri, port);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
				esp_mqtt_client_config_t mqtt_cfg = {
					.broker.address.uri = uri,
					.broker.address.port = port,
					.credentials.client_id = textBuf.clientId,
				};
				if (strlen(textBuf.username) > 0) {
					mqtt_cfg.credentials.username = textBuf.username;
				}
				if (strlen(textBuf.password) > 0) {
					mqtt_cfg.credentials.authentication.password = textBuf.password;
				}
#else
				esp_mqtt_client_config_t mqtt_cfg = {
					.uri = uri,
					.port = port,
					.client_id = textBuf.clientId,
					.event_handle = mqtt_event_handler
				};
				if (strlen(textBuf.username) > 0) {
					mqtt_cfg.username = textBuf.username;
				}
				if (strlen(textBuf.password) > 0) {
					mqtt_cfg.password = textBuf.password;
				}
#endif

				mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
				esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
#endif

				esp_mqtt_client_start(mqtt_client);
				EventBits_t uxBits = xEventGroupWaitBits(status_event_group, MQTT_CONNECTED_BIT | MQTT_ERROR_BIT, true, false, portMAX_DELAY);
				ESP_LOGI(TAG, "xEventGroupWaitBits uxBits=0x%"PRIx32, uxBits);

				cJSON *response;
				response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "id", "connect-response");
				if( ( uxBits & MQTT_CONNECTED_BIT ) != 0 ) {
					ESP_LOGI(TAG, "Connect Success");
					connected = true;
					cJSON_AddStringToObject(response, "result", "OK");
				} else if( ( uxBits & MQTT_ERROR_BIT ) != 0 ) {
					ESP_LOGW(TAG, "Connect Fail");
					esp_mqtt_client_stop(mqtt_client);
					cJSON_AddStringToObject(response, "result", "NG");
				}
				char *my_json_string = cJSON_Print(response);
				ESP_LOGI(TAG, "my_json_string\n%s",my_json_string);
				size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, my_json_string, strlen(my_json_string), portMAX_DELAY);
				if (xBytesSent != strlen(my_json_string)) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
				cJSON_Delete(response);
				cJSON_free(my_json_string);
			} // end of connect-request

			if ( strcmp (id, "disconnect-request") == 0) {
				ESP_LOGI(TAG, "disconnect-request connected=%d", connected);
				if (connected == false) continue;
				//esp_mqtt_client_stop(mqtt_client);
				esp_mqtt_client_disconnect(mqtt_client);
				EventBits_t uxBits = xEventGroupWaitBits(status_event_group, MQTT_DISCONNECTED_BIT | MQTT_ERROR_BIT, true, false, portMAX_DELAY);
				ESP_LOGI(TAG, "xEventGroupWaitBits uxBits=0x%"PRIx32, uxBits);

				cJSON *response;
				response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "id", "disconnect-response");
				if( ( uxBits & MQTT_DISCONNECTED_BIT ) != 0 ) {
					ESP_LOGI(TAG, "Disconnect Success");
					esp_mqtt_client_stop(mqtt_client);
					connected = false;
					cJSON_AddStringToObject(response, "result", "OK");
				} else if( ( uxBits & MQTT_ERROR_BIT ) != 0 ) {
					ESP_LOGW(TAG, "Disconnect Fail");
					cJSON_AddStringToObject(response, "result", "NG");
				}
				char *my_json_string = cJSON_Print(response);
				ESP_LOGI(TAG, "my_json_string\n%s",my_json_string);
				size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, my_json_string, strlen(my_json_string), portMAX_DELAY);
				if (xBytesSent != strlen(my_json_string)) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
				cJSON_Delete(response);
				cJSON_free(my_json_string);
			} // end of disconnect-request

			if ( strcmp (id, "subscribe-request") == 0) {
				ESP_LOGI(TAG, "subscribe-request connected=%d", connected);
				if (connected == false) continue;
				object2text(request, &textBuf);
				int qos = strtol(textBuf.qosSub, NULL, 10);
				ESP_LOGI(TAG, "topic=%s qos=%d", textBuf.topicSub, qos);
				int msg_id = esp_mqtt_client_subscribe(mqtt_client, textBuf.topicSub, qos);
				ESP_LOGI(TAG, "esp_mqtt_client_subscribe msg_id=%d", msg_id);

				cJSON *response;
				response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "id", "subscribe-response");
				if (msg_id >= 0) {
					ESP_LOGI(TAG, "esp_mqtt_client_subscribe Success");
					cJSON_AddStringToObject(response, "result", "OK");
				} else {
					ESP_LOGW(TAG, "esp_mqtt_client_subscribe Fail");
					cJSON_AddStringToObject(response, "result", "NG");
				}
				char *my_json_string = cJSON_Print(response);
				ESP_LOGI(TAG, "my_json_string\n%s",my_json_string);
				size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, my_json_string, strlen(my_json_string), portMAX_DELAY);
				if (xBytesSent != strlen(my_json_string)) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
				cJSON_Delete(response);
				cJSON_free(my_json_string);
			} // end of subscribe-request

			if ( strcmp (id, "unsubscribe-request") == 0) {
				ESP_LOGI(TAG, "unsubscribe-request connected=%d", connected);
				if (connected == false) continue;
				object2text(request, &textBuf);
				ESP_LOGI(TAG, "topic=%s", textBuf.topicSub);
				int msg_id = esp_mqtt_client_unsubscribe(mqtt_client, textBuf.topicSub);
				ESP_LOGI(TAG, "esp_mqtt_client_unsubscribe msg_id=%d", msg_id);

				cJSON *response;
				response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "id", "unsubscribe-response");
				if (msg_id >= 0) {
					ESP_LOGI(TAG, "esp_mqtt_client_unsubscribe Success");
					cJSON_AddStringToObject(response, "result", "OK");
				} else {
					ESP_LOGW(TAG, "esp_mqtt_client_unsubscribe Fail");
					cJSON_AddStringToObject(response, "result", "NG");
				}
				char *my_json_string = cJSON_Print(response);
				ESP_LOGI(TAG, "my_json_string\n%s",my_json_string);
				size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, my_json_string, strlen(my_json_string), portMAX_DELAY);
				if (xBytesSent != strlen(my_json_string)) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
				cJSON_Delete(response);
				cJSON_free(my_json_string);
			} // end of unsubscribe-request

			if ( strcmp (id, "publish-request") == 0) {
				ESP_LOGI(TAG, "publish-request connected=%d", connected);
				if (connected == false) continue;
				object2text(request, &textBuf);
				int qos = strtol(textBuf.qosPub, NULL, 10);
				ESP_LOGI(TAG, "topic=%s qos=%d payload=%s", textBuf.topicPub, qos, textBuf.payload);
				int msg_id = esp_mqtt_client_publish(mqtt_client, textBuf.topicPub, textBuf.payload, 0, qos, 0);
				ESP_LOGI(TAG, "esp_mqtt_client_publish msg_id=%d", msg_id);

				cJSON *response;
				response = cJSON_CreateObject();
				cJSON_AddStringToObject(response, "id", "publish-response");
				if (msg_id >= 0) {
					ESP_LOGI(TAG, "esp_mqtt_client_publish Success");
					cJSON_AddStringToObject(response, "result", "OK");
				} else {
					ESP_LOGW(TAG, "esp_mqtt_client_publish Fail");
					cJSON_AddStringToObject(response, "result", "NG");
				}
				char *my_json_string = cJSON_Print(response);
				ESP_LOGI(TAG, "my_json_string\n%s",my_json_string);
				size_t xBytesSent = xMessageBufferSend(xMessageBufferMain, my_json_string, strlen(my_json_string), portMAX_DELAY);
				if (xBytesSent != strlen(my_json_string)) {
					ESP_LOGE(TAG, "xMessageBufferSend fail");
				}
				cJSON_Delete(response);
				cJSON_free(my_json_string);
			} // end of publish-request

		} // end of request
		cJSON_Delete(request);

	} // end of while

}
