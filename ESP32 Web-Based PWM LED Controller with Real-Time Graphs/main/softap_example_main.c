/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/param.h>
#include <esp_http_server.h>
#include "esp_netif.h"

#include "driver/gpio.h"
#include "driver/ledc.h"  // Add this at the top if missing

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN
#define LED                        GPIO_NUM_2       
#define RESPONSE_SIZE 4096  // Use 4096 or more


#define MAX_LOG_ENTRIES 100

typedef struct {
    uint32_t timestamp_ms;
    uint8_t value;
} SliderLogEntry;

static SliderLogEntry slider_log[MAX_LOG_ENTRIES];
static int log_index = 0;

void log_slider_value(uint8_t value) {
    SliderLogEntry entry;
    entry.timestamp_ms = esp_log_timestamp(); // returns ms since boot
    entry.value = value;
    slider_log[log_index % MAX_LOG_ENTRIES] = entry;
    log_index++;
}

static esp_err_t history_handler(httpd_req_t *req)
{
    char json[4096];
    strcpy(json, "[");

    int start = (log_index > MAX_LOG_ENTRIES) ? log_index - MAX_LOG_ENTRIES : 0;
    int count = (log_index > MAX_LOG_ENTRIES) ? MAX_LOG_ENTRIES : log_index;

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % MAX_LOG_ENTRIES;
        char entry[64];
        snprintf(entry, sizeof(entry),
            "{\"t\":%lu,\"v\":%d}%s",
            slider_log[idx].timestamp_ms,
            slider_log[idx].value,
            (i < count - 1) ? "," : ""
        );
        strcat(json, entry);
    }

    strcat(json, "]");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static const httpd_uri_t history = {
    .uri       = "/history",
    .method    = HTTP_GET,
    .handler   = history_handler,
    .user_ctx  = NULL
};



/*****************WEBSERVER CODE BEGINS***************************** */




static const char *TAG = "webserver";
/* An HTTP GET handler */

static bool led_state = false;
static uint32_t slider_value = 0;  // initial slider position

static const char *response_template =
"<!DOCTYPE html><html><head><style>\
input[type=range] { width: 300px; }\
canvas { max-width: 100%%; height: auto; margin-top: 20px; }\
</style>\
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>\
</head><body>\
<h1>ESP32 WebServer</h1>\
<h2>LED Control (ON at 100, OFF at 0)</h2>\
<input type='range' min='0' max='100' value='%d' id='ledSlider' oninput='sendValue(this.value)'>\
<p id='ledStatus'>Current: %d%%</p>\
<canvas id='brightnessChart'></canvas>\
<canvas id='dutyChart'></canvas>\
<script>\
let lastSent = -1;\
let timeLabels = [];\
let sliderData = [];\
let voltageData = [];\
let dutyData = [];\
\
let brightnessChart = new Chart(document.getElementById('brightnessChart').getContext('2d'), {\
    type: 'line',\
    data: {\
        labels: timeLabels,\
        datasets: [\
            { label: 'Brightness %%', data: sliderData, borderWidth: 2, fill: false },\
            { label: 'Voltage (V)', data: voltageData, borderWidth: 2, fill: false }\
        ]\
    },\
    options: { scales: { y: { beginAtZero: true, max: 3.3 } } }\
});\
\
let dutyChart = new Chart(document.getElementById('dutyChart').getContext('2d'), {\
    type: 'line',\
    data: {\
        labels: timeLabels,\
        datasets: [\
            { label: 'PWM Duty', data: dutyData, borderWidth: 2, borderDash: [5,5], fill: false }\
        ]\
    },\
    options: { scales: { y: { beginAtZero: true, max: 8200 } } }\
});\
\
function sendValue(val) {\
    val = parseInt(val);\
    document.getElementById('ledStatus').innerText = 'Current: ' + val + '%%';\
    if (val !== lastSent) {\
        lastSent = val;\
        fetch('/set?value=' + val)\
            .then(response => response.text())\
            .then(data => {\
                console.log('Server:', data);\
                updateCharts(val);\
            })\
            .catch(error => console.error('Error:', error));\
    }\
}\
\
function updateCharts(brightness) {\
    let voltage = (brightness / 100.0) * 3.3;\
    let duty = Math.round((brightness / 100.0) * 8191);\
    if (brightness > 0 && duty < 100) duty = 100;\
    let now = new Date();\
    let timeStr = now.getHours() + ':' + now.getMinutes() + ':' + now.getSeconds();\
\
    if (sliderData.length > 50) {\
        sliderData.shift(); voltageData.shift(); dutyData.shift(); timeLabels.shift();\
    }\
\
    sliderData.push(brightness);\
    voltageData.push(voltage.toFixed(2));\
    dutyData.push(duty);\
    timeLabels.push(timeStr);\
    brightnessChart.update();\
    dutyChart.update();\
}\
</script>\
</body></html>";

// Root page handler
static esp_err_t root_get_handler(httpd_req_t *req)
{
    char *response = malloc(RESPONSE_SIZE);
    if (!response) return httpd_resp_send_500(req);

    snprintf(response, RESPONSE_SIZE, response_template, slider_value, slider_value);
    esp_err_t result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    free(response);
    return result;
}

// LED control handler
static esp_err_t set_led_handler(httpd_req_t *req)
{
    char buf[32];
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > sizeof(buf)) return httpd_resp_send_500(req);

    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char param[8];
        if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
            int val = atoi(param);
            slider_value = val;

            // Map 0–100 to 0–8191 (13-bit duty cycle)
            int duty = (val * 8191) / 100;
            if (val > 0 && duty < 100) {
                duty = 100;  // Ensure visible brightness for non-zero slider
            }
            ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
            ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

            char msg[64];
            snprintf(msg, sizeof(msg), "Brightness set to %d%% (duty %d)", val, duty);
            ESP_LOGI(TAG, "%s", msg);
            return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
        }
    }
    return httpd_resp_send(req, "Invalid input", HTTPD_RESP_USE_STRLEN);
}

// URI registration
static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t set_led = {
    .uri       = "/set",
    .method    = HTTP_GET,
    .handler   = set_led_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &set_led);
        httpd_register_uri_handler(server, &history);
        ESP_LOGI(TAG, "Webserver started");
        return server;
    }
    ESP_LOGE(TAG, "Failed to start webserver");
    return NULL;
}


//#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
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
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}
/***************WEBSERVER CODE ENDS********************** */

/***********LED BASED CODE*********************************/
static void configure_led(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_HIGH_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_13_BIT,  // 13-bit resolution (0–8191)
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL_0,
        .duty       = 0,
        .gpio_num   = LED,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

void app_main(void)
{
    static httpd_handle_t server = NULL;
    configure_led();
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &connect_handler, &server));
}
