#include "ui/MenuOptions.hpp"
#include "ui/MenuManager.hpp"
#include "ui/MenuTheme.hpp"
#include <raylib.h>

namespace {

struct Button {
    const char* text;
    int x, y, w, h;
    bool hovered;
};

const int BTN_W = 240;
const int BTN_H = 42;
const int BTN_GAP = 12;

bool draw_button(Button& btn) {
    Vector2 mouse = GetMousePosition();
    btn.hovered = CheckCollisionPointRec(mouse, { (float)btn.x, (float)btn.y, (float)btn.w, (float)btn.h });

    MenuTheme::draw_button({ (float)btn.x, (float)btn.y, (float)btn.w, (float)btn.h }, btn.hovered);

    int tw = MeasureText(btn.text, 20);
    int tx = btn.x + (btn.w - tw) / 2;
    int ty = btn.y + (btn.h - 20) / 2;
    DrawText(btn.text, tx, ty, 20, WHITE);

    return btn.hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

} // namespace

void MenuOptions::update(MenuManager& manager) {
}

void MenuOptions::draw(MenuManager& manager) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    MenuTheme::draw_overlay(sw, sh);

    int panel_w = 320;
    int panel_h = 240;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;

    MenuTheme::draw_panel(px, py, panel_w, panel_h);

    const char* title = "Opciones";
    int title_w = MeasureText(title, 26);
    DrawText(title, px + (panel_w - title_w) / 2, py + 24, 26, MenuTheme::col_title);

    int btn_x = px + (panel_w - BTN_W) / 2;
    int start_y = py + 80;

    Button btn_packs = { "Paquetes de Recursos", btn_x, start_y,                          BTN_W, BTN_H };
    Button btn_back  = { "Volver",               btn_x, start_y + BTN_H + BTN_GAP,        BTN_W, BTN_H };

    if (draw_button(btn_packs)) manager.state = MENU_RESOURCE_PACKS;
    if (draw_button(btn_back))  { manager.go_back(); return; }
}
