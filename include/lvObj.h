#ifndef LV_OBJ_h
#define LV_OBJ_h

#include "lvBase.h"

class LvObj : public LvBase<LvObj> {
public:
    explicit LvObj(lv_obj_t* parent) {
        obj = lv_obj_create(parent);
    }

    explicit LvObj(lv_obj_t* existing, bool) {
        obj = existing;
    }

    LvObj& flexFlow(lv_flex_flow_t flow) {
        lv_obj_set_flex_flow(obj, flow);
        return *this;
    }

    LvObj& flexAlign(
        lv_flex_align_t main,
        lv_flex_align_t cross,
        lv_flex_align_t track = LV_FLEX_ALIGN_START
    ) {
        lv_obj_set_flex_align(obj, main, cross, track);
        return *this;
    }

    LvObj& bringToFront() {
        lv_obj_move_foreground(obj);
        return *this;
    }

    LvObj& radius(int r) {
        lv_obj_set_style_radius(obj, r, 0);
        return *this;
    }

};

#endif