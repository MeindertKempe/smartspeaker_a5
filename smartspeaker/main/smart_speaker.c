#include "bt_sink.h"
#include "lcd.h"
#include "led_controller_commands.h"
#include "radio.h"
#include "wifi.h"

#include "audio_event_iface.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "driver/i2c.h"
#include "i2s_stream.h"

#include "esp_peripherals.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "periph_touch.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#define ARRAY_SIZE(a) ((sizeof a) / (sizeof a[0]))

static const char *TAG = "MAIN";

static audio_board_handle_t board_handle;
static esp_periph_set_handle_t periph_set;
static audio_event_iface_handle_t evt;
static audio_element_handle_t i2s_stream_writer;

static bool use_radio = true;
static int player_volume;

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

	ESP_LOGI(TAG, "Initialise audio board");
	board_handle = audio_board_init();
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE,
	                     AUDIO_HAL_CTRL_START);

	ESP_LOGI(TAG, "Initialise peripherals");
	esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
	periph_set                     = esp_periph_set_init(&periph_cfg);

	ESP_LOGI(TAG, "Initialise touch peripheral");
	audio_board_key_init(periph_set);

	ESP_LOGI(TAG, "Initialise event listener");
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	evt                             = audio_event_iface_init(&evt_cfg);

	ESP_LOGI(TAG, "Add keys to event listener");
	audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set),
	                               evt);

	i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
	i2s_cfg.type             = AUDIO_STREAM_WRITER;
	i2s_stream_writer        = i2s_stream_init(&i2s_cfg);

	ESP_LOGI(TAG, "Run LCD task");
	xTaskCreatePinnedToCore(lcd1602_task, "lcd_task", 1024 * 2, NULL, 5, NULL,
	                        1);

	ESP_LOGI(TAG, "Initialise WI-FI");
	wifi_init();
	wifi_wait();
}

static void app_free(void) {
	ESP_LOGI(TAG, "Remove keys from event listener");
	audio_event_iface_remove_listener(
	    esp_periph_set_get_event_iface(periph_set), evt);

	ESP_LOGI(TAG, "Deinitialise event listener");
	audio_event_iface_destroy(evt);

	ESP_LOGI(TAG, "Deinitialise peripherals");
	esp_periph_set_stop_all(periph_set);
	esp_periph_set_destroy(periph_set);

	ESP_LOGI(TAG, "Deinitialise audio board");
	audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH,
	                     AUDIO_HAL_CTRL_STOP);
	audio_board_deinit(board_handle);

	// TODO function for deinitialising wifi
}

void switch_stream() {
	if (use_radio) {
		use_radio = false;

		ESP_LOGI(TAG, "Deinitialise radio");
		ESP_ERROR_CHECK(deinit_radio(&i2s_stream_writer, 1, &evt));

		ESP_LOGI(TAG, "Initialise Bluetooth sink");
		ESP_ERROR_CHECK(init_bt(&i2s_stream_writer, 1, &evt, periph_set));
	} else {
		use_radio = true;

		ESP_LOGI(TAG, "Deinitialise Bluetooth");
		ESP_ERROR_CHECK(deinit_bt(&i2s_stream_writer, 1, &evt, periph_set));

		ESP_LOGI(TAG, "Initialise radio");
		ESP_ERROR_CHECK(init_radio(&i2s_stream_writer, 1, &evt));
	}
}

void app_main() {
	// Initialise component dependencies
	app_init();

	ESP_RETURN_ON_ERROR(init_radio(&i2s_stream_writer, 1, &evt), TAG,
	                    "Failed to start radio thread");

	// Main loop
	for (;;) {
		audio_event_iface_msg_t msg;
		ESP_RETURN_ON_ERROR(audio_event_iface_listen(evt, &msg, portMAX_DELAY),
		                    TAG, "Event interface error");

		if (use_radio) {
			ESP_RETURN_ON_ERROR(radio_run(&msg), TAG, "Radio handler failed");
		} else {
			ESP_RETURN_ON_ERROR(bt_run(&msg), TAG, "Bluetooth handler failed");
		}

		if (msg.source_type == PERIPH_ID_TOUCH ||
		    msg.source_type == PERIPH_ID_BUTTON ||
		    msg.source_type == PERIPH_ID_ADC_BTN) {
			if (msg.cmd == PERIPH_TOUCH_TAP ||
			    msg.cmd == PERIPH_BUTTON_PRESSED ||
			    msg.cmd == PERIPH_ADC_BUTTON_PRESSED) {
				if ((int)msg.data == get_input_play_id()) {
					ESP_LOGI(TAG, "[ * ] [Play] touch tap event");
					switch_stream();
				} else if ((int)msg.data == get_input_set_id()) {
					ESP_LOGI(TAG, "[ * ] [Set] touch tap event");
				} else if ((int)msg.data == get_input_volup_id()) {
					ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");

					player_volume += 10;
					if (player_volume > 100) { player_volume = 100; }
					audio_hal_set_volume(board_handle->audio_hal,
					                     player_volume);
					led_controller_set_leds_volume(player_volume);
				} else if ((int)msg.data == get_input_voldown_id()) {
					ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");

					player_volume -= 10;
					if (player_volume < 0) { player_volume = 0; }
					audio_hal_set_volume(board_handle->audio_hal,
					                     player_volume);
					led_controller_set_leds_volume(player_volume);
				}
			}
		}
	}

	// Deinitialise component dependencies
	app_free();
}
