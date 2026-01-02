#ifndef TUI_H
#define TUI_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"

struct ChatMessage {
    std::string channel;
    std::string username;
    std::string message;
    std::string timestamp;  // Format: [HH:MM]
    bool is_emote;
    bool is_system;
};

struct Channel {
    std::string name;
    std::string topic;
    std::vector<std::string> users;
    std::vector<ChatMessage> messages;
    int unread_count;
    bool is_dm;   // true if this is a direct message conversation
    bool joined;  // true if user has joined the channel (always true for DMs)
};

class TUI {
private:
    std::map<std::string, Channel> channels;
    std::string active_channel;
    std::string current_username;
    std::string status_text;
    
    std::string input_content;
    ftxui::ScreenInteractive screen;
    ftxui::Component main_component;
    ftxui::Component input_component;
    ftxui::Component conversations_container;
    
    // Chat scrolling state
    float chat_scroll_y = 1.0f; // 0.0 = top, 1.0 = bottom
    ftxui::Box chat_box;

    // Conversations scroll state
    float conv_scroll_y = 0.0f; // start at top
    ftxui::Box conv_box;

    // Input cursor management
    int input_cursor_pos = 0;
    
    // Join modal state
    bool show_join_modal = false;
    std::string join_target_input;
    std::string join_password_input;
    ftxui::Component join_modal_component;
    ftxui::Component join_target_input_component;
    ftxui::Component join_password_input_component;
    ftxui::Component join_ok_button;
    ftxui::Component join_cancel_button;
    
    std::function<void(const std::string&)> on_input_callback;
    std::function<void(const std::string& name, const std::string& password, bool is_dm)> on_join_request;
    bool should_exit;
    
public:
    TUI();
    ~TUI();
    
    void init();
    void cleanup();
    void run();
    void exit_loop();
    
    void add_channel(const std::string& name, const std::string& topic = "", bool is_dm = false, bool joined = false);
    void remove_channel(const std::string& name);
    void clear_unjoined_channels();
    void set_active_channel(const std::string& name);
    void set_channel_joined(const std::string& name, bool joined);
    void add_message(const ChatMessage& msg);
    void add_user_to_channel(const std::string& channel, const std::string& username);
    void remove_user_from_channel(const std::string& channel, const std::string& username);
    void update_topic(const std::string& channel, const std::string& topic);
    void set_username(const std::string& username) { current_username = username; }
    void set_status(const std::string& status) { status_text = status; }
    
    // Clear messages for a given channel/DM; if name empty, no-op.
    void clear_channel_messages(const std::string& name);
    
    void render();
    std::string get_active_channel() const { return active_channel; }
    std::string get_first_active_channel() const;
    bool is_active_channel_dm() const {
        auto it = channels.find(active_channel);
        return it != channels.end() && it->second.is_dm;
    }
    
    void set_input_callback(std::function<void(const std::string&)> callback) {
        on_input_callback = callback;
    }
    void set_join_request_callback(std::function<void(const std::string& name, const std::string& password, bool is_dm)> callback) {
        on_join_request = callback;
    }
    
    // Dialog functions
    bool show_login_dialog(std::string& host, int& port, bool& use_ssl, 
                          std::string& username, std::string& password);
    void show_error(const std::string& error);
    
private:
    ftxui::Component build_ui();
    ftxui::Component build_channel_list();
    void refresh_conversations();
    ftxui::Component build_join_modal();
    ftxui::Element render_chat_area(); // returns inner content only (no frame/border)
    ftxui::Element render_user_list();
    ftxui::Color get_color_for_user(const std::string& username);
    bool contains_url(const std::string& text);
    ftxui::Element format_text_with_urls(const std::string& line);
    ftxui::Element format_message(const ChatMessage& msg);
    std::vector<std::string> wrap_text(const std::string& text, int max_width);
};

#endif
