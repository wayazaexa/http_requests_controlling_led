/* ESP-IDF example: Simple HTTP server on port 8080
   - Serves a small HTML page at /
   - /on turns LED on, /off turns LED off
   - /status returns "ON" or "OFF"
   - Uses esp_http_server component
   Build with ESP-IDF (tested with v4.x+ / esp-idf v5 compatible)
*/

#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_event.h"

#include "esp_http_server.h"
#include "driver/gpio.h"
#include "wifi_info.h"

// #define EXAMPLE_ESP_WIFI_SSID      "YOUR_SSID"
// #define EXAMPLE_ESP_WIFI_PASS      "YOUR_PASSWORD"
#define LED_GPIO                   GPIO_NUM_2   // change if needed
#define HTTP_PORT                  8080

static const char *TAG = "esp_http_server_example";

/* Simple HTML page */
static const char *html_page =
"<!doctype html>"
"<html>"
"<head><meta charset=\"utf-8\"><title>ESP32 LED Control</title></head>"
"<body>"
"<h1>ESP32 LED Control</h1>"
"<p><a href=\"/on\"><button>Turn ON</button></a>"
" <a href=\"/off\"><button>Turn OFF</button></a></p>"
"<p>Status: <span id=\"status\">unknown</span></p>"
"<script>"
"async function fetchStatus(){"
" try { const r = await fetch('/status'); const t = await r.text(); document.getElementById('status').innerText = t; }"
" catch(e){ document.getElementById('status').innerText = 'error'; }"
"}"
"fetchStatus(); setInterval(fetchStatus,2000);"
"</script>"
"</body>"
"</html>";

/* Handler for root page */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

/* Redirect helper */
static esp_err_t redirect_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Handler to turn LED on */
static esp_err_t led_on_handler(httpd_req_t *req)
{
    gpio_set_level(LED_GPIO, 1);
    return redirect_root(req);
}

/* Handler to turn LED off */
static esp_err_t led_off_handler(httpd_req_t *req)
{
    gpio_set_level(LED_GPIO, 0);
    return redirect_root(req);
}

/* Handler to return LED status */
static esp_err_t status_handler(httpd_req_t *req)
{
    int level = gpio_get_level(LED_GPIO);
    const char *msg = level ? "ON" : "OFF";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, msg, strlen(msg));
    return ESP_OK;
}

/* Start the web server on port 8080 */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.ctrl_port = HTTP_PORT + 1; // optional separate control port

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t on = {
            .uri       = "/on",
            .method    = HTTP_GET,
            .handler   = led_on_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &on);

        httpd_uri_t off = {
            .uri       = "/off",
            .method    = HTTP_GET,
            .handler   = led_off_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &off);

        httpd_uri_t status = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &status);

        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
    return server;
}

/* Initialize WiFi in station mode and connect */
static void initialise_wifi(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    ESP_LOGI(TAG, "WiFi initialization attempted");
}

/* Simple event handler to log connection status (optional) */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void app_main(void)
{
    // Init NVS
    nvs_flash_init();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    // Init WiFi
    initialise_wifi();

    // Configure LED GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Start web server
    start_webserver();
}
