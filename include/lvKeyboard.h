#ifndef LV_KEYBOARD_h
#define LV_KEYBOARD_h

#include "lvBase.h"

class LvKeyboard : public LvBase<LvKeyboard> {
public:
    explicit LvKeyboard(lv_obj_t* parent) {
        obj = lv_keyboard_create(parent);
    }
    
    explicit LvKeyboard(lv_obj_t* existing, bool) {
        obj = existing;
    }

    LvKeyboard& target(lv_obj_t* ta) {
        lv_keyboard_set_textarea(obj, ta);
        return *this;
    }

    LvKeyboard& show(bool v = true) {
        v ? lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN)
          : lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        return *this;
    }

    LvKeyboard& onEvent(lv_event_cb_t cb, void* user = nullptr) {
        lv_obj_add_event_cb(obj, cb, LV_EVENT_ALL, user);
        return *this;
    }
};
#endif