#include "ads7846.h"
#include "common.h"
#include "fifo.h"
#include "hspi.h"
#include "mi0283qt.h"
#include "mp3.h"
#include "stream_client.h"
#include "terminal.h"
#include "wm8731.h"

#include "esp/hwrand.h"
#include "esp/uart.h"
#include "esp8266.h"
#include "espressif/esp_common.h"
#include "ssid_config.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"

#include <stdio.h>
#include <string.h>

static void btn_prev_channel(void) { printf("Button: previous channel\n"); }
static void btn_next_channel(void) { printf("Button: next channel\n"); }
static void btn_standby(void) { printf("Button: standby\n"); }
static void btn_settings(void) { printf("Button: settings\n"); }
static void btn_play_pause(void) { printf("Button: play/pause\n"); }
static void btn_vol_minus(void) { printf("Button: vol -\n"); }
static void btn_vol_plus(void) { printf("Button: vol +\n"); }

extern const struct image img_arrow_left;
extern const struct image img_arrow_right;
extern const struct image img_standby;
extern const struct image img_settings;
extern const struct image img_play;
extern const struct image img_vol_minus;
extern const struct image img_vol_plus;
const struct button {
  uint16_t pos_x;
  uint16_t pos_y;
  const struct image *face;
  void (*callback)(void);
} buttons[] = {
    {0, 0, &img_arrow_left, btn_prev_channel},
    {200, 0, &img_arrow_right, btn_next_channel},
    {0, 192, &img_standby, btn_standby},
    {48, 192, &img_settings, btn_settings},
    {96, 192, &img_play, btn_play_pause},
    {144, 192, &img_vol_minus, btn_vol_minus},
    {192, 192, &img_vol_plus, btn_vol_plus},
};

void ui_draw_buttons(void) {
  for (int i = 0; i < ARRAY_SIZE(buttons); ++i) {
    const struct button *b = buttons + i;
    lcd_image(b->pos_x, b->pos_y, b->face);
  }
}

void ui_task(void *p) {
  ads_init();

  ads_calibrate(80);

  ui_draw_buttons();

  for (int i = 0;; ++i) {
    vTaskDelay(2);

    uint32_t x, y;
    if (ads_poll(&x, &y)) {
      for (int i = 0; i < ARRAY_SIZE(buttons); ++i) {
        const struct button *b = buttons + i;
        if (x < b->pos_x || x >= (b->pos_x + b->face->width))
          continue;
        if (y < b->pos_y || y >= (b->pos_y + b->face->height))
          continue;
        b->callback();
      }
      vTaskDelay(10);
    }

    unsigned int underruns = get_and_reset_underrun_counter();
    if (underruns)
      printf("!!! %u underruns\n", underruns);

#if 0
    printf("free heap: %u\nfifo: %u/%u\nunderruns: %u\n\n",
           xPortGetFreeHeapSize(), fifo_fill(), fifo_size(),
           get_and_reset_underrun_counter());
#endif
  }

  vTaskDelete(NULL);
}

static void stream_up(void) {
  if (xTaskCreate(mp3_task, "decode", 2100, NULL, 4, NULL) != pdPASS) {
    printf("Failed to create mp3 task!\n");
  }
}

static void stream_metadata(enum stream_metadata type, const char *s) {
  switch (type) {
  case STREAM_ARTIST:
    printf("Artist: %s\n", s);
    break;
  case STREAM_TITLE:
    printf("Title: %s\n", s);
    break;
  }
}

void user_init(void) {
  int ret;

  uart_set_baud(0, 115200);
  hspi_init();

  if ((ret = lcd_init())) {
    printf("lcd_init failed (%d)\n", ret);
    goto fail;
  }
  term_init();

  if ((ret = fifo_init())) {
    printf("fifo_init failed (%d)\n", ret);
    goto fail;
  }

  if ((ret = wm8731_init())) {
    printf("wm8731_init failed (%d)\n", ret);
    goto fail;
  }

  struct sdk_station_config config = {
      .ssid = WIFI_SSID,
      .password = WIFI_PASS,
  };

  /* required to call wifi_set_opmode before station_set_config */
  sdk_wifi_set_opmode(STATION_MODE);
  sdk_wifi_station_set_config(&config);

  if (stream_start("r.ezbt.me", "/antenne", stream_up, stream_metadata)) {
    printf("Failed to create stream task!\n");
    goto fail;
  }

  if (xTaskCreate(ui_task, "UI", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL) !=
      pdPASS) {
    printf("Failed to create UI task!\n");
    goto fail;
  }

  return;

fail:
  while (1)
    ;
}
