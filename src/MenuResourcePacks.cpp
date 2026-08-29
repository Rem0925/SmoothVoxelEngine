#include "MenuResourcePacks.hpp"
#include "MenuManager.hpp"
#include <raylib.h>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

struct PackInfo {
    std::string dir_name;
    std::string display_name;
    std::string description;
    std::string path;
    Texture2D icon = { 0 };
    bool selected = false;
};

namespace {

const std::string PACKS_DIR = "resourcepacks";
const std::string SELECTED_FILE = "resourcepacks/selected.txt";
std::vector<PackInfo> packs;
int scroll_offset = 0;
bool initialized = false;

Color col_panel      = Color{ 22, 26, 33, 230 };
Color col_panel_line = Color{ 70, 85, 110, 255 };
Color col_btn        = Color{ 35, 42, 55, 255 };
Color col_btn_hover  = Color{ 55, 68, 90, 255 };
Color col_btn_line   = Color{ 80, 100, 130, 255 };
Color col_btn_active = Color{ 30, 120, 45, 255 };
Color col_btn_active_line = Color{ 80, 180, 90, 255 };
Color col_title      = Color{ 230, 235, 245, 255 };
Color col_desc       = Color{ 170, 180, 195, 255 };
Color col_pack_bg    = Color{ 28, 34, 44, 255 };
Color col_pack_hover = Color{ 38, 46, 60, 255 };

std::string strip_minecraft_formatting(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if ((unsigned char)text[i] == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA7) {
            i += 1;
            continue;
        }
        result += text[i];
    }
    return result;
}

std::string read_pack_mcmeta(const std::string& pack_path, std::string& description) {
    std::string mcmeta_path = pack_path + "/pack.mcmeta";
    std::ifstream f(mcmeta_path);
    if (!f.is_open()) return "";

    try {
        json j;
        f >> j;
        if (j.contains("pack")) {
            if (j["pack"].contains("description")) {
                auto& desc = j["pack"]["description"];
                if (desc.is_string()) description = desc.get<std::string>();
                else if (desc.is_array() && !desc.empty()) {
                    for (auto& part : desc) {
                        if (part.is_string()) description += part.get<std::string>();
                    }
                }
            }
        }
    } catch (...) {}

    description = strip_minecraft_formatting(description);
    std::string name = fs::path(pack_path).filename().string();
    return name;
}

void load_selected() {
    std::ifstream f(SELECTED_FILE);
    if (!f.is_open()) return;

    std::string selected_name;
    if (std::getline(f, selected_name)) {
        for (auto& p : packs) {
            p.selected = (p.dir_name == selected_name);
        }
    }
}

void save_selected() {
    for (auto& p : packs) {
        if (p.selected) {
            std::ofstream f(SELECTED_FILE);
            if (f.is_open()) f << p.dir_name;
            return;
        }
    }
    fs::remove(SELECTED_FILE);
}

bool draw_button_at(const char* text, int x, int y, int w, int h, bool active = false) {
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, { (float)x, (float)y, (float)w, (float)h });

    Color bg, border;
    if (active) {
        bg = col_btn_active;
        border = col_btn_active_line;
    } else {
        bg = hovered ? col_btn_hover : col_btn;
        border = col_btn_line;
    }

    DrawRectangleRounded({ (float)x, (float)y, (float)w, (float)h }, 0.3f, 6, bg);
    DrawRectangleRoundedLines({ (float)x, (float)y, (float)w, (float)h }, 0.3f, 6, border);

    int tw = MeasureText(text, 16);
    int tx = x + (w - tw) / 2;
    int ty = y + (h - 16) / 2;
    DrawText(text, tx, ty, 16, WHITE);

    return hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

} // namespace

void MenuResourcePacks::save_selection() {
    save_selected();
}

std::string MenuResourcePacks::get_active_pack_path() {
    for (auto& p : packs) {
        if (p.selected) return p.path;
    }
    return "";
}

void MenuResourcePacks::scan_packs() {
    packs.clear();

    fs::create_directories(PACKS_DIR);

    for (auto& entry : fs::directory_iterator(PACKS_DIR)) {
        if (!entry.is_directory()) continue;

        std::string mcmeta = entry.path().string() + "/pack.mcmeta";
        if (!fs::exists(mcmeta)) continue;

        PackInfo info;
        info.path = entry.path().string();
        info.dir_name = entry.path().filename().string();

        std::string desc;
        info.display_name = read_pack_mcmeta(info.path, desc);
        info.description = desc;

        std::string icon_path = info.path + "/pack.png";
        if (fs::exists(icon_path)) {
            info.icon = LoadTexture(icon_path.c_str());
            SetTextureFilter(info.icon, TEXTURE_FILTER_BILINEAR);
        }

        packs.push_back(info);
    }

    load_selected();
    initialized = true;

    std::cout << "[ResourcePacks] Found " << packs.size() << " pack(s)" << std::endl;
}

void MenuResourcePacks::update() {
    // ESC is handled by main loop
}

void MenuResourcePacks::draw(MenuManager& manager) {
    if (!initialized) scan_packs();

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));

    int panel_w = 500;
    int panel_h = 420;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;

    DrawRectangleRounded({ (float)px, (float)py, (float)panel_w, (float)panel_h }, 0.06f, 6, col_panel);
    DrawRectangleRoundedLines({ (float)px, (float)py, (float)panel_w, (float)panel_h }, 0.06f, 6, col_panel_line);

    const char* title = "Paquetes de Recursos";
    int title_w = MeasureText(title, 26);
    DrawText(title, px + (panel_w - title_w) / 2, py + 18, 26, col_title);

    if (packs.empty()) {
        const char* empty_msg = "No se encontraron paquetes en 'resourcepacks/'";
        int msg_w = MeasureText(empty_msg, 16);
        DrawText(empty_msg, px + (panel_w - msg_w) / 2, py + 80, 16, col_desc);

        const char* hint = "Coloca una carpeta de paquete en la carpeta resourcepacks/";
        int hint_w = MeasureText(hint, 14);
        DrawText(hint, px + (panel_w - hint_w) / 2, py + 110, 14, Fade(col_desc, 0.6f));
    } else {
        int list_x = px + 12;
        int list_y = py + 58;
        int list_w = panel_w - 24;
        int list_h = panel_h - 115;
        int item_h = 72;
        int icon_size = 48;
        int icon_pad = 10;

        BeginScissorMode(list_x, list_y, list_w, list_h);
        for (int i = 0; i < (int)packs.size(); ++i) {
            int iy = list_y + i * (item_h + 6) - scroll_offset;
            if (iy + item_h < list_y || iy > list_y + list_h) continue;

            auto& p = packs[i];
            Vector2 mouse = GetMousePosition();
            bool hovered = CheckCollisionPointRec(mouse, { (float)list_x, (float)iy, (float)list_w, (float)item_h });
            Color bg = hovered ? col_pack_hover : col_pack_bg;
            DrawRectangleRounded({ (float)list_x, (float)iy, (float)list_w, (float)item_h }, 0.15f, 4, bg);
            DrawRectangleRoundedLines({ (float)list_x, (float)iy, (float)list_w, (float)item_h }, 0.15f, 4,
                p.selected ? col_btn_active_line : Color{ 60, 70, 85, 255 });

            int ix = list_x + icon_pad;
            int iy_icon = iy + (item_h - icon_size) / 2;
            if (p.icon.id != 0) {
                DrawTexturePro(p.icon,
                    { 0, 0, (float)p.icon.width, (float)p.icon.height },
                    { (float)ix, (float)iy_icon, (float)icon_size, (float)icon_size },
                    { 0, 0 }, 0.0f, WHITE);
            } else {
                DrawRectangle(ix, iy_icon, icon_size, icon_size, Color{ 50, 55, 65, 255 });
                DrawText("?", ix + icon_size / 2 - 5, iy_icon + icon_size / 2 - 9, 18, col_desc);
            }

            int text_x = ix + icon_size + 12;
            int text_max_w = list_w - (text_x - list_x) - 80;
            DrawText(p.display_name.c_str(), text_x, iy + 12, 16, WHITE);

            if (!p.description.empty()) {
                DrawText(p.description.c_str(), text_x, iy + 34, 12, col_desc);
            }

            const char* btn_text = p.selected ? "Activo" : "Activar";
            bool btn_clicked = draw_button_at(btn_text, list_x + list_w - 86, iy + (item_h - 28) / 2, 74, 28, p.selected);
            if (btn_clicked) {
                if (p.selected) {
                    p.selected = false;
                } else {
                    for (auto& pack : packs) pack.selected = false;
                    p.selected = true;
                }
            }
        }
        EndScissorMode();

        if (list_h < (int)packs.size() * (item_h + 6)) {
            int total_h = (int)packs.size() * (item_h + 6);
            int bar_h = (int)((float)list_h / total_h * list_h);
            int bar_y = list_y + (int)((float)scroll_offset / total_h * list_h);
            DrawRectangle(px + panel_w - 18, list_y, 4, list_h, Fade(BLACK, 0.3f));
            DrawRectangle(px + panel_w - 19, bar_y, 6, bar_h, Fade(WHITE, 0.4f));
        }
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && packs.size() > 0) {
        int total_h = (int)packs.size() * 78;
        int list_h = panel_h - 115;
        scroll_offset -= (int)(wheel * 30);
        scroll_offset = std::clamp(scroll_offset, 0, std::max(0, total_h - list_h));
    }

    int btn_x = px + (panel_w - 180) / 2;
    int btn_y = py + panel_h - 48;
    if (draw_button_at("Volver", btn_x, btn_y, 180, 36, false)) {
        save_selected();
        manager.go_back();
    }
}
