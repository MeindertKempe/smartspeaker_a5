#include "bt_sink.h"
#include "wifi.h"
#include "radio.h"

#include "freertos/FreeRTOS.h"
#include "audio_event_iface.h"
#include "board.h"
#include "nvs_flash.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"

#include "esp_peripherals.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "periph_touch.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;

typedef esp_err_t(audio_init_fn)(audio_board_handle_t *, audio_event_iface_handle_t);
typedef esp_err_t(audio_deinit_fn)(audio_event_iface_handle_t);
typedef esp_err_t(audio_run_fn)(audio_event_iface_msg_t *);

static void app_init(void) {
	esp_log_level_set("*", ESP_LOG_INFO);

	/* Initialise NVS flash. */
	ESP_LOGI(TAG, "Init NVS flash");
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
		// NVS partition was truncated and needs to be erased
		// Retry nvs_flash_init
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}

	/* Initialise audio board */
	ESP_LOGI(TAG, "Start codec chip");
	board_handle = audio_board_init();
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE,
	                     AUDIO_HAL_CTRL_START);

	/* Initialise peripherals */
	ESP_LOGI(TAG, "Initialise peripherals");
	esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
	periph_set                     = esp_periph_set_init(&periph_cfg);

	ESP_LOGI(TAG, "Initialise touch peripheral");
	audio_board_key_init(periph_set);

	ESP_LOGI(TAG, "Set up event listener");
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt                             = audio_event_iface_init(&evt_cfg);

	ESP_LOGI(TAG, "Add keys to event listener");
	audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set),
	                               evt);

	/* Initialise Bluetooth sink component. */
	ESP_LOGI(TAG, "Initialise Bluetooth sink");
	bt_sink_init(periph_set);

	/* Initialise WI-Fi component */
	ESP_LOGI(TAG, "Initialise WI-FI");
	wifi_init();
}

static void app_free(void) {
	ESP_LOGI(TAG, "Deinitialise Bluetooth sink");
	bt_sink_destroy(periph_set);

	audio_event_iface_remove_listener(
	    esp_periph_set_get_event_iface(periph_set), evt);
	audio_event_iface_destroy(evt);

	ESP_LOGI(TAG, "Deinitialise peripherals");
	esp_periph_set_stop_all(periph_set);
	esp_periph_set_destroy(periph_set);

	ESP_LOGI(TAG, "Deinitialise audio board");
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH,
	                     AUDIO_HAL_CTRL_STOP);
	audio_board_deinit(board_handle);
}

static esp_err_t pipeline_init(audio_init_fn init_fn) {
	return init_fn(&board_handle, evt);
}

static esp_err_t pipeline_deinit(audio_deinit_fn deinit_fn) {
	return deinit_fn(evt);
}

static esp_err_t pipeline_run(audio_run_fn run_fn, audio_event_iface_msg_t *msg) {
	return run_fn(msg);
}

void app_main(void) {
	app_init();

	vTaskDelay(3000 / portTICK_PERIOD_MS);

	esp_err_t err = pipeline_init(radio_init);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start radio thread (err=%d) %s", err, esp_err_to_name(err));
		return;
	}

	/* Main eventloop */
	ESP_LOGI(TAG, "Entering main eventloop");
	for (;;) {
		audio_event_iface_msg_t msg;
		err = audio_event_iface_listen(evt, &msg, portMAX_DELAY);

		if (err != ESP_OK) {
			ESP_LOGE(TAG, "[ * ] Event interface error : (%d) %s", err,
			         esp_err_to_name(err));
			continue;
		}

		err = pipeline_run(radio_run, &msg);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Radio handler failed (err=%d) %s", err, esp_err_to_name(err));
			break;
		}
	}

	err = pipeline_deinit(radio_deinit);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to destroy radio pipeline (err=%d) %s", err, esp_err_to_name(err));
	}

	app_free();
}
