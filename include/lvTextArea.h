#ifndef LV_TEXT_AREA_h
#define LV_TEXT_AREA_h

#include "lvBase.h"

class LvTextArea : public LvBase<LvTextArea> {
public:
    explicit LvTextArea(lv_obj_t* parent) {
        obj = lv_textarea_create(parent);
    }

    LvTextArea& oneLine(bool v = true) {
        lv_textarea_set_one_line(obj, v);
        return *this;
    }

    LvTextArea& placeholder(const char* txt) {
        lv_textarea_set_placeholder_text(obj, txt);
        return *this;
    }

    LvTextArea& font(const lv_font_t* f) {
        lv_obj_set_style_text_font(obj, f, 0);
        return *this;
    }

    LvTextArea& textColor(uint32_t hex) {
        lv_obj_set_style_text_color(obj, lv_color_hex(hex), 0);
        return *this;
    }

    LvTextArea& borderColor(uint32_t hex) {
        lv_obj_set_style_border_color(obj, lv_color_hex(hex), 0);
        return *this;
    }

    LvTextArea& borderWidth(int w) {
        lv_obj_set_style_border_width(obj, w, 0);
        return *this;
    }

    LvTextArea& radius(int r) {
        lv_obj_set_style_radius(obj, r, 0);
        return *this;
    }

    LvTextArea& onFocus(lv_event_cb_t cb, void* user = nullptr) {
        lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
        return *this;
    }
};
#endif