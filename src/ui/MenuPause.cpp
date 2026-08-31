#include "ui/MenuPause.hpp"
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

void MenuPause::update(MenuManager& manager) {
}

void MenuPause::draw(MenuManager& manager) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    MenuTheme::draw_overlay(sw, sh);

    int panel_w = 320;
    int panel_h = 280;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;

    MenuTheme::draw_panel(px, py, panel_w, panel_h);

    const char* title = "Juego en Pausa";
    int title_w = MeasureText(title, 26);
    DrawText(title, px + (panel_w - title_w) / 2, py + 24, 26, MenuTheme::col_title);

    int btn_x = px + (panel_w - BTN_W) / 2;
    int start_y = py + 80;

    Button btn_resume  = { "Reanudar",            btn_x, start_y,                          BTN_W, BTN_H };
    Button btn_options = { "Opciones",            btn_x, start_y + BTN_H + BTN_GAP,        BTN_W, BTN_H };
    Button btn_quit    = { "Salir del juego",     btn_x, start_y + (BTN_H + BTN_GAP) * 2,  BTN_W, BTN_H };

    if (draw_button(btn_resume))  { manager.close(); return; }
    if (draw_button(btn_options)) manager.state = MENU_OPTIONS;
    if (draw_button(btn_quit))    manager.wants_quit = true;
}
