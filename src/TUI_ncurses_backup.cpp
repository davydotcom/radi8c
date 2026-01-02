#include "TUI.h"
#include <algorithm>
#include <sstream>
#include <regex>
#include <cstring>

TUI::TUI() : left_pane(nullptr), main_pane(nullptr), right_pane(nullptr),
             input_pane(nullptr), status_bar(nullptr), term_height(0), 
             term_width(0), color_supported(false), next_color_pair(1),
             input_cursor(0) {}

TUI::~TUI() {
    cleanup();
}

void TUI::init() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    
    // Check color support
    if (has_colors()) {
        start_color();
        color_supported = true;
        use_default_colors();
        
        // Initialize color pairs for users
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_YELLOW, -1);
        init_pair(4, COLOR_MAGENTA, -1);
        init_pair(5, COLOR_BLUE, -1);
        init_pair(6, COLOR_RED, -1);
        next_color_pair = 7;
    }
    
    getmaxyx(stdscr, term_height, term_width);
    
    // Create windows
    // Left pane: 20% width
    // Main pane: 60% width
    // Right pane: 20% width
    int left_width = term_width / 5;
    int right_width = term_width / 5;
    int main_width = term_width - left_width - right_width;
    
    // Main panes take all space except bottom 3 lines (2 for input, 1 for status)
    left_pane = newwin(term_height - 3, left_width, 0, 0);
    main_pane = newwin(term_height - 3, main_width, 0, left_width);
    right_pane = newwin(term_height - 3, right_width, 0, left_width + main_width);
    input_pane = newwin(2, term_width, term_height - 3, 0);
    status_bar = newwin(1, term_width, term_height - 1, 0);
    
    scrollok(main_pane, TRUE);
    
    refresh();
}

void TUI::cleanup() {
    if (left_pane) delwin(left_pane);
    if (main_pane) delwin(main_pane);
    if (right_pane) delwin(right_pane);
    if (input_pane) delwin(input_pane);
    if (status_bar) delwin(status_bar);
    
    endwin();
}

void TUI::resize() {
    getmaxyx(stdscr, term_height, term_width);
    
    cleanup();
    endwin();
    refresh();
    clear();
    
    init();
    render();
}

void TUI::add_channel(const std::string& name, const std::string& topic) {
    if (channels.find(name) == channels.end()) {
        Channel ch;
        ch.name = name;
        ch.topic = topic;
        ch.unread_count = 0;
        channels[name] = ch;
        
        if (active_channel.empty()) {
            active_channel = name;
        }
    }
}

void TUI::remove_channel(const std::string& name) {
    channels.erase(name);
    if (active_channel == name) {
        active_channel = channels.empty() ? "" : channels.begin()->first;
    }
}

void TUI::set_active_channel(const std::string& name) {
    if (channels.find(name) != channels.end()) {
        active_channel = name;
        channels[name].unread_count = 0;
    }
}

void TUI::add_message(const ChatMessage& msg) {
    if (channels.find(msg.channel) != channels.end()) {
        channels[msg.channel].messages.push_back(msg);
        
        if (msg.channel != active_channel) {
            channels[msg.channel].unread_count++;
        }
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

int TUI::get_color_for_user(const std::string& username) {
    if (!color_supported) return 0;
    
    if (user_colors.find(username) == user_colors.end()) {
        // Assign a color based on username hash
        int color = (std::hash<std::string>{}(username) % 6) + 1;
        user_colors[username] = color;
    }
    
    return user_colors[username];
}

bool TUI::contains_url(const std::string& text) {
    // Simple URL detection
    return text.find("http://") != std::string::npos ||
           text.find("https://") != std::string::npos ||
           text.find("www.") != std::string::npos;
}

std::vector<std::string> TUI::word_wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string current_line;
    
    while (words >> word) {
        if (current_line.length() + word.length() + 1 > (size_t)width) {
            if (!current_line.empty()) {
                lines.push_back(current_line);
                current_line.clear();
            }
            
            // Handle very long words
            while (word.length() > (size_t)width) {
                lines.push_back(word.substr(0, width));
                word = word.substr(width);
            }
            current_line = word;
        } else {
            if (!current_line.empty()) {
                current_line += " ";
            }
            current_line += word;
        }
    }
    
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    
    return lines.empty() ? std::vector<std::string>{""}  : lines;
}

void TUI::print_with_formatting(WINDOW* win, int y, int x, const std::string& text, 
                                const std::string& username) {
    int current_x = x;
    
    // Print username with color if provided
    if (!username.empty()) {
        int color = get_color_for_user(username);
        if (color_supported) {
            wattron(win, COLOR_PAIR(color) | A_BOLD);
        }
        mvwprintw(win, y, current_x, "%s: ", username.c_str());
        if (color_supported) {
            wattroff(win, COLOR_PAIR(color) | A_BOLD);
        }
        current_x += username.length() + 2;
    }
    
    // Check for URLs and apply underline
    if (contains_url(text)) {
        std::string remaining = text;
        size_t pos = 0;
        
        while (pos < remaining.length()) {
            size_t http_pos = remaining.find("http", pos);
            size_t www_pos = remaining.find("www.", pos);
            size_t url_start = std::min(http_pos, www_pos);
            
            if (url_start == std::string::npos) {
                mvwprintw(win, y, current_x, "%s", remaining.substr(pos).c_str());
                break;
            }
            
            // Print text before URL
            if (url_start > pos) {
                std::string before = remaining.substr(pos, url_start - pos);
                mvwprintw(win, y, current_x, "%s", before.c_str());
                current_x += before.length();
            }
            
            // Find end of URL
            size_t url_end = remaining.find_first_of(" \t\n", url_start);
            if (url_end == std::string::npos) {
                url_end = remaining.length();
            }
            
            // Print URL with underline
            std::string url = remaining.substr(url_start, url_end - url_start);
            wattron(win, A_UNDERLINE);
            mvwprintw(win, y, current_x, "%s", url.c_str());
            wattroff(win, A_UNDERLINE);
            current_x += url.length();
            
            pos = url_end;
        }
    } else {
        mvwprintw(win, y, current_x, "%s", text.c_str());
    }
}

void TUI::render() {
    render_left_pane();
    render_main_pane();
    render_right_pane();
    render_input_pane();
    render_status_bar("Connected");
}

void TUI::render_left_pane() {
    werase(left_pane);
    box(left_pane, 0, 0);
    mvwprintw(left_pane, 0, 2, " Channels ");
    
    int y = 1;
    int max_y, max_x;
    getmaxyx(left_pane, max_y, max_x);
    
    for (auto& pair : channels) {
        if (y >= max_y - 1) break;
        
        std::string display = pair.first;
        if (pair.second.unread_count > 0) {
            display += " (" + std::to_string(pair.second.unread_count) + ")";
        }
        
        if (pair.first == active_channel) {
            wattron(left_pane, A_REVERSE);
        }
        
        mvwprintw(left_pane, y, 1, "%-*s", max_x - 2, display.c_str());
        
        if (pair.first == active_channel) {
            wattroff(left_pane, A_REVERSE);
        }
        
        y++;
    }
    
    wrefresh(left_pane);
}

void TUI::render_main_pane() {
    werase(main_pane);
    box(main_pane, 0, 0);
    
    int max_y, max_x;
    getmaxyx(main_pane, max_y, max_x);
    
    if (active_channel.empty() || channels.find(active_channel) == channels.end()) {
        mvwprintw(main_pane, 0, 2, " No Active Channel ");
        wrefresh(main_pane);
        return;
    }
    
    Channel& ch = channels[active_channel];
    
    // Display topic in header
    std::string header = " " + active_channel + " ";
    if (!ch.topic.empty()) {
        header += "- " + ch.topic + " ";
    }
    if (header.length() > (size_t)(max_x - 4)) {
        header = header.substr(0, max_x - 7) + "...";
    }
    mvwprintw(main_pane, 0, 2, "%s", header.c_str());
    
    // Display messages
    int display_lines = max_y - 2;
    int total_messages = ch.messages.size();
    int start_msg = std::max(0, total_messages - display_lines);
    
    int y = 1;
    for (int i = start_msg; i < total_messages && y < max_y - 1; i++) {
        const ChatMessage& msg = ch.messages[i];
        
        if (msg.is_system) {
            if (color_supported) {
                wattron(main_pane, COLOR_PAIR(6));  // Red for system
            }
            mvwprintw(main_pane, y, 1, "[%s] %s", msg.username.c_str(), msg.message.c_str());
            if (color_supported) {
                wattroff(main_pane, COLOR_PAIR(6));
            }
        } else if (msg.is_emote) {
            if (color_supported) {
                wattron(main_pane, A_ITALIC);
            }
            print_with_formatting(main_pane, y, 1, "* " + msg.username + " " + msg.message);
            if (color_supported) {
                wattroff(main_pane, A_ITALIC);
            }
        } else {
            print_with_formatting(main_pane, y, 1, msg.message, msg.username);
        }
        
        y++;
    }
    
    wrefresh(main_pane);
}

void TUI::render_right_pane() {
    werase(right_pane);
    box(right_pane, 0, 0);
    mvwprintw(right_pane, 0, 2, " Users ");
    
    int max_y, max_x;
    getmaxyx(right_pane, max_y, max_x);
    
    if (active_channel.empty() || channels.find(active_channel) == channels.end()) {
        wrefresh(right_pane);
        return;
    }
    
    Channel& ch = channels[active_channel];
    int y = 1;
    
    for (const auto& user : ch.users) {
        if (y >= max_y - 1) break;
        
        int color = get_color_for_user(user);
        if (color_supported) {
            wattron(right_pane, COLOR_PAIR(color));
        }
        
        mvwprintw(right_pane, y, 1, "%-*s", max_x - 2, user.c_str());
        
        if (color_supported) {
            wattroff(right_pane, COLOR_PAIR(color));
        }
        
        y++;
    }
    
    wrefresh(right_pane);
}

void TUI::render_input_pane() {
    werase(input_pane);
    box(input_pane, 0, 0);
    
    int max_y, max_x;
    getmaxyx(input_pane, max_y, max_x);
    
    std::string prompt = "> ";
    mvwprintw(input_pane, 1, 1, "%s%s", prompt.c_str(), input_buffer.c_str());
    
    wrefresh(input_pane);
}

void TUI::render_status_bar(const std::string& status) {
    werase(status_bar);
    
    int max_x;
    getmaxyx(status_bar, max_x, max_x);
    
    wattron(status_bar, A_REVERSE);
    mvwprintw(status_bar, 0, 0, "%-*s", max_x, status.c_str());
    wattroff(status_bar, A_REVERSE);
    
    wrefresh(status_bar);
}

std::string TUI::get_input() {
    int ch = getch();
    
    if (ch == ERR) {
        return "";
    }
    
    if (ch == '\n' || ch == KEY_ENTER) {
        std::string result = input_buffer;
        input_buffer.clear();
        input_cursor = 0;
        render_input_pane();
        return result;
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (!input_buffer.empty()) {
            input_buffer.pop_back();
            input_cursor = input_buffer.length();
        }
        render_input_pane();
    } else if (ch >= 32 && ch < 127) {
        input_buffer += (char)ch;
        input_cursor = input_buffer.length();
        render_input_pane();
    }
    
    return "";
}

bool TUI::show_login_dialog(std::string& host, int& port, bool& use_ssl,
                            std::string& username, std::string& password) {
    WINDOW* dialog = newwin(12, 60, (term_height - 12) / 2, (term_width - 60) / 2);
    box(dialog, 0, 0);
    mvwprintw(dialog, 0, 2, " radi8c2 - Login ");
    
    mvwprintw(dialog, 2, 2, "Host: ");
    mvwprintw(dialog, 3, 2, "Port: ");
    mvwprintw(dialog, 4, 2, "SSL:  ");
    mvwprintw(dialog, 5, 2, "Username: ");
    mvwprintw(dialog, 6, 2, "Password: ");
    mvwprintw(dialog, 8, 2, "[Tab] Next  [Enter] Connect  [Ctrl+C] Quit");
    
    wrefresh(dialog);
    
    echo();
    keypad(dialog, TRUE);
    nodelay(dialog, FALSE);
    
    std::string fields[5] = {"localhost", "1337", "n", "", ""};
    int current_field = 0;
    
    while (true) {
        // Display current values
        mvwprintw(dialog, 2, 8, "%-40s", fields[0].c_str());
        mvwprintw(dialog, 3, 8, "%-40s", fields[1].c_str());
        mvwprintw(dialog, 4, 8, "%-40s", fields[2].c_str());
        mvwprintw(dialog, 5, 12, "%-36s", fields[3].c_str());
        mvwprintw(dialog, 6, 12, "%-36s", std::string(fields[4].length(), '*').c_str());
        
        // Move cursor to current field
        int y_pos[] = {2, 3, 4, 5, 6};
        int x_pos[] = {8, 8, 8, 12, 12};
        wmove(dialog, y_pos[current_field], x_pos[current_field] + fields[current_field].length());
        wrefresh(dialog);
        
        int ch = wgetch(dialog);
        
        if (ch == '\t' || ch == KEY_DOWN) {
            current_field = (current_field + 1) % 5;
        } else if (ch == KEY_UP) {
            current_field = (current_field - 1 + 5) % 5;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            host = fields[0];
            try {
                port = std::stoi(fields[1]);
            } catch (...) {
                port = 1337;
            }
            use_ssl = (fields[2] == "y" || fields[2] == "Y" || fields[2] == "yes");
            username = fields[3];
            password = fields[4];
            
            delwin(dialog);
            noecho();
            nodelay(stdscr, TRUE);
            return true;
        } else if (ch == 3) {  // Ctrl+C
            delwin(dialog);
            noecho();
            nodelay(stdscr, TRUE);
            return false;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!fields[current_field].empty()) {
                fields[current_field].pop_back();
            }
        } else if (ch >= 32 && ch < 127) {
            fields[current_field] += (char)ch;
        }
    }
}

void TUI::show_error(const std::string& error) {
    WINDOW* dialog = newwin(7, 60, (term_height - 7) / 2, (term_width - 60) / 2);
    box(dialog, 0, 0);
    mvwprintw(dialog, 0, 2, " Error ");
    
    mvwprintw(dialog, 2, 2, "%s", error.c_str());
    mvwprintw(dialog, 4, 2, "Press any key to continue...");
    
    wrefresh(dialog);
    
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);
    
    delwin(dialog);
}
