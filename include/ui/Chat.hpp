#pragma once
#include <string>
#include <vector>
#include <raylib.h>

struct ChatMessage {
    std::string text;
    float time_visible;
};

class Chat {
public:
    bool is_open = false;
    std::string current_input = "";
    std::vector<ChatMessage> history;
    std::vector<std::string> input_history;  // historial de comandos escritos
    int history_index = -1;                  // indice actual en input_history (-1 = nada)

    void update();
    void draw();
    void toggle();
    void add_message(const std::string& msg);
    
    std::string get_pending_command(); 
private:
    std::string pending_command = "";
};
