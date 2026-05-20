#ifndef LV_DROP_DOWN_h
#define LV_DROP_DOWN_h

#include "lvBase.h"

class LvDropdown : public LvBase<LvDropdown> {
public:
    explicit LvDropdown(lv_obj_t* parent) {
        obj = lv_dropdown_create(parent);
    }

    LvDropdown& options(const char* opts) {
        lv_dropdown_set_options(obj, opts);
        return *this;
    }

    LvDropdown& selected(uint16_t idx) {
        lv_dropdown_set_selected(obj, idx);
        return *this;
    }
};

#endif
