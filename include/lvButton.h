#ifndef LV_BUTTON_h
#define LV_BUTTON_h

#include "lvBase.h"

class LvButton : public LvBase<LvButton> {
public:
    explicit LvButton(lv_obj_t* parent) {
        obj = lv_btn_create(parent);
    }
};
#endif