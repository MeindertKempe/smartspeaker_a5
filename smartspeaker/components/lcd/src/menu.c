#include "menu.h"
#include "lcd.h"

#include "lcd_util.h"
#include <stdlib.h>

#include <esp_log.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

int isPartyModeOn = 0;
int isBLuetoothOn = 0;
int isRadioOn     = 0;

static const char *lcdTag     = "LCD";
static const char *btnOkTag   = "Button ok";
static const char *btnUpTag   = "Button up";
static const char *btnDownTag = "Button down";

/**
 * @brief Turns bluetooth on when off and off when on.
 */
static void bluetoothOnOff(void *args) {
	isBLuetoothOn = !isBLuetoothOn;
	ESP_LOGI(lcdTag, "bluetooth %d", isBLuetoothOn);
}

/**
 * @brief Turns party mode on when off and off when on.
 */
static void partyModeOnOff(void *args) {
	isPartyModeOn = !isPartyModeOn;
	ESP_LOGI(lcdTag, "party mode %d", isPartyModeOn);
}

/**
 * @brief Turns radio on when off and off when on.
 */
static void radioOnOff(void *args) {
	isRadioOn = !isRadioOn;
	ESP_LOGI(lcdTag, "radio %d", isRadioOn);
}

/**
 * @brief Goes a channel down. (radio)
 */
static void changeChannelDown(void *args) { ESP_LOGI(lcdTag, "channel down"); }

/**
 * @brief Goes a channel up. (radio)
 */
static void changeChannelUp(void *args) { ESP_LOGI(lcdTag, "channel up"); }

/**
 * @brief Turns volume up.
 */
static void plusVolume(void *args) { ESP_LOGI(lcdTag, "volume up"); }

/**
 * @brief Turns volume down.
 */
static void minVolume(void *args) { ESP_LOGI(lcdTag, "volume down"); }

static void screen_draw_menu(struct screen *screen, int redraw);
static void screen_event_handler_menu(struct screen *screen, enum button_id);
static void screen_draw_welcome(struct screen *screen, int redraw);
static void screen_event_handler_welcome(struct screen *screen, enum button_id);

static struct menu menu_main;

static struct menu_item menu_clock_items[] = {
	{ .type = MENU_TYPE_FUNCTION, .name = "+", .data.function = plusVolume },
	{ .type = MENU_TYPE_FUNCTION, .name = "-", .data.function = minVolume },
	{ .type = MENU_TYPE_MENU, .name = "Back", .data.menu = &menu_main },
};

static struct menu_item menu_radio_items[] = {
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Radio On/Off",
	  .data.function = radioOnOff },
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Change channel up",
	  .data.function = changeChannelUp },
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Change channel down",
	  .data.function = changeChannelDown },

	{ .type = MENU_TYPE_FUNCTION, .name = "+", .data.function = plusVolume },
	{ .type = MENU_TYPE_FUNCTION, .name = "-", .data.function = minVolume },
	{ .type = MENU_TYPE_MENU, .name = "Back", .data.menu = &menu_main },
};

static struct menu_item menu_bluetooth_items[] = {
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Bluetooth On/Off",
	  .data.function = bluetoothOnOff },
	{ .type          = MENU_TYPE_FUNCTION,
	  .name          = "Partymode On/Off",
	  .data.function = partyModeOnOff },
	{ .type = MENU_TYPE_FUNCTION, .name = "+", .data.function = plusVolume },
	{ .type = MENU_TYPE_FUNCTION, .name = "-", .data.function = minVolume },
	{ .type = MENU_TYPE_MENU, .name = "Back", .data.menu = &menu_main },
};

/* menus */
static struct menu menu_clock = {
	.size  = ARRAY_SIZE(menu_clock_items),
	.index = 0,
	.items = menu_clock_items,
};

static struct menu menu_radio = {
	.size  = ARRAY_SIZE(menu_radio_items),
	.index = 0,
	.items = menu_radio_items,
};

static struct menu menu_bluetooth = {
	.size  = ARRAY_SIZE(menu_bluetooth_items),
	.index = 0,
	.items = menu_bluetooth_items,
};

static struct menu_item menu_main_items[] = {
	{ .type = MENU_TYPE_MENU, .name = "Clock", .data.menu = &menu_clock },
	{ .type = MENU_TYPE_MENU, .name = "Radio", .data.menu = &menu_radio },
	{ .type      = MENU_TYPE_MENU,
	  .name      = "Bluetooth",
	  .data.menu = &menu_bluetooth },
};

static struct menu menu_main = {
	.size  = ARRAY_SIZE(menu_main_items),
	.index = 0,
	.items = menu_main_items,
};

struct screen screen_menu = {
	.draw          = screen_draw_menu,
	.event_handler = screen_event_handler_menu,
	.data          = &menu_main,
};

struct screen screen_welcome = {
	.draw          = screen_draw_welcome,
	.event_handler = screen_event_handler_welcome,
	.data          = NULL,
};

static struct screen *screen_current = &screen_welcome;
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * @brief Draws the sub maps, functions and pointer on the screen.
 */
static void screen_draw_menu(struct screen *screen, int redraw) {
	if (redraw) lcd_clear();
	struct menu *menu = screen->data;

	if (menu->size > CONFIG_LCD_NUM_ROWS) goto more_lines;

	for (size_t i = 0; i < menu->size; i++) {
		lcd_move_cursor(0, i);
		if (menu->index == i) lcd_write_str("-");
		else lcd_write_str(" ");
		lcd_write_str(menu->items[i].name);
	}

	return;
more_lines:

	for (size_t i = MIN(menu->index, menu->size - CONFIG_LCD_NUM_ROWS);
	     i < MIN(menu->index, menu->size - CONFIG_LCD_NUM_ROWS) +
	             CONFIG_LCD_NUM_ROWS;
	     ++i) {
		lcd_move_cursor(0,
		                i - MIN(menu->index, menu->size - CONFIG_LCD_NUM_ROWS));
		if (menu->index == i) lcd_write_str("-");
		else lcd_write_str(" ");
		lcd_write_str(menu->items[i].name);
	}
}

/**
 * @brief Handles button presses on main menu so the pointer arrow can go up,
 * down and run functions.
 */
static void screen_event_handler_menu(struct screen *screen,
                                      enum button_id button) {
	ESP_LOGI(lcdTag, "button: %d", button);
	struct menu *menu           = screen->data;
	struct menu_item *menu_item = &menu->items[menu->index];
	switch (button) {
		case BUTTON_DOWN:
			if (menu->index > 0) menu->index--;
			screen_current->draw(screen_current, true);
			break;
		case BUTTON_UP:
			if (menu->index < menu->size - 1) menu->index++;
			screen_current->draw(screen_current, true);
			break;
		case BUTTON_OK:
			switch (menu_item->type) {
				case MENU_TYPE_MENU:
					if (menu_item->data.menu)
						screen->data = menu_item->data.menu;
					break;
				case MENU_TYPE_FUNCTION:
					if (menu_item->data.function)
						menu_item->data.function(NULL);
					break;
				case MENU_TYPE_SCREEN:
					if (menu_item->data.screen)
						screen_current = menu_item->data.screen;
					break;
				default: break;
			}
			screen_current->draw(screen_current, true);
			break;
		default: break;
	}
}

/**
 * @brief Draws the welcome screen.
 */
static void screen_draw_welcome(struct screen *screen, int redraw) {
	lcd_clear();
	lcd_move_cursor(0, 0);

	lcd_write_str("Welcome");
	lcd_move_cursor(0, 1);
	lcd_write_str("Press middle button");
	lcd_move_cursor(0, 2);
	lcd_write_str("to navigate to main");
	lcd_move_cursor(0, 3);
	lcd_write_str("menu");
}

/**
 * @brief Handles when ok button is pressed on welcome screen so it loads main
 * menu.
 */
static void screen_event_handler_welcome(struct screen *screen,
                                         enum button_id button) {
	ESP_LOGI(lcdTag, "button: %d", button);

	switch (button) {
		case BUTTON_OK:
			screen_current = &screen_menu;
			screen_current->draw(screen_current, 1);
			break;
		default: break;
	}
}

void lcd1602_task(void *pvParameter) {
	// Set up I2C
	i2c_master_init();

	lcd_button_init();

	lcd_init();

	lcd_clear();

	// Initialize previous button states
	int prevBtnUp   = -1;
	int prevBtnOk   = -1;
	int prevBtnDown = -1;

	screen_current->draw(screen_current, 1);

	for (;;) {
		// Read the button state
		uint8_t value = lcd_button_read();

		int btnUp   = (value >> 2) & 1;
		int btnOk   = (value >> 0) & 1;
		int btnDown = (value >> 1) & 1;

		// Check for changes in button states
		if (btnUp != prevBtnUp || btnOk != prevBtnOk ||
		    btnDown != prevBtnDown) {

			ESP_LOGI(btnUpTag, "Button Up: %d", btnUp);
			ESP_LOGI(btnDownTag, "Button Down: %d", btnDown);
			ESP_LOGI(btnOkTag, "Button OK: %d", btnOk);

			prevBtnUp   = btnUp;
			prevBtnOk   = btnOk;
			prevBtnDown = btnDown;

			if (btnOk == 1) {
				screen_current->event_handler(screen_current, BUTTON_OK);
			}
			if (btnDown == 1) {
				screen_current->event_handler(screen_current, BUTTON_DOWN);
			}
			if (btnUp == 1) {
				screen_current->event_handler(screen_current, BUTTON_UP);
			}
		}
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
	ESP_LOGE(lcdTag, "DOOD");
	vTaskDelete(NULL);
}