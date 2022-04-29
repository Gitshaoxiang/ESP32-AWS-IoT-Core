/*
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Additions Copyright 2016 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
/**
 * @file main.c
 * @brief simple MQTT publish and subscribe for use with AWS IoT EduKit reference hardware.
 *
 * This example takes the parameters from the build configuration and establishes a connection to AWS IoT Core over MQTT.
 *
 * Some configuration is required. Visit https://edukit.workshop.aws
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "wifi.h"

#define PUBLISH_INTERVAL_MS 3000 


/* The time prefix used by the logger. */
static const char *TAG = "MAIN";

/* The FreeRTOS task handler for the blink task that can be used to control the task later */
TaskHandle_t xBlink;

/* CA Root certificate */
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");

extern const uint8_t aws_certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t aws_certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");

extern const uint8_t aws_private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t aws_private_pem_key_end[] asm("_binary_private_pem_key_end");


/* Default MQTT HOST URL is pulled from the aws_iot_config.h */
char *HostAddress = CONFIG_AWS_HOST_ADDRESS;
char *client_id = CONFIG_AWS_THING_NAME;
char *subscribe_topic = "esp32/down";
char *base_publish_topic = "esp32/up";


/* Default MQTT port is pulled from the aws_iot_config.h */
uint32_t port = AWS_IOT_MQTT_PORT;

void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData) {
    ESP_LOGI(TAG, "Subscribe callback");
    ESP_LOGI(TAG, "%.*s\t%.*s", topicNameLen, topicName, (int) params->payloadLen, (char *)params->payload);
    if (strstr(topicName, "/blink") != NULL) {
        // Get state of the FreeRTOS task, "blinkTask", using it's task handle.
        // Suspend or resume the task depending on the returned task state
        eTaskState blinkState = eTaskGetState(xBlink);
        if (blinkState == eSuspended){
            vTaskResume(xBlink);
        } else{
            vTaskSuspend(xBlink);
        }
    }
}

void disconnect_callback_handler(AWS_IoT_Client *pClient, void *data) {
    ESP_LOGW(TAG, "MQTT Disconnect");
    IoT_Error_t rc = FAILURE;

    if(pClient == NULL) {
        return;
    }

    if(aws_iot_is_autoreconnect_enabled(pClient)) {
        ESP_LOGI(TAG, "Auto Reconnect is enabled, Reconnecting attempt will start now");
    } else {
        ESP_LOGW(TAG, "Auto Reconnect not enabled. Starting manual reconnect...");
        rc = aws_iot_mqtt_attempt_reconnect(pClient);
        if(NETWORK_RECONNECTED == rc) {
            ESP_LOGW(TAG, "Manual Reconnect Successful");
        } else {
            ESP_LOGW(TAG, "Manual Reconnect Failed - %d", rc);
        }
    }
}

int count = 0;

static void publisher(AWS_IoT_Client *client, char *base_topic, uint16_t base_topic_len){
    char cPayload[100];
    int32_t i = 0;

    IoT_Publish_Message_Params paramsQOS0;
    IoT_Publish_Message_Params paramsQOS1;

    paramsQOS0.qos = QOS0;
    paramsQOS0.payload = (void *) cPayload;
    paramsQOS0.isRetained = 0;

    // Publish and ignore if "ack" was received or  from AWS IoT Core
    sprintf(cPayload, "{\"%s\" : \"%s %d \"}", "Device", "Hello from ESP32 (QOS0): Count", count);
    paramsQOS0.payloadLen = strlen(cPayload);
    IoT_Error_t rc = aws_iot_mqtt_publish(client, base_topic, base_topic_len, &paramsQOS0);
    if (rc != SUCCESS){
        ESP_LOGE(TAG, "Publish QOS0 error %i", rc);
        rc = SUCCESS;
    }
    count++;
}

void aws_iot_task(void *param) {
    IoT_Error_t rc = FAILURE;

    AWS_IoT_Client client;
    IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
    IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    mqttInitParams.enableAutoReconnect = true; // We enable this later below
    mqttInitParams.pHostURL = HostAddress;
    mqttInitParams.port = port;    
    mqttInitParams.pRootCALocation = (const char *)aws_root_ca_pem_start;
    mqttInitParams.pDeviceCertLocation = (const char *)aws_certificate_pem_crt_start;
    mqttInitParams.pDevicePrivateKeyLocation = (const char *)aws_private_pem_key_start;

    mqttInitParams.mqttCommandTimeout_ms = 20000;
    mqttInitParams.tlsHandshakeTimeout_ms = 5000;
    mqttInitParams.isSSLHostnameVerify = true;
    mqttInitParams.disconnectHandler = disconnect_callback_handler;
    mqttInitParams.disconnectHandlerData = NULL;

    rc = aws_iot_mqtt_init(&client, &mqttInitParams);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_init returned error : %d ", rc);
        abort();
    }

    /* Wait for WiFI to show as connected */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);    

    connectParams.keepAliveIntervalInSec = 10;
    connectParams.isCleanSession = true;
    connectParams.MQTTVersion = MQTT_3_1_1;

    connectParams.pClientID = client_id;
    connectParams.clientIDLen = strlen(client_id);
    connectParams.isWillMsgPresent = false;
    ESP_LOGI(TAG, "Connecting to AWS IoT Core at %s:%d", mqttInitParams.pHostURL, mqttInitParams.port);

    ESP_LOGI(TAG, "Connecting to AWS...");
    do {
        rc = aws_iot_mqtt_connect(&client, &connectParams);
        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    } while(SUCCESS != rc);

    ESP_LOGI(TAG, "Successfully connected to AWS IoT Core!");

    /*
     * Enable Auto Reconnect functionality. Minimum and Maximum time for exponential backoff for retries.
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d", rc);
        abort();
    }

    ESP_LOGI(TAG, "Subscribing to '%s'", subscribe_topic);
    rc = aws_iot_mqtt_subscribe(&client, subscribe_topic, strlen(subscribe_topic), QOS0, iot_subscribe_callback_handler, NULL);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Error subscribing : %d ", rc);
        abort();
    } else{
        ESP_LOGI(TAG, "Subscribed to topic '%s'", subscribe_topic);
    }
    
    ESP_LOGI(TAG, "\n****************************************\n*  AWS client Id - %s  *\n****************************************\n\n",
             client_id);
    
    while((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)) {

        //Max time the yield function will wait for read messages
        rc = aws_iot_mqtt_yield(&client, 100);
        if(NETWORK_ATTEMPTING_RECONNECT == rc) {
            // If the client is attempting to reconnect we will skip the rest of the loop.
            continue;
        }

        ESP_LOGD(TAG, "Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
        
        publisher(&client, base_publish_topic, strlen(base_publish_topic));
    }

    ESP_LOGE(TAG, "An error occurred in the main loop.");
    abort();
}

void app_main()
{
    initialise_wifi();
    xTaskCreatePinnedToCore(&aws_iot_task, "aws_iot_task", 4096 * 2, NULL, 5, NULL, 1);
}