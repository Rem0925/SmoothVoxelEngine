#include "MenuPause.hpp"
#include "MenuManager.hpp"
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

Color col_panel      = Color{ 22, 26, 33, 230 };
Color col_panel_line = Color{ 70, 85, 110, 255 };
Color col_btn        = Color{ 35, 42, 55, 255 };
Color col_btn_hover  = Color{ 55, 68, 90, 255 };
Color col_btn_line   = Color{ 80, 100, 130, 255 };
Color col_title      = Color{ 230, 235, 245, 255 };

bool draw_button(Button& btn) {
    Vector2 mouse = GetMousePosition();
    btn.hovered = CheckCollisionPointRec(mouse, { (float)btn.x, (float)btn.y, (float)btn.w, (float)btn.h });

    Color bg = btn.hovered ? col_btn_hover : col_btn;
    DrawRectangleRounded({ (float)btn.x, (float)btn.y, (float)btn.w, (float)btn.h }, 0.3f, 6, bg);
    DrawRectangleRoundedLines({ (float)btn.x, (float)btn.y, (float)btn.w, (float)btn.h }, 0.3f, 6, col_btn_line);

    int tw = MeasureText(btn.text, 20);
    int tx = btn.x + (btn.w - tw) / 2;
    int ty = btn.y + (btn.h - 20) / 2;
    DrawText(btn.text, tx, ty, 20, WHITE);

    return btn.hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

} // namespace

void MenuPause::update(MenuManager& manager) {
    // ESC is handled by main loop to avoid same-frame open+close
}

void MenuPause::draw(MenuManager& manager) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));

    int panel_w = 320;
    int panel_h = 280;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;

    DrawRectangleRounded({ (float)px, (float)py, (float)panel_w, (float)panel_h }, 0.06f, 6, col_panel);
    DrawRectangleRoundedLines({ (float)px, (float)py, (float)panel_w, (float)panel_h }, 0.06f, 6, col_panel_line);

    const char* title = "Juego en Pausa";
    int title_w = MeasureText(title, 26);
    DrawText(title, px + (panel_w - title_w) / 2, py + 24, 26, col_title);

    int btn_x = px + (panel_w - BTN_W) / 2;
    int start_y = py + 80;

    Button btn_resume  = { "Reanudar",            btn_x, start_y,                          BTN_W, BTN_H };
    Button btn_options = { "Opciones",            btn_x, start_y + BTN_H + BTN_GAP,        BTN_W, BTN_H };
    Button btn_quit    = { "Salir del juego",     btn_x, start_y + (BTN_H + BTN_GAP) * 2,  BTN_W, BTN_H };

    if (draw_button(btn_resume))  { manager.close(); return; }
    if (draw_button(btn_options)) manager.state = MENU_OPTIONS;
    if (draw_button(btn_quit))    manager.wants_quit = true;
}
