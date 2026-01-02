#include "TUI.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include <algorithm>
#include <sstream>
#include <climits>
#include <cstdio>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

using namespace ftxui;

TUI::TUI() : screen(ScreenInteractive::Fullscreen()), 
             should_exit(false) {}

TUI::~TUI() {
    cleanup();
}

void TUI::init() {
    main_component = build_ui();
}

void TUI::open_file(const std::string& path) {
#if defined(__APPLE__) || defined(__MACH__)
    std::string cmd = std::string("open ") + '"' + path + '"';
#else
    std::string cmd = std::string("xdg-open ") + '"' + path + '"';
#endif
    // Fire and forget
    std::system(cmd.c_str());
}

void TUI::open_download_path(const std::string& path) {
    open_file(path);
}

void TUI::open_last_download() {
    if (!last_download_path.empty()) {
        open_file(last_download_path);
    }
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

void TUI::clear_all_channels() {
    // Clear all channels and reset active channel
    channels.clear();
    active_channel.clear();
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

void TUI::add_message(const ChatMessage& incoming) {
    auto itc = channels.find(incoming.channel);
    if (itc == channels.end()) return;

    ChatMessage msg = incoming;
    msg.id = next_msg_id++;
    msg.raw_message = incoming.message;
    // Redact private regions for display by default
    bool has_priv = false;
    msg.message = redact_private(incoming.message, &has_priv);
    msg.has_private = has_priv;

    itc->second.messages.push_back(msg);

    if (msg.channel != active_channel) {
        itc->second.unread_count++;
    } else {
        chat_scroll_y = 1.0f;
    }
    refresh_conversations();
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

std::string TUI::redact_private(const std::string& in, bool* has_private) {
    if (has_private) *has_private = false;
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t start = in.find("<private>", pos);
        if (start == std::string::npos) {
            out += in.substr(pos);
            break;
        }
        if (has_private) *has_private = true;
        out += in.substr(pos, start - pos);
        size_t end = in.find("</private>", start + 9);
        if (end == std::string::npos) { // malformed, append rest
            out += in.substr(start);
            break;
        }
        size_t len = end - (start + 9);
        out += std::string(len, '*');
        pos = end + 10; // skip closing tag
    }
    return out;
}

std::string TUI::untag_private(const std::string& in) {
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t start = in.find("<private>", pos);
        if (start == std::string::npos) {
            out += in.substr(pos);
            break;
        }
        out += in.substr(pos, start - pos);
        size_t end = in.find("</private>", start + 9);
        if (end == std::string::npos) {
            out += in.substr(start);
            break;
        }
        // append inner content only
        out += in.substr(start + 9, end - (start + 9));
        pos = end + 10;
    }
    return out;
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

    // Ensure message_controls exists
    if (!message_controls) message_controls = Container::Horizontal({});
    
    if (msg.is_system) {
        std::string sys_text = "[" + msg.username + "] " + msg.message;
        auto wrapped = wrap_text(sys_text, message_width);
        Elements lines;
        
        // If this system message has an open_path, make the wrapped lines clickable
        if (!msg.open_path.empty()) {
            ButtonOption opt = ButtonOption::Simple();
            opt.transform = [](const EntryState& s){
                auto e = text(s.label) | underlined | color(Color::Cyan);
                if (s.focused) e = e | bold;
                return e;
            };
            
            for (const auto& line : wrapped) {
                auto btn = Button(line, [this, &msg](){ open_file(msg.open_path); }, opt);
                if (message_controls) message_controls->Add(btn);
                lines.push_back(btn->Render());
            }
        } else {
            for (const auto& line : wrapped) {
                lines.push_back(text(line) | color(Color::Red));
            }
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
        // Normal message with timestamp, username, and content possibly containing <private>…</private>
        bool is_own_message = (msg.username == current_username);
        std::string prefix = msg.timestamp + " " + msg.username + ": ";

        // If not revealed yet, msg.message already has private blocks redacted.
        // If revealed, use raw_message for this message id.
        bool revealed = (msg.has_private && revealed_private_ids.count(msg.id) > 0);
        std::string content = revealed ? untag_private(msg.raw_message) : msg.message;

        // If not revealed and has private, render buttons in place of masked regions
        Elements lines;
        if (msg.has_private && !revealed) {
            // Split by <private>…</private>
            size_t pos = 0;
            size_t start = 0;
            Elements row_segments;
            auto flush_text = [&](const std::string& s){ if(!s.empty()) row_segments.push_back(text(s)); };
            // We render a single line using paragraph for wrapping later
            while ((start = msg.raw_message.find("<private>", pos)) != std::string::npos) {
                std::string before = msg.raw_message.substr(pos, start - pos);
                size_t endtag = msg.raw_message.find("</private>", start + 9);
                if (endtag == std::string::npos) break; // malformed; bail
                std::string hidden = msg.raw_message.substr(start + 9, endtag - (start + 9));
                flush_text(before);
                std::string mask(hidden.size(), '*');
                // Create a button to reveal this message's private content (toggle all of them)
                ButtonOption opt = ButtonOption::Simple();
                opt.transform = [](const EntryState& s){ auto e = text(s.label) | underlined; if (s.focused) e = e | inverted; return e; };
                auto btn = Button(mask, [this, &msg]() {
                    // Toggle reveal for this message id
                    if (revealed_private_ids.count(msg.id)) revealed_private_ids.erase(msg.id); else revealed_private_ids.insert(msg.id);
                    render();
                }, opt);
                if (message_controls) message_controls->Add(btn);
                row_segments.push_back(btn->Render());
                pos = endtag + 10; // len("</private>") == 10
            }
            // Append remaining tail
            if (pos < msg.raw_message.size()) {
                flush_text(msg.raw_message.substr(pos));
            }

            // Build a paragraph with prefix + segments
            Elements prefix_el{ text(prefix) };
            auto line_el = hbox(prefix_el) | nothing;
            // Join segments after prefix
            row_segments.insert(row_segments.begin(), text(""));
            lines.push_back(hbox({ text(prefix), hbox(row_segments) }));
            return vbox(lines);
        }

        // Else: simple wrapped text for either revealed or non-private
        std::string full_text = prefix + content;
        auto wrapped = wrap_text(full_text, message_width);
        
        if (!msg.open_path.empty()) {
            // Make the wrapped lines clickable
            ButtonOption opt = ButtonOption::Simple();
            opt.transform = [](const EntryState& s){
                auto e = text(s.label) | underlined | color(Color::Cyan);
                if (s.focused) e = e | bold;
                return e;
            };
            
            for (const auto& line : wrapped) {
                auto btn = Button(line, [this, &msg](){ open_file(msg.open_path); }, opt);
                if (message_controls) message_controls->Add(btn);
                lines.push_back(btn->Render());
            }
        } else {
            // For each wrapped line, check if it contains the username and colorize it
            for (size_t i = 0; i < wrapped.size(); ++i) {
                const auto& line = wrapped[i];
                
                // First line contains timestamp and username
                if (i == 0 && is_own_message) {
                    // Split line into parts: timestamp, username, and rest
                    size_t username_start = msg.timestamp.length() + 1; // +1 for space
                    size_t username_end = username_start + msg.username.length();
                    
                    if (line.length() > username_end) {
                        std::string timestamp_part = line.substr(0, username_start);
                        std::string username_part = line.substr(username_start, msg.username.length());
                        std::string rest_part = line.substr(username_end);
                        
                        auto elem = hbox({
                            text(timestamp_part),
                            text(username_part) | color(Color::Green),
                            text(rest_part)
                        });
                        lines.push_back(elem);
                    } else {
                        if (contains_url(line)) lines.push_back(format_text_with_urls(line));
                        else lines.push_back(text(line));
                    }
                } else {
                    if (contains_url(line)) lines.push_back(format_text_with_urls(line));
                    else lines.push_back(text(line));
                }
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

    // Joined channels buttons
    for (const auto& name : joined_channels) {
        const auto& ch = channels[name];
        int unread = ch.unread_count;
        std::string label = "#" + name + (unread>0? (" ("+std::to_string(unread)+")") : "");
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
        std::string label = "@" + name + (unread>0? (" ("+std::to_string(unread)+")") : "");
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
        std::string label = "#" + name + (unread>0? (" ("+std::to_string(unread)+")") : "");
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
            bool is_current_user = (user == current_username);
            std::string display_name = user + (is_current_user ? " *" : "");
            auto elem = text(display_name) | color(get_color_for_user(user));
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
    message_controls = Container::Horizontal({});
    auto container = Container::Horizontal({
        channel_list,
        input_component,
        maybe_modal,
        message_controls,
    });
    
    // Give initial focus to the input box
    input_component->TakeFocus();

    // Main renderer
    auto renderer = Renderer(container, [this, channel_list]() {
        // Reset message control widgets for this frame
        if (message_controls) message_controls->DetachAllChildren();

        auto left = channel_list->Render() | size(WIDTH, EQUAL, 24);
        auto center_inner = render_chat_area();
        auto center = center_inner | vscroll_indicator | focusPositionRelative(0.0f, chat_scroll_y) | frame | reflect(chat_box) | border | flex;
        auto right = render_user_list() | size(WIDTH, EQUAL, 22);
        
        // Build status row
        auto status = hbox({ text(status_text) | inverted | flex });
        
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
            
            // Build channel list in visual order: joined channels, DMs, browse/unjoined
            std::vector<std::string> joined_channels;
            std::vector<std::string> dm_list;
            std::vector<std::string> unjoined_channels;
            
            for (const auto& kv : channels) {
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
            std::sort(dm_list.begin(), dm_list.end(), by_name);
            std::sort(unjoined_channels.begin(), unjoined_channels.end(), by_name);
            
            // Build final ordered list
            std::vector<std::string> channel_list;
            channel_list.insert(channel_list.end(), joined_channels.begin(), joined_channels.end());
            channel_list.insert(channel_list.end(), dm_list.begin(), dm_list.end());
            channel_list.insert(channel_list.end(), unjoined_channels.begin(), unjoined_channels.end());
            
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
    
    // If all fields except password are filled, focus password field
    if (!host.empty() && !username.empty()) {
        pass_input->TakeFocus();
    }
    
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

void TUI::refresh_file_picker_entries() {
    file_picker_entries.clear();
    file_picker_selected_index = 0;
    
    // Always add parent directory option
    file_picker_entries.push_back("..");
    
    DIR* dir = opendir(file_picker_path.c_str());
    if (!dir) return;
    
    std::vector<std::string> directories;
    std::vector<std::string> files;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        std::string full_path = file_picker_path + "/" + name;
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                directories.push_back(name + "/");
            } else if (S_ISREG(st.st_mode)) {
                files.push_back(name);
            }
        }
    }
    closedir(dir);
    
    // Sort and add directories first, then files
    std::sort(directories.begin(), directories.end());
    std::sort(files.begin(), files.end());
    
    for (const auto& d : directories) {
        file_picker_entries.push_back(d);
    }
    for (const auto& f : files) {
        file_picker_entries.push_back(f);
    }
}

Component TUI::build_file_picker_modal() {
    // Create a menu for file selection
    auto menu_container = Container::Vertical({});
    
    return Renderer(menu_container, [this]() {
        Elements entries_display;
        for (size_t i = 0; i < file_picker_entries.size(); ++i) {
            auto entry_text = text(file_picker_entries[i]);
            if (i == file_picker_selected_index) {
                entry_text = entry_text | inverted | bold;
            }
            entries_display.push_back(entry_text);
        }
        
        auto title = text("Select File: " + file_picker_path) | bold | center;
        auto file_list = vbox(entries_display) | vscroll_indicator | frame | flex;
        auto help_text = text("↑/↓: Navigate | Enter: Select | Esc: Cancel") | dim | center;
        
        return vbox({
            title,
            separator(),
            file_list,
            separator(),
            help_text,
        }) | border | size(WIDTH, EQUAL, 80) | size(HEIGHT, EQUAL, 30) | center;
    });
}

std::string TUI::pick_file() {
    // Initialize file picker with current directory
    char cwd_buffer[PATH_MAX];
    if (getcwd(cwd_buffer, sizeof(cwd_buffer)) != nullptr) {
        file_picker_path = cwd_buffer;
    } else {
        file_picker_path = "/";
    }
    
    refresh_file_picker_entries();
    file_picker_selected_file.clear();
    show_file_picker_modal = true;
    
    file_picker_modal_component = build_file_picker_modal();
    
    auto component = CatchEvent(file_picker_modal_component, [this](Event event) {
        if (event == Event::Escape) {
            show_file_picker_modal = false;
            screen.Exit();
            return true;
        }
        
        if (event == Event::ArrowUp) {
            if (file_picker_selected_index > 0) {
                file_picker_selected_index--;
            }
            return true;
        }
        
        if (event == Event::ArrowDown) {
            if (file_picker_selected_index < static_cast<int>(file_picker_entries.size()) - 1) {
                file_picker_selected_index++;
            }
            return true;
        }
        
        if (event == Event::Return) {
            if (file_picker_selected_index >= 0 && 
                file_picker_selected_index < static_cast<int>(file_picker_entries.size())) {
                std::string selected = file_picker_entries[file_picker_selected_index];
                
                if (selected == "..") {
                    // Go to parent directory
                    size_t last_slash = file_picker_path.find_last_of('/');
                    if (last_slash != std::string::npos && last_slash > 0) {
                        file_picker_path = file_picker_path.substr(0, last_slash);
                    } else {
                        file_picker_path = "/";
                    }
                    refresh_file_picker_entries();
                } else if (selected.back() == '/') {
                    // Enter directory
                    file_picker_path += "/" + selected.substr(0, selected.length() - 1);
                    refresh_file_picker_entries();
                } else {
                    // Selected a file
                    file_picker_selected_file = file_picker_path + "/" + selected;
                    show_file_picker_modal = false;
                    screen.Exit();
                }
            }
            return true;
        }
        
        return false;
    });
    
    screen.Loop(component);
    
    return file_picker_selected_file;
}
