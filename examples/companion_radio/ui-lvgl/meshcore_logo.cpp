// MeshCore wordmark for the boot splash. The bitmap is the same one the other
// display UIs draw; it is expanded to a 2x alpha mask so LVGL can scale it into
// place and tint it from the widget style.
#include <lvgl.h>
#include "../ui-new/icons.h"
#include "lv_psram_pool.h"

#define LOGO_W      128
#define LOGO_H      13
#define LOGO_ZOOM   2

static lv_image_dsc_t logo_dsc;

const lv_image_dsc_t* meshcoreLogoImage() {
  if (logo_dsc.data != NULL) return &logo_dsc;

  const uint32_t w = LOGO_W * LOGO_ZOOM, h = LOGO_H * LOGO_ZOOM;
  uint8_t* mask = (uint8_t *) lv_psram_pool_alloc(w * h);
  if (mask == NULL) return NULL;

  for (uint32_t y = 0; y < h; y++) {
    const uint8_t* row = &meshcore_logo[(y / LOGO_ZOOM) * (LOGO_W / 8)];
    for (uint32_t x = 0; x < w; x++) {
      uint32_t sx = x / LOGO_ZOOM;
      mask[y * w + x] = (row[sx >> 3] & (0x80 >> (sx & 7))) ? 0xff : 0x00;
    }
  }

  logo_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  logo_dsc.header.cf = LV_COLOR_FORMAT_A8;
  logo_dsc.header.w = w;
  logo_dsc.header.h = h;
  logo_dsc.header.stride = w;
  logo_dsc.data_size = w * h;
  logo_dsc.data = mask;
  return &logo_dsc;
}
