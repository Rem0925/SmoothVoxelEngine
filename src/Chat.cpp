#include "Chat.hpp"

void Chat::update() {
    // Fade out old messages
    for (auto& msg : history) {
        if (msg.time_visible > 0) {
            msg.time_visible -= GetFrameTime();
        }
    }

    if (!is_open) return;

    // Get char input
    int key = GetCharPressed();
    while (key > 0) {
        if ((key >= 32) && (key <= 125)) {
            current_input += (char)key;
        }
        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !current_input.empty()) {
        current_input.pop_back();
    }

    // Historial con flechas arriba/abajo
    if (IsKeyPressed(KEY_UP)) {
        if (!input_history.empty()) {
            if (history_index < (int)input_history.size() - 1) {
                history_index++;
                current_input = input_history[input_history.size() - 1 - history_index];
            }
        }
    }
    if (IsKeyPressed(KEY_DOWN)) {
        if (history_index > 0) {
            history_index--;
            current_input = input_history[input_history.size() - 1 - history_index];
        } else if (history_index == 0) {
            history_index = -1;
            current_input = "";
        }
    }

    if (IsKeyPressed(KEY_ENTER)) {
        if (!current_input.empty()) {
            // Guardar en historial
            input_history.push_back(current_input);
            history_index = -1;
            
            if (current_input[0] == '/') {
                pending_command = current_input;
            } else {
                add_message("<Player> " + current_input);
            }
            current_input = "";
        }
        toggle(); // Close chat after sending
    }
}

void Chat::draw() {
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    
    int chat_x = 10;
    int chat_y = screen_h - 40; // Just above hotbar

    if (is_open) {
        DrawRectangle(chat_x, chat_y - 10, 400, 30, Fade(BLACK, 0.5f));
        DrawText(("> " + current_input + "_").c_str(), chat_x + 5, chat_y - 5, 20, WHITE);
    }

    // Draw history
    int hist_y = chat_y - 40;
    for (int i = (int)history.size() - 1; i >= 0; --i) {
        if (is_open || history[i].time_visible > 0) {
            float alpha = is_open ? 1.0f : std::min(1.0f, history[i].time_visible);
            DrawText(history[i].text.c_str(), chat_x + 5, hist_y, 20, Fade(WHITE, alpha));
            hist_y -= 25;
            if (hist_y < 100) break; // Limit height
        }
    }
}

void Chat::toggle() {
    is_open = !is_open;
    if (is_open) {
        EnableCursor();
        // Clear queue to avoid capturing the key that opened the chat
        while (GetCharPressed() > 0) {} 
    } else {
        DisableCursor();
    }
}

void Chat::add_message(const std::string& msg) {
    history.push_back({msg, 5.0f}); // visible for 5 seconds
}

std::string Chat::get_pending_command() {
    std::string cmd = pending_command;
    pending_command = "";
    return cmd;
}
