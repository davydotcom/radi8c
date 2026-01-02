#include "TUI.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include <algorithm>
#include <sstream>

using namespace ftxui;

TUI::TUI() : screen(ScreenInteractive::Fullscreen()), 
             should_exit(false) {}

TUI::~TUI() {
    cleanup();
}

void TUI::init() {
    main_component = build_ui();
}

void TUI::cleanup() {
    // FTXUI handles cleanup automatically
}

void TUI::run() {
    screen.Loop(main_component);
}

void TUI::exit_loop() {
    should_exit = true;
    screen.Exit();
}

void TUI::add_channel(const std::string& name, const std::string& topic, bool is_dm, bool joined) {
    auto it = channels.find(name);
    if (it == channels.end()) {
        Channel ch;
        ch.name = name;
        ch.topic = topic;
        ch.unread_count = 0;
        ch.is_dm = is_dm;
        ch.joined = is_dm ? true : joined;
        channels[name] = ch;
        if (active_channel.empty() && (ch.joined || ch.is_dm)) active_channel = name;
    } else {
        // Update topic and flags but never downgrade joined=true
        it->second.topic = topic.empty() ? it->second.topic : topic;
        it->second.is_dm = it->second.is_dm || is_dm;
        if (joined) it->second.joined = true;
    }
    refresh_conversations();
}

void TUI::set_channel_joined(const std::string& name, bool j) {
    auto it = channels.find(name);
    if (it != channels.end()) {
        it->second.joined = j || it->second.is_dm;
    }
    refresh_conversations();
}

void TUI::remove_channel(const std::string& name) {
    channels.erase(name);
    if (active_channel == name) {
        // Switch to the first active (joined) channel
        active_channel = get_first_active_channel();
    }
    refresh_conversations();
}

void TUI::clear_unjoined_channels() {
    // Remove all unjoined, non-DM channels (browse list)
    auto it = channels.begin();
    while (it != channels.end()) {
        if (!it->second.joined && !it->second.is_dm) {
            it = channels.erase(it);
        } else {
            ++it;
        }
    }
    refresh_conversations();
}

std::string TUI::get_first_active_channel() const {
    // Priority: joined channels or DMs
    for (const auto& [name, ch] : channels) {
        if (ch.joined || ch.is_dm) {
            return name;
        }
    }
    // Fallback: any channel
    if (!channels.empty()) {
        return channels.begin()->first;
    }
    return "";
}

std::vector<std::string> TUI::get_joined_channels() const {
    std::vector<std::string> joined;
    for (const auto& [name, ch] : channels) {
        // Only include actual joined channels, not DMs
        if (ch.joined && !ch.is_dm) {
            joined.push_back(name);
        }
    }
    return joined;
}

void TUI::set_active_channel(const std::string& name) {
    if (channels.find(name) != channels.end()) {
        active_channel = name;
        channels[name].unread_count = 0;
        // Reset scroll to bottom when switching channels
        chat_scroll_y = 1.0f;
    }
    refresh_conversations();
}

void TUI::add_message(const ChatMessage& msg) {
    if (channels.find(msg.channel) != channels.end()) {
        channels[msg.channel].messages.push_back(msg);
        
        if (msg.channel != active_channel) {
            channels[msg.channel].unread_count++;
        } else {
            // Autoscroll to bottom for active channel
            chat_scroll_y = 1.0f;
        }
        // Update list to reflect unread badges
        refresh_conversations();
    }
}

void TUI::add_user_to_channel(const std::string& channel, const std::string& username) {
    if (channels.find(channel) != channels.end()) {
        auto& users = channels[channel].users;
        if (std::find(users.begin(), users.end(), username) == users.end()) {
            users.push_back(username);
            std::sort(users.begin(), users.end());
        }
    }
}

void TUI::remove_user_from_channel(const std::string& channel, const std::string& username) {
    if (channels.find(channel) != channels.end()) {
        auto& users = channels[channel].users;
        users.erase(std::remove(users.begin(), users.end(), username), users.end());
    }
}

void TUI::update_topic(const std::string& channel, const std::string& topic) {
    if (channels.find(channel) != channels.end()) {
        channels[channel].topic = topic;
    }
}

void TUI::clear_channel_messages(const std::string& name) {
    auto it = channels.find(name);
    if (it != channels.end()) {
        it->second.messages.clear();
        it->second.unread_count = 0;
        // Keep channel, topic, users intact; just clear the scroll to bottom
        chat_scroll_y = 1.0f;
        render();
    }
}

Component TUI::build_join_modal() {
    // Inputs
    InputOption target_opt;
    join_target_input_component = Input(&join_target_input, "channel or @user", target_opt);
    
    InputOption pw_opt;
    pw_opt.password = true;
    join_password_input_component = Input(&join_password_input, "password (optional)", pw_opt);
    
    // Buttons
    ButtonOption ok_opt = ButtonOption::Simple();
    ok_opt.transform = [](const EntryState& s) {
        auto e = text(s.label) | center;
        if (s.focused) e = e | inverted;
        return e | border;
    };
    join_ok_button = Button("Join", [this]() {
        std::string target = join_target_input;
        // trim spaces
        target.erase(0, target.find_first_not_of(" \t"));
        target.erase(target.find_last_not_of(" \t") + 1);
        if (target.empty()) {
            status_text = "Enter a channel or @user";
            return;
        }
        if (!on_join_request) {
            // Fallback: just update UI
            if (target[0] == '@') {
                std::string user = target.substr(1);
                add_channel(user, "", true);
                set_active_channel(user);
            } else {
                std::string chan = target[0] == '#' ? target.substr(1) : target;
                add_channel(chan, "", false);
                set_active_channel(chan);
            }
        } else {
            if (target[0] == '@') {
                std::string user = target.substr(1);
                add_channel(user, "", true);
                set_active_channel(user);
                on_join_request(user, "", true);
            } else {
                std::string chan = target[0] == '#' ? target.substr(1) : target;
                add_channel(chan, "", false);
                set_active_channel(chan);
                on_join_request(chan, join_password_input, false);
            }
        }
        // Close modal and reset
        show_join_modal = false;
        join_target_input.clear();
        join_password_input.clear();
    }, ok_opt);
    
    ButtonOption cancel_opt = ButtonOption::Simple();
    cancel_opt.transform = [](const EntryState& s) {
        auto e = text(s.label) | center;
        if (s.focused) e = e | inverted;
        return e | border;
    };
    join_cancel_button = Button("Cancel", [this]() {
        show_join_modal = false;
        join_target_input.clear();
        join_password_input.clear();
    }, cancel_opt);
    
    auto buttons = Container::Horizontal({ join_ok_button, join_cancel_button });
    auto modal_container = Container::Vertical({ join_target_input_component, join_password_input_component, buttons });
    
    return Renderer(modal_container, [this]() {
        auto title = text("Join conversation") | bold | center;
        auto target_row = hbox({ text("Target: ") | dim, join_target_input_component->Render() | flex });
        auto pw_row = hbox({ text("Password: ") | dim, join_password_input_component->Render() | flex });
        auto btn_row = hbox({ filler(), join_ok_button->Render(), text(" "), join_cancel_button->Render(), filler() });
        auto panel = vbox({ title, separator(), target_row, pw_row, separator(), btn_row }) | border | size(WIDTH, EQUAL, 50) | size(HEIGHT, EQUAL, 10);
        return panel | center;
    });
}

Color TUI::get_color_for_user(const std::string& username) {
    // Hash username to get consistent color
    size_t hash = std::hash<std::string>{}(username);
    
    std::vector<Color> colors = {
        Color::Cyan,
        Color::Green,
        Color::Yellow,
        Color::Magenta,
        Color::Blue,
        Color::CyanLight
    };
    
    return colors[hash % colors.size()];
}

bool TUI::contains_url(const std::string& text) {
    return text.find("http://") != std::string::npos ||
           text.find("https://") != std::string::npos ||
           text.find("www.") != std::string::npos;
}

Element TUI::format_text_with_urls(const std::string& line) {
    // Simple URL detection: look for http://, https://, or www.
    // Split text into segments, underlining URL segments
    Elements segments;
    std::string remaining = line;
    
    while (!remaining.empty()) {
        size_t http_pos = remaining.find("http://");
        size_t https_pos = remaining.find("https://");
        size_t www_pos = remaining.find("www.");
        
        // Find earliest URL marker
        size_t url_start = std::string::npos;
        if (http_pos != std::string::npos) url_start = http_pos;
        if (https_pos != std::string::npos && (url_start == std::string::npos || https_pos < url_start)) url_start = https_pos;
        if (www_pos != std::string::npos && (url_start == std::string::npos || www_pos < url_start)) url_start = www_pos;
        
        if (url_start == std::string::npos) {
            // No more URLs, add remaining text
            if (!remaining.empty()) {
                segments.push_back(text(remaining));
            }
            break;
        }
        
        // Add text before URL
        if (url_start > 0) {
            segments.push_back(text(remaining.substr(0, url_start)));
        }
        
        // Find end of URL (space or end of string)
        size_t url_end = remaining.find(' ', url_start);
        if (url_end == std::string::npos) url_end = remaining.length();
        
        // Add URL as underlined
        std::string url = remaining.substr(url_start, url_end - url_start);
        segments.push_back(text(url) | underlined);
        
        // Continue with remaining text
        remaining = remaining.substr(url_end);
    }
    
    if (segments.empty()) {
        return text("");
    }
    
    return hbox(segments);
}

std::vector<std::string> TUI::wrap_text(const std::string& text, int max_width) {
    std::vector<std::string> lines;
    if (max_width <= 0) max_width = 80; // fallback
    
    std::string current_line;
    std::istringstream words(text);
    std::string word;
    
    while (words >> word) {
        if (current_line.empty()) {
            current_line = word;
        } else if (static_cast<int>(current_line.size() + 1 + word.size()) <= max_width) {
            current_line += " " + word;
        } else {
            lines.push_back(current_line);
            current_line = word;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    
    return lines;
}

Element TUI::format_message(const ChatMessage& msg) {
    // Estimate available width: typical terminal is ~80-120 cols, minus left panel (24), right panel (22), borders
    // Let's use a safe estimate of 60 characters for message content
    const int message_width = 80;
    
    if (msg.is_system) {
        std::string sys_text = "[" + msg.username + "] " + msg.message;
        auto wrapped = wrap_text(sys_text, message_width);
        Elements lines;
        for (const auto& line : wrapped) {
            lines.push_back(text(line) | color(Color::Red));
        }
        return vbox(lines);
    } else if (msg.is_emote) {
        std::string emote_text = msg.timestamp + " (" + msg.username + " " + msg.message + ")";
        auto wrapped = wrap_text(emote_text, message_width);
        Elements lines;
        for (const auto& line : wrapped) {
            lines.push_back(text(line) | italic | color(Color::GreenLight));
        }
        return vbox(lines);
    } else {
        // Normal message with timestamp, username, and wrapped content
        std::string prefix = msg.timestamp + " " + msg.username + ": ";
        std::string full_text = prefix + msg.message;
        auto wrapped = wrap_text(full_text, message_width);
        
        Elements lines;
        for (const auto& line : wrapped) {
            // Check if this line contains a URL and format accordingly
            if (contains_url(line)) {
                lines.push_back(format_text_with_urls(line));
            } else {
                lines.push_back(text(line));
            }
        }
        return vbox(lines);
    }
}

Component TUI::build_channel_list() {
    // Create container once and refresh its children whenever state changes
    if (!conversations_container) {
        conversations_container = Container::Vertical({});
    }
    refresh_conversations();
    
    // Wrap with a renderer to add scrolling, scrollbar, and border
    auto wrapper = Renderer(conversations_container, [this]() {
        auto inner = conversations_container->Render() | vscroll_indicator | focusPositionRelative(0.0f, conv_scroll_y) | frame | reflect(conv_box);
        return vbox({ inner }) | border;
    });
    return wrapper;
}

void TUI::refresh_conversations() {
    Components channel_buttons;
    Components dm_buttons;
    
    int chan_index = 1;
    int dm_index = 1;
    
    // Build sorted lists: joined channels (non-DM), DMs (always joined), and unjoined channels
    std::vector<std::string> joined_channels;
    std::vector<std::string> unjoined_channels;
    std::vector<std::string> dm_list;
    for (auto& kv : channels) {
        const auto& ch = kv.second;
        if (ch.is_dm) {
            dm_list.push_back(kv.first);
        } else if (ch.joined) {
            joined_channels.push_back(kv.first);
        } else {
            unjoined_channels.push_back(kv.first);
        }
    }
    auto by_name = [](const std::string& a, const std::string& b){ return a < b; };
    std::sort(joined_channels.begin(), joined_channels.end(), by_name);
    std::sort(unjoined_channels.begin(), unjoined_channels.end(), by_name);
    std::sort(dm_list.begin(), dm_list.end(), by_name);

    Components joined_buttons;
    Components dm_buttons_local;
    Components browse_buttons;

    int idx = 1;
    // Joined channels buttons
    for (const auto& name : joined_channels) {
        const auto& ch = channels[name];
        int unread = ch.unread_count;
        std::string label = "[" + std::to_string(idx++) + "] #" + name + (unread>0? (" ("+std::to_string(unread)+")") : "");
        ButtonOption opt = ButtonOption::Simple();
        opt.transform = [this, name](const EntryState& s) {
            auto elem = text(s.label);
            if (name == active_channel) elem = elem | inverted | bold;
            return elem;
        };
        auto btn = Button(label, [this, name]() { set_active_channel(name); }, opt);
        joined_buttons.push_back(btn);
    }

    // DM buttons
    for (const auto& name : dm_list) {
        const auto& ch = channels[name];
        int unread = ch.unread_count;
        std::string label = "[" + std::to_string(idx++) + "] @" + name + (unread>0? (" ("+std::to_string(unread)+")") : "");
        ButtonOption opt = ButtonOption::Simple();
        opt.transform = [this, name](const EntryState& s) {
            auto elem = hbox({ text("│ ") | dim, text(s.label) });
            if (name == active_channel) elem = elem | inverted | bold;
            return elem;
        };
        auto btn = Button(label, [this, name]() { set_active_channel(name); }, opt);
        dm_buttons_local.push_back(btn);
    }

    // Unjoined browse list (dim, click to open join modal prefilled)
    for (const auto& name : unjoined_channels) {
        const auto& ch = channels[name];
        int unread = ch.unread_count;
        std::string label = "[" + std::to_string(idx++) + "] #" + name + (unread>0? (" ("+std::to_string(unread)+")") : "");
        ButtonOption opt = ButtonOption::Simple();
        opt.transform = [](const EntryState& s) {
            auto elem = text(s.label) | dim;
            if (s.focused) elem = elem | inverted; // highlight focus but keep dim
            return elem;
        };
        auto btn = Button(label, [this, name]() {
            // Prefill modal for joining this channel
            join_target_input = name;
            join_password_input.clear();
            show_join_modal = true;
        }, opt);
        browse_buttons.push_back(btn);
    }

    // If nothing is active and we have joined items, keep behavior; else unchanged
    if (active_channel.empty()) {
        if (!joined_channels.empty()) active_channel = joined_channels.front();
        else if (!dm_list.empty()) active_channel = dm_list.front();
    }

    // Rebuild children of conversations_container dynamically
    conversations_container->DetachAllChildren();

    // Header
    conversations_container->Add(Renderer([](){ return text("Conversations") | bold | center; }));
    conversations_container->Add(Renderer([](){ return separator(); }));

    if (!joined_buttons.empty()) {
        conversations_container->Add(Renderer([]() { return text("CHANNELS") | dim; }));
        for (auto& btn : joined_buttons) conversations_container->Add(btn);
    }

    if (!dm_buttons_local.empty()) {
        if (!joined_buttons.empty()) conversations_container->Add(Renderer([]() { return separator(); }));
        conversations_container->Add(Renderer([]() { return text("DIRECT MESSAGES") | dim; }));
        for (auto& btn : dm_buttons_local) conversations_container->Add(btn);
    }

    if (!browse_buttons.empty()) {
        conversations_container->Add(Renderer([]() { return separator(); }));
        conversations_container->Add(Renderer([]() { return text("BROWSE") | dim; }));
        for (auto& btn : browse_buttons) conversations_container->Add(btn);
    }

    // Join button at bottom with clear label style
    conversations_container->Add(Renderer([](){ return separator(); }));
    ButtonOption join_opt = ButtonOption::Simple();
    join_opt.transform = [](const EntryState& s) {
        auto e = text(s.label) | center;
        if (s.focused) e = e | inverted;
        return e | border;
    };
    conversations_container->Add(Button("Join…", [this]() {
        show_join_modal = true;
    }, join_opt));

    if (joined_buttons.empty() && dm_buttons_local.empty() && browse_buttons.empty()) {
        conversations_container->Add(Renderer([]() { return text("No conversations") | dim | center; }));
    }

    // Ensure UI updates
    render();
}

Element TUI::render_chat_area() {
    // Return inner content only; outer frame/border applied in main renderer.
    if (active_channel.empty() || channels.find(active_channel) == channels.end()) {
        return vbox({ text("No Active Channel") | center | bold });
    }
    
    Channel& ch = channels[active_channel];
    
    // Header with topic
    std::string header = active_channel;
    if (!ch.topic.empty()) {
        header += " - " + ch.topic;
    }
    
    Elements message_elements;
    message_elements.push_back(text(header) | bold | center);
    message_elements.push_back(separator());
    
    for (const auto& msg : ch.messages) {
        message_elements.push_back(format_message(msg));
    }
    
    return vbox(message_elements);
}

Element TUI::render_user_list() {
    Elements user_elements;
    user_elements.push_back(text("Users") | bold | center);
    user_elements.push_back(separator());
    
    if (!active_channel.empty() && channels.find(active_channel) != channels.end()) {
        Channel& ch = channels[active_channel];
        for (const auto& user : ch.users) {
            auto elem = text(user) | color(get_color_for_user(user));
            user_elements.push_back(elem);
        }
    }
    
    return vbox(user_elements) | border;
}

Component TUI::build_ui() {
    // Input component
    InputOption input_option;
    input_option.multiline = false; // Enter sends by default
    input_option.cursor_position = &input_cursor_pos;
    // Hide Input's own rendering to avoid double-draw; we render a wrapped preview ourselves
    input_option.transform = [](InputState) { return emptyElement(); };
    input_option.on_change = [this]() { input_cursor_pos = static_cast<int>(input_content.size()); render(); };  // recompute input height live
    input_option.on_enter = [this]() {
        if (!input_content.empty() && on_input_callback) {
            on_input_callback(input_content);
            input_content.clear();
            input_cursor_pos = 0;
            chat_scroll_y = 1.0f;
            render();
        }
    };
    
    input_component = Input(&input_content, "> ", input_option);
    
    // Build channel list component
    auto channel_list = build_channel_list();
    
    // Build join modal component
    join_modal_component = build_join_modal();
    auto maybe_modal = Maybe(join_modal_component, &show_join_modal);
    
    // Container with all components (include modal so it can receive focus)
    auto container = Container::Horizontal({
        channel_list,
        input_component,
        maybe_modal,
    });
    
    // Give initial focus to the input box
    input_component->TakeFocus();

    // Main renderer
    auto renderer = Renderer(container, [this, channel_list]() {
        auto left = channel_list->Render() | size(WIDTH, EQUAL, 24);
        auto center_inner = render_chat_area();
        auto center = center_inner | vscroll_indicator | focusPositionRelative(0.0f, chat_scroll_y) | frame | reflect(chat_box) | border | flex;
        auto right = render_user_list() | size(WIDTH, EQUAL, 22);
        
        auto status = hbox({
            text(status_text) | inverted | flex,
        });
        
        // Visual preview: wrapped paragraph of the input content.
        auto input_visual = hbox({
            text("> "),
            paragraph(input_content) | flex,
        });
        // Transparent overlay to focus the input when clicked anywhere on the visual area.
        auto focus_proxy = CatchEvent(Renderer([](){ return emptyElement(); }), [this](Event e){
            if (e.is_mouse() && e.mouse().button == Mouse::Left && e.mouse().motion == Mouse::Pressed) {
                input_component->TakeFocus();
                return true;
            }
            return false;
        });
        // Hidden control to capture keystrokes; it draws nothing due to transform above
        auto input_control = input_component->Render();
        auto input_box = dbox({ input_visual | border, focus_proxy->Render(), input_control });
        
        auto base = vbox({
            hbox({left, center, right}) | flex,
            input_box,
            status | size(HEIGHT, EQUAL, 1),
        });
        
        if (show_join_modal) {
            // Overlay modal centered without decorators to avoid operator issues
            auto overlay = vbox({
                filler(),
                hbox({ filler(), join_modal_component->Render() | clear_under, filler() }),
                filler(),
            });
            return dbox({ base, overlay });
        }
        return base;
    });
    
    // Catch quit, navigation, and chat scroll events
    auto component_with_keys = CatchEvent(renderer, [this](Event event) {
        if (event == Event::Escape || event == Event::CtrlC) {
            exit_loop();
            return true;
        }
        
        // Always treat Enter as send for the main input (unless a modal is open)
        if (!show_join_modal && event == Event::Return) {
            if (on_input_callback) {
                on_input_callback(input_content);
                input_content.clear();
                input_cursor_pos = 0;
                chat_scroll_y = 1.0f;
                render();
            }
            return true;
        }
        
        // Scroll chat with PageUp/PageDown/Home/End
        if (event == Event::PageUp) {
            chat_scroll_y = std::max(0.0f, chat_scroll_y - 0.2f);
            render();
            return true;
        }
        if (event == Event::PageDown) {
            chat_scroll_y = std::min(1.0f, chat_scroll_y + 0.2f);
            render();
            return true;
        }
        if (event == Event::Home) {
            chat_scroll_y = 0.0f;
            render();
            return true;
        }
        if (event == Event::End) {
            chat_scroll_y = 1.0f;
            render();
            return true;
        }
        // Ctrl+J inserts newline while composing
        if (input_component && input_component->Focused() && event == Event::CtrlJ) {
            input_content.push_back('\n');
            render();
            return true;
        }

        // Send message with Ctrl+Enter (Ctrl+M) when input has focus
        if (input_component && input_component->Focused() && (event == Event::CtrlM)) {
            if (!input_content.empty() && on_input_callback) {
                on_input_callback(input_content);
                input_content.clear();
                chat_scroll_y = 1.0f;
                render();
            }
            return true;
        }

        // Scroll chat with Ctrl+Arrow regardless of focus
        if (event == Event::ArrowUpCtrl) {
            chat_scroll_y = std::max(0.0f, chat_scroll_y - 0.1f);
            render();
            return true;
        }
        if (event == Event::ArrowDownCtrl) {
            chat_scroll_y = std::min(1.0f, chat_scroll_y + 0.1f);
            render();
            return true;
        }

        // When input has focus, ArrowUp/ArrowDown scroll the chat instead of navigating conversations
        if (input_component && input_component->Focused() && (event == Event::ArrowUp || event == Event::ArrowDown)) {
            if (event == Event::ArrowUp) {
                chat_scroll_y = std::max(0.0f, chat_scroll_y - 0.1f);
            } else {
                chat_scroll_y = std::min(1.0f, chat_scroll_y + 0.1f);
            }
            render();
            return true;
        }

        // Navigate channels with arrow keys
        if (event == Event::ArrowUp || event == Event::ArrowDown) {
            if (channels.empty()) return false;
            
            // Build a list of channel names
            std::vector<std::string> channel_list;
            for (const auto& pair : channels) {
                channel_list.push_back(pair.first);
            }
            
            // Find current channel index
            int current_index = 0;
            for (size_t i = 0; i < channel_list.size(); i++) {
                if (channel_list[i] == active_channel) {
                    current_index = i;
                    break;
                }
            }
            
            // Move up or down
            if (event == Event::ArrowUp && current_index > 0) {
                set_active_channel(channel_list[current_index - 1]);
                return true;
            } else if (event == Event::ArrowDown && current_index < (int)channel_list.size() - 1) {
                set_active_channel(channel_list[current_index + 1]);
                return true;
            }
        }
        // Mouse wheel scroll when cursor is over chat area
        if (event.is_mouse()) {
            auto& m = event.mouse();
            if (m.x >= chat_box.x_min && m.x <= chat_box.x_max && m.y >= chat_box.y_min && m.y <= chat_box.y_max) {
                if (m.button == Mouse::WheelUp) {
                    chat_scroll_y = std::max(0.0f, chat_scroll_y - 0.1f);
                    render();
                    return true;
                }
                if (m.button == Mouse::WheelDown) {
                    chat_scroll_y = std::min(1.0f, chat_scroll_y + 0.1f);
                    render();
                    return true;
                }
            }
        }
        
        return false;
    });
    
    return component_with_keys;
}

void TUI::render() {
    screen.Post(Event::Custom);
}

bool TUI::show_login_dialog(std::string& host, int& port, bool& use_ssl,
                            std::string& username, std::string& password) {
    std::string port_str = std::to_string(port);
    int ssl_selected = use_ssl ? 0 : 1;
    std::vector<std::string> ssl_options = {"Yes", "No"};
    
    bool submitted = false;
    bool cancelled = false;
    
    // Create single-line inputs
    InputOption host_option;
    host_option.multiline = false;
    auto host_input = Input(&host, "localhost", host_option);
    
    InputOption port_option;
    port_option.multiline = false;
    auto port_input = Input(&port_str, "1337", port_option);
    
    // SSL dropdown
    auto ssl_dropdown = Radiobox(&ssl_options, &ssl_selected);
    
    InputOption user_option;
    user_option.multiline = false;
    auto user_input = Input(&username, "", user_option);
    
    InputOption password_option;
    password_option.password = true;
    password_option.multiline = false;
    auto pass_input = Input(&password, "", password_option);
    
    auto container = Container::Vertical({
        host_input,
        port_input,
        ssl_dropdown,
        user_input,
        pass_input,
    });
    
    auto renderer = Renderer(container, [&]() {
        return vbox({
            text("radi8c2 - Login") | bold | center,
            separator(),
            hbox({text("Host:     "), host_input->Render() | flex}),
            hbox({text("Port:     "), port_input->Render() | flex}),
            hbox({text("SSL:      "), ssl_dropdown->Render()}),
            hbox({text("Username: "), user_input->Render() | flex}),
            hbox({text("Password: "), pass_input->Render() | flex}),
            separator(),
            text("[Enter] Connect  [Esc] Quit") | dim | center,
        }) | border | size(WIDTH, EQUAL, 60) | center;
    });
    
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return) {
            // Validate username
            if (username.empty()) {
                return false; // Don't submit if username is empty
            }
            submitted = true;
            screen.Exit();
            return true;
        } else if (event == Event::Escape) {
            cancelled = true;
            screen.Exit();
            return true;
        }
        return false;
    });
    
    screen.Loop(component);
    
    if (cancelled) {
        return false;
    }
    
    if (submitted) {
        try {
            port = std::stoi(port_str);
        } catch (...) {
            port = 1337;
        }
        use_ssl = (ssl_selected == 0); // 0 = Yes, 1 = No
        return true;
    }
    
    return false;
}

void TUI::show_error(const std::string& error) {
    bool acknowledged = false;
    
    auto ok_button = Button("OK", [&] {
        acknowledged = true;
        screen.Exit();
    });
    
    auto component = Container::Vertical({
        ok_button,
    });
    
    auto renderer = Renderer(component, [&] {
        return vbox({
            text("Error") | bold | center,
            separator(),
            text(error) | center,
            separator(),
            ok_button->Render() | center,
        }) | border | size(WIDTH, EQUAL, 60) | center;
    });
    
    auto with_exit = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Return || event == Event::Escape) {
            acknowledged = true;
            screen.Exit();
            return true;
        }
        return false;
    });
    
    screen.Loop(with_exit);
}
