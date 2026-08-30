#pragma once
#include <raylib.h>

namespace MenuTheme {
    inline Color col_panel       = { 22, 26, 33, 230 };
    inline Color col_panel_line  = { 70, 85, 110, 255 };
    inline Color col_btn         = { 35, 42, 55, 255 };
    inline Color col_btn_hover   = { 55, 68, 90, 255 };
    inline Color col_btn_line    = { 80, 100, 130, 255 };
    inline Color col_btn_active  = { 30, 120, 45, 255 };
    inline Color col_btn_active_line = { 80, 180, 90, 255 };
    inline Color col_title       = { 230, 235, 245, 255 };
    inline Color col_desc        = { 170, 180, 195, 255 };
    inline Color col_pack_bg     = { 28, 34, 44, 255 };
    inline Color col_pack_hover  = { 38, 46, 60, 255 };

    inline void draw_button(Rectangle btn, bool hover, Color bg_override = {0,0,0,0}, Color border_override = {0,0,0,0}) {
        Color bg = bg_override.a ? bg_override : (hover ? col_btn_hover : col_btn);
        Color border = border_override.a ? border_override : col_btn_line;
        DrawRectangleRounded(btn, 0.3f, 6, bg);
        DrawRectangleRoundedLines(btn, 0.3f, 6, border);
    }

    inline void draw_panel(int px, int py, int pw, int ph) {
        DrawRectangleRounded({ (float)px, (float)py, (float)pw, (float)ph }, 0.06f, 6, col_panel);
        DrawRectangleRoundedLines({ (float)px, (float)py, (float)pw, (float)ph }, 0.06f, 6, col_panel_line);
    }

    inline void draw_overlay(int sw, int sh) {
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
    }
}
