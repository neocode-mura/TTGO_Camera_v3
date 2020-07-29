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
#include "tcpip_adapter.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"

#include "esp_camera.h"
#include "esp_timer.h"
#include "ssd1306.h"

#include "event_source.h"
#include "camera_index.h"

#include <esp_http_server.h>

//TTGO Camera PIN Map
#define CAM_PIN_PWDN    26//-1 //power down is not used
#define CAM_PIN_RESET   -1 //software reset will be performed
#define CAM_PIN_XCLK    32//21
#define CAM_PIN_SIOD    13//26
#define CAM_PIN_SIOC    12//27

#define CAM_PIN_D7      39//35
#define CAM_PIN_D6      36//34
#define CAM_PIN_D5      23//39
#define CAM_PIN_D4      18//36
#define CAM_PIN_D3      15//19
#define CAM_PIN_D2      4//18
#define CAM_PIN_D1      14// 5
#define CAM_PIN_D0      5// 4
#define CAM_PIN_VSYNC   27//25
#define CAM_PIN_HREF    25//23
#define CAM_PIN_PCLK    19//22

static camera_config_t camera_config = {
    .pin_pwdn  = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,//YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_UXGA,//QQVGA-QXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 12, //0-63 lower number means higher quality
    .fb_count = 1 //if more than one, i2s runs in continuous mode. Use only with JPEG
};

static char *TAG = "TTGO_Camera_v2";

esp_err_t camera_init2(){
    //power up the camera if PWDN pin is defined
    if(CAM_PIN_PWDN != -1){
        gpio_set_direction(CAM_PIN_PWDN, GPIO_MODE_OUTPUT);
        int level = 0;
		gpio_set_level(CAM_PIN_PWDN, level);
    }

    //initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";


static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz, index_html_gz_len);
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char  *buf;
    size_t buf_len;
    char variable[32] = {0,};
    char value[32] = {0,};

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char *)malloc(buf_len);
        if (!buf) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                    httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
            } else {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        } else {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int val = atoi(value);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!strcmp(variable, "framesize")) {
        if (s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
    } else if (!strcmp(variable, "quality")) res = s->set_quality(s, val);
    else if (!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
    else if (!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
    else if (!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
    else if (!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
    else if (!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
    else if (!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
    else if (!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
    else if (!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
    else if (!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
    else if (!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
    else if (!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
    else if (!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
    else if (!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
    else if (!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
    else if (!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
    else if (!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
    else if (!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
    else if (!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
    else if (!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
    else if (!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
    else if (!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
    else if (!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
    else {
        res = -1;
    }

    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1024];

    sensor_t *s = esp_camera_sensor_get();
    char *p = json_response;
    *p++ = '{';

    p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p += sprintf(p, "\"quality\":%u,", s->status.quality);
    p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p += sprintf(p, "\"awb\":%u,", s->status.awb);
    p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p += sprintf(p, "\"aec\":%u,", s->status.aec);
    p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p += sprintf(p, "\"agc\":%u,", s->status.agc);
    p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

esp_err_t jpg_httpd_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    res = httpd_resp_set_type(req, "image/jpeg");
    if(res == ESP_OK){
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }

    if(res == ESP_OK){
        if(fb->format == PIXFORMAT_JPEG){
            fb_len = fb->len;
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        } else {
//            jpg_chunking_t jchunk = {req, 0};
//            res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
//            httpd_resp_send_chunk(req, NULL, 0);
//            fb_len = jchunk.len;
        }
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %uKB %ums", (uint32_t)(fb_len/1024), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

esp_err_t jpg_stream_httpd_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t * _jpg_buf;
    char * part_buf[64];
    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        if(fb->format != PIXFORMAT_JPEG){
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            if(!jpeg_converted){
                ESP_LOGE(TAG, "JPEG compression failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(fb->format != PIXFORMAT_JPEG){
            free(_jpg_buf);
        }
        esp_camera_fb_return(fb);
        if(res != ESP_OK){
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        ESP_LOGI(TAG, "MJPG: %uKB %ums (%.1ffps)",
            (uint32_t)(_jpg_buf_len/1024),
            (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    }

    last_frame = 0;
    return res;
}

httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
};

httpd_uri_t status_uri = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = status_handler,
    .user_ctx  = NULL
};

httpd_uri_t cmd_uri = {
    .uri       = "/control",
    .method    = HTTP_GET,
    .handler   = cmd_handler,
    .user_ctx  = NULL
};

httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = jpg_httpd_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t camera = {
	.uri = "/stream",
	.method = HTTP_GET,
	.handler = jpg_stream_httpd_handler,
	.user_ctx = "camera"
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_handle_t server2 = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &cmd_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &capture_uri);

        config.server_port += 1;
        config.ctrl_port += 1;
        if (httpd_start(&server2, &config) == ESP_OK) {
        	httpd_register_uri_handler(server2, &camera);
        	return server;
        }
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
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

SSD1306_t dev;
#define PIR_SENSOR_PIN 33
int blink = 0;

// Handler which executes when the timer expiry event gets executed by the loop.
static void timer_expiry_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
	if(gpio_get_level(PIR_SENSOR_PIN))
	{
		ssd1306_display_text(&dev, 6, "Pir Sensor On ", 14, true);
	}
	else
	{
		ssd1306_display_text(&dev, 6, "Pir Sensor Off", 14, false);
	}
	if(blink)
	{
		blink = 0;
		ssd1306_display_text(&dev, 7, " ", 1, false);
	}
	else
	{
		blink = 1;
		ssd1306_display_text(&dev, 7, " ", 1, true);
	}
}

// Callback that will be executed when the timer period lapses. Posts the timer expiry event
// to the default event loop.
static void timer_callback(void* arg)
{
     ESP_ERROR_CHECK(esp_event_post(TIMER_EVENTS, TIMER_EVENT_EXPIRY, NULL, 0, portMAX_DELAY));
}

esp_timer_handle_t TIMER;
/* Event source periodic timer related definitions */
ESP_EVENT_DEFINE_BASE(TIMER_EVENTS);

void app_main()
{
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

    camera_init2();

    /* Start the server for the first time */
    server = start_webserver();

#if CONFIG_I2C_INTERFACE
	ESP_LOGI(TAG, "INTERFACE is i2c");
	ESP_LOGI(TAG, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ESP_LOGI(TAG, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	ESP_LOGI(TAG, "CONFIG_RESET_GPIO=%d",CONFIG_RESET_GPIO);
	i2c_master_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
#if CONFIG_SSD1306_128x64
	ESP_LOGI(TAG, "Panel is 128x64");
	i2c_init(&dev, 128, 64, 0x3C);
#endif // CONFIG_SSD1306_128x64
#endif // CONFIG_I2C_INTERFACE

	ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);
	ssd1306_display_text(&dev, 0, TAG, 14, false);
	ssd1306_display_text(&dev, 1, " SSD1306 128x64", 15, false);

	tcpip_adapter_ip_info_t s_ip_info;
	char char_ip_addr[16];
	int strLen;

	ssd1306_display_text(&dev, 3, "IP address:", 11, false);

	tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &s_ip_info);
	strLen = sprintf(char_ip_addr, IPSTR, IP2STR(&s_ip_info.ip));
	ssd1306_display_text(&dev, 4, char_ip_addr, strLen, false);

    gpio_set_direction(PIR_SENSOR_PIN, GPIO_MODE_INPUT);

    ESP_ERROR_CHECK(esp_event_handler_register(TIMER_EVENTS, TIMER_EVENT_EXPIRY, timer_expiry_handler, NULL));

    // Create and start the event sources
    esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &TIMER));
    ESP_ERROR_CHECK(esp_timer_start_periodic(TIMER, TIMER_PERIOD));
}
