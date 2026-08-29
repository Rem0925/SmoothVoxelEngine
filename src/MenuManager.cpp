#include "MenuManager.hpp"
#include "MenuPause.hpp"
#include "MenuOptions.hpp"
#include "MenuResourcePacks.hpp"
#include <raylib.h>

void MenuManager::open_pause() {
    state = MENU_PAUSED;
    wants_quit = false;
}

void MenuManager::close() {
    state = MENU_CLOSED;
    DisableCursor();
}

void MenuManager::go_back() {
    switch (state) {
        case MENU_RESOURCE_PACKS:
            MenuResourcePacks::save_selection();
            state = MENU_PAUSED;
            break;
        case MENU_OPTIONS:
            state = MENU_PAUSED;
            break;
        case MENU_PAUSED:
            state = MENU_CLOSED;
            DisableCursor();
            break;
        default:
            break;
    }
}

void MenuManager::update() {
    switch (state) {
        case MENU_PAUSED: MenuPause::update(*this); break;
        case MENU_OPTIONS: MenuOptions::update(*this); break;
        case MENU_RESOURCE_PACKS: MenuResourcePacks::update(); break;
        default: break;
    }
}

void MenuManager::draw() {
    switch (state) {
        case MENU_PAUSED: MenuPause::draw(*this); break;
        case MENU_OPTIONS: MenuOptions::draw(*this); break;
        case MENU_RESOURCE_PACKS: MenuResourcePacks::draw(*this); break;
        default: break;
    }
}
