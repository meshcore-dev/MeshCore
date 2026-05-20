#ifndef LV_BASE_h
#define LV_BASE_h
#include "lvgl.h"

template<typename T>
class LvBase {
public:
    operator lv_obj_t*() const { return obj; }
    lv_obj_t* raw() const { return obj; }

    T& width(int v) {
        lv_obj_set_width(obj, v == 100 ? lv_pct(100) : v);
        return self();
    }

    T& height(int v) {
        lv_obj_set_height(obj, v);
        return self();
    }

    T& padAll(int v) {
        lv_obj_set_style_pad_all(obj, v, 0);
        return self();
    }

    T& bgOpa(int v) {
        lv_obj_set_style_bg_opa(obj, v, 0);
        return self();
    }

    T& border(int w) {
        lv_obj_set_style_border_width(obj, w, 0);
        return self();
    }

    T& scrollable(bool isScrollable) {
        if (isScrollable) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
        } else {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
        }
        return self();
    }

    T& position(lv_coord_t x, lv_coord_t y) {
        lv_obj_set_x(obj, x);
        lv_obj_set_y(obj, y);
        return self();
    }

    T& positionY(lv_coord_t y) {
        lv_obj_set_y(obj, y);
        return self();
    }

    T& positionX(lv_coord_t x) {
        lv_obj_set_x(obj, x);
        return self();
    }

    T& size(int w, int h) {
        lv_obj_set_size(obj, w, h);
        return self();
    }

    T& align(lv_align_t align) {
        lv_obj_set_align(obj, align);
        return self();
    }

    T& bgColor(uint32_t hex) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(hex), 0);
        return self();
    }

    T& noDecor() {
        lv_obj_set_style_outline_width(obj, 0, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        return self();
    }

    T& clickable(bool isClickable) {
        if (isClickable) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        }
        return self();
    }

    T& onClick(lv_event_cb_t cb, void* user = nullptr) {
        lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
        return self();
    }

protected:
    lv_obj_t* obj = nullptr;

private:
    T& self() {
        return static_cast<T&>(*this);
    }
};

#endif