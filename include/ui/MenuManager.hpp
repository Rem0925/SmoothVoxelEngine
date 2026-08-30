#pragma once
#include <raylib.h>

enum MenuState {
    MENU_CLOSED,
    MENU_PAUSED,
    MENU_OPTIONS,
    MENU_RESOURCE_PACKS
};

class MenuManager {
public:
    MenuState state = MENU_CLOSED;

    void open_pause();
    void close();
    void go_back();

    bool is_paused() const { return state != MENU_CLOSED; }

    void update();
    void draw();

    bool wants_quit = false;
};
