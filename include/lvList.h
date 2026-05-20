#ifndef LV_LIST_h
#define LV_LIST_h

#include "lvBase.h"

class LvList : public LvBase<LvList> {
public:
    explicit LvList(lv_obj_t* parent) {
        obj = lv_list_create(parent);
    }

    LvList& transparent() {
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(obj, 0, 0);

        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_ITEMS);
        lv_obj_set_style_border_width(obj, 0, LV_PART_ITEMS);

        return *this;
    }

    LvList& padRow(int v) {
        lv_obj_set_style_pad_row(obj, v, 0);
        return *this;
    }
};

#endif