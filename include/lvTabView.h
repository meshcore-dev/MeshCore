#ifndef LV_TAB_VIEW_h
#define LV_TAB_VIEW_h

#include "lvBase.h"

class LvTabView : public LvBase<LvTabView> {
public:
    explicit LvTabView(lv_obj_t* parent,
                       lv_dir_t dir = LV_DIR_TOP,
                       int tab_h = 50) {
        obj = lv_tabview_create(parent, dir, tab_h);
    }

    lv_obj_t* addTab(const char* name) {
        return lv_tabview_add_tab(obj, name);
    }

    LvTabView& contentNoScroll() {
        lv_obj_clear_flag(
            lv_tabview_get_content(obj),
            LV_OBJ_FLAG_SCROLLABLE
        );
        return *this;
    }

    LvTabView& tabBtnBg(uint32_t hex) {
        lv_obj_set_style_bg_color(
            lv_tabview_get_tab_btns(obj),
            lv_color_hex(hex),
            0
        );
        return *this;
    }

    LvTabView& tabBtnText(uint32_t hex, const lv_font_t* f = nullptr) {
        lv_obj_set_style_text_color(
            lv_tabview_get_tab_btns(obj),
            lv_color_hex(hex),
            LV_PART_ITEMS
        );
        if (f) {
            lv_obj_set_style_text_font(
                lv_tabview_get_tab_btns(obj),
                f,
                LV_PART_ITEMS
            );
        }
        return *this;
    }
};
#endif