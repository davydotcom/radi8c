#include "TUI.h"
#include "Connection.h"
#include "Protocol.h"
#include "Config.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <sstream>

std::atomic<bool> running(true);

static std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    std::ostringstream oss;
    oss << "[" << std::setfill('0') << std::setw(2) << local_time->tm_hour
        << ":" << std::setfill('0') << std::setw(2) << local_time->tm_min << "]";
    return oss.str();
}

void signal_handler(int) {
    running = false;
}

void receive_thread(Connection* conn, Protocol* proto, TUI* tui, std::atomic<bool>* connection_lost) {
    std::string line_buffer;  // Buffer for incomplete lines
    
    while (running && conn->is_connected()) {
        
        // Check connection before attempting receive to avoid issues during disconnect
        if (!conn->is_connected()) {
            break;
        }
        
        std::string message = conn->receive_message(100);
        
        if (!message.empty()) {
            // Prepend any buffered incomplete line from previous read
            if (!line_buffer.empty()) {
                message = line_buffer + message;
                line_buffer.clear();
            }
            
            // Process each line separately
            size_t start = 0;
            size_t pos = 0;
            while (pos < message.length()) {
                if (message[pos] == '\n') {
                    std::string line = message.substr(start, pos - start);
                    // Remove any trailing \r
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty() && line[0] == '!') {
                        proto->process_server_message(line);
                        tui->render();
                    }
                    start = pos + 1;
                }
                pos++;
            }
            
            // Buffer incomplete line (no trailing newline) for next read
            if (start < message.length()) {
                line_buffer = message.substr(start);
            }
        }
    }
    
    // If we exited because connection was lost (not user quitting), signal it
    if (running && !conn->is_connected()) {
        *connection_lost = true;
        tui->exit_loop();  // Exit the UI loop
    }
}

int main(int, char**) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    TUI tui;
    Connection conn;
    Config config;
    
    // Load saved configuration
    config.load();
    
    try {
        tui.init();
        
        // Main connection loop - reconnect on disconnection
        bool want_reconnect = true;
        
        while (want_reconnect && running) {
            // Clear all channels from previous connection
            tui.clear_all_channels();
            
            // Show login dialog and authenticate (loop until success or cancel)
            std::string host, username, password;
            int port;
            bool use_ssl;
            bool authenticated = false;
            Protocol* proto = nullptr;
            std::thread* recv_thread = nullptr;
            std::atomic<bool> connection_lost(false);
            
            // Initialize with saved config values
            ConnectionConfig last_conn = config.get_last_connection();
            host = last_conn.host;
            port = last_conn.port;
            use_ssl = last_conn.use_ssl;
            username = last_conn.username;
            password = "";
            
            while (!authenticated) {
            if (!tui.show_login_dialog(host, port, use_ssl, username, password)) {
                std::cout << "Login cancelled." << std::endl;
                return 0;
            }
            
            tui.set_status("Connecting to " + host + ":" + std::to_string(port) + "...");
            
            // Connect to server
            if (!conn.connect_to_server(host, port, use_ssl)) {
                tui.show_error("Failed to connect to server. Please try again.");
                conn.disconnect();
                continue;
            }
            
            // Create protocol object and start receive thread
            proto = new Protocol(&conn, &tui);
            proto->clear_auth_error();
            proto->clear_auth_approved();
            
            recv_thread = new std::thread(receive_thread, &conn, proto, &tui, &connection_lost);
            
            // Send authentication request
            tui.set_status("Authenticating as " + username + "...");
            if (!proto->authenticate(username, password)) {
                tui.show_error("Failed to send authentication. Please try again.");
                running = false;
                recv_thread->join();
                running = true;
                delete recv_thread;
                delete proto;
                proto = nullptr;
                recv_thread = nullptr;
                conn.disconnect();
                continue;
            }
            
            // Wait for authentication response (max 30 seconds for SSL handshake/network latency)
            int wait_ms = 0;
            while (wait_ms < 30000 && !proto->is_auth_approved() && !proto->has_auth_error()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                wait_ms += 100;
            }
            
            // Check result
            if (proto->has_auth_error()) {
                tui.show_error("Authentication failed. Invalid username or password.");
                conn.disconnect();  // Disconnect first to unblock receive thread
                running = false;  // Stop receive thread
                recv_thread->join();
                running = true;   // Reset for next attempt
                delete recv_thread;
                delete proto;
                proto = nullptr;
                recv_thread = nullptr;
                continue;
            } else if (!proto->is_auth_approved()) {
                tui.show_error("Authentication timeout. Please try again.");
                conn.disconnect();  // Disconnect first to unblock receive thread
                running = false;  // Stop receive thread
                recv_thread->join();
                running = true;   // Reset for next attempt
                delete recv_thread;
                delete proto;
                proto = nullptr;
                recv_thread = nullptr;
                continue;
            }
            
            authenticated = true;
        }
        
        // Save successful connection settings (excluding password)
        config.set_last_connection(host, port, use_ssl, username);
        config.save();
        
        // Wire join request callback (channel join or DM start)
        tui.set_join_request_callback([&](const std::string& name, const std::string& password, bool is_dm) {
            if (is_dm) {
                // No server-side join for DMs. Conversation opens locally.
                // Optionally request whois in future.
            } else {
                proto->join_channel(name, password);
            }
        });
        
        tui.set_username(username);
        tui.set_status("Connected as " + username);
        
        // Receive thread already started during authentication
        
        // Start file transfer processing thread
        std::thread file_transfer_thread([&]() {
            while (running && conn.is_connected()) {
                proto->process_file_transfers();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Minimal delay to prevent CPU spinning
            }
        });
        
        // Request MOTD
        proto->request_motd();
        
        // Request channel list
        proto->request_channel_list();
        
        // TEMPORARILY DISABLED: Auto-rejoin previously joined channels for this host
        // std::vector<std::string> prev_channels = config.get_joined_channels(host);
        // if (!prev_channels.empty()) {
        //     tui.set_status("Rejoining previous channels...");
        //     for (const auto& channel : prev_channels) {
        //         proto->join_channel(channel, "");
        //     }
        // }
        
        // Set up input callback
        bool user_requested_disconnect = false;
        tui.set_input_callback([&](const std::string& input) {
            if (input.empty()) return;
            
            // Handle commands
            if (input[0] == '/') {
                // Parse command
                size_t space_pos = input.find(' ');
                std::string cmd = input.substr(1, space_pos - 1);
                std::string args = (space_pos != std::string::npos) ? 
                                  input.substr(space_pos + 1) : "";
                
                if (cmd == "join" || cmd == "j") {
                    if (!args.empty()) {
                        // Support: /join <channel> [password]
                        auto trim = [](std::string s) {
                            size_t start = s.find_first_not_of(" \t");
                            size_t end = s.find_last_not_of(" \t");
                            if (start == std::string::npos) return std::string();
                            return s.substr(start, end - start + 1);
                        };
                        size_t sp = args.find(' ');
                        std::string chan = trim(sp == std::string::npos ? args : args.substr(0, sp));
                        std::string pw = sp == std::string::npos ? std::string() : trim(args.substr(sp + 1));
                        if (!chan.empty() && chan[0] == '#') chan = chan.substr(1);
                        if (!chan.empty()) {
                            proto->join_channel(chan, pw);
                        }
                    }
                } else if (cmd == "leave" || cmd == "part" || cmd == "l") {
                    std::string channel = args.empty() ? tui.get_active_channel() : args;
                    if (!channel.empty()) {
                        bool is_dm = tui.is_active_channel_dm();
                        // Only notify server for channels, not DMs
                        if (!is_dm && channel != "server") {
                            proto->leave_channel(channel);
                        }
                        // Remove from local UI and clear messages for both channels and DMs
                        tui.remove_channel(channel);
                        tui.set_status(is_dm ? "Left conversation with " + channel : "Left channel " + channel);
                    }
                } else if (cmd == "me") {
                    std::string channel = tui.get_active_channel();
                    if (!channel.empty() && !args.empty()) {
                        std::string target = tui.is_active_channel_dm() ? (std::string("user:") + channel) : channel;
                        proto->send_emote(target, args);
                        // Echo own emote locally
                        ChatMessage msg;
                        msg.channel = channel;
                        msg.username = username;
                        msg.message = args;
                        msg.timestamp = get_timestamp();
                        msg.is_emote = true;
                        msg.is_system = false;
                        tui.add_message(msg);
                    }
                } else if (cmd == "dm") {
                    // /dm <user> [message] — opens a DM pane and optionally sends a message
                    if (!args.empty()) {
                        // Extract username (accept with or without leading @) and optional message
                        std::istringstream iss(args);
                        std::string user; iss >> user;
                        if (!user.empty() && user[0] == '@') user.erase(0,1);
                        std::string dm_msg; std::getline(iss, dm_msg);
                        if (!dm_msg.empty() && dm_msg[0] == ' ') dm_msg.erase(0,1);

                        if (!user.empty()) {
                            // Ensure a DM conversation exists and focus it
                            tui.add_channel(user, "", true /*is_dm*/);
                            tui.set_active_channel(user);
                            // Optionally send message
                            if (!dm_msg.empty()) {
                                proto->send_message(std::string("user:") + user, dm_msg);
                                ChatMessage my;
                                my.channel = user;
                                my.username = username;
                                my.message = dm_msg;
                                my.timestamp = get_timestamp();
                                my.is_emote = false;
                                my.is_system = false;
                                tui.add_message(my);
                            }
                        } else {
                            tui.set_status("Usage: /dm <user> [message]");
                        }
                    } else {
                        tui.set_status("Usage: /dm <user> [message]");
                    }
                } else if (cmd == "topic") {
                    std::string channel = tui.get_active_channel();
                    if (!channel.empty()) {
                        if (args.empty()) {
                            proto->request_topic(channel);
                        } else {
                            proto->set_topic(channel, args);
                        }
                    }
                } else if (cmd == "list") {
                    proto->request_channel_list();
                    tui.set_status("Requested channel list");
                } else if (cmd == "refresh") {
                    tui.clear_unjoined_channels();
                    proto->request_channel_list();
                    tui.set_status("Refreshing channel list...");
                } else if (cmd == "pv") {
                    // /pv <message> — wrap entire message in <private>…</private>
                    if (!args.empty()) {
                        std::string channel = tui.get_active_channel();
                        if (!channel.empty()) {
                            std::string wrapped = std::string("<private>") + args + "</private>";
                            std::string target = tui.is_active_channel_dm() ? "user:" + channel : channel;
                            proto->send_message(target, wrapped);
                            // Echo locally (will be redacted by TUI)
                            ChatMessage msg;
                            msg.channel = channel;
                            msg.username = username;
                            msg.message = wrapped;
                            msg.timestamp = get_timestamp();
                            msg.is_emote = false;
                            msg.is_system = false;
                            tui.add_message(msg);
                        }
                    } else {
                        tui.set_status("Usage: /pv <message>");
                    }
                } else if (cmd == "send") {
                    // Send file: /send [/path/to/file]
                    std::string file_path = args;
                    if (file_path.empty()) {
                        file_path = tui.pick_file();
                    }
                    if (!file_path.empty()) {
                        std::string channel = tui.get_active_channel();
                        if (!channel.empty()) {
                            std::ifstream test_file(file_path);
                            if (test_file.good()) {
                                test_file.close();
                                std::string target = tui.is_active_channel_dm() ? "user:" + channel : channel;
                                if (proto->get_file_transfer_manager()->send_file(file_path, target)) {
                                    tui.set_status("Initiating file transfer...");
                                } else {
                                    tui.set_status("Failed to start file transfer");
                                }
                            } else {
                                tui.set_status("File not found: " + file_path);
                            }
                        }
                    }
                } else if (cmd == "open") {
                    // /open — opens the last received file
                    std::string last_path = tui.get_last_download();
                    if (!last_path.empty()) {
                        tui.open_download_path(last_path);
                        tui.set_status("Opening: " + last_path);
                    } else {
                        tui.set_status("No recent downloads");
                    }
                } else if (cmd == "disconnect") {
                    // Tear down connection and return to login prompt without error
                    user_requested_disconnect = true;
                    running = false;
                    conn.disconnect();
                    tui.exit_loop();
                } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                    running = false;
                    conn.disconnect();
                    tui.exit_loop();
                } else if (cmd == "kick") {
                    // /kick <user> [reason] - uses active channel
                    // /kick #<channel> <user> [reason] - specifies channel (must start with #)
                    if (!args.empty()) {
                        std::istringstream iss(args);
                        std::string first; iss >> first;
                        
                        std::string channel, user;
                        // If first arg starts with #, it's a channel specifier
                        if (!first.empty() && first[0] == '#') {
                            channel = first.substr(1);  // Remove #
                            iss >> user;  // Next arg is user
                        } else {
                            // First arg is the user, use active channel
                            channel = tui.get_active_channel();
                            user = first;
                        }
                        
                        // Rest is reason
                        std::string reason; std::getline(iss, reason);
                        if (!reason.empty() && reason[0] == ' ') reason.erase(0,1);
                        
                        if (!channel.empty() && !user.empty() && channel != "server" && !tui.is_active_channel_dm()) {
                            proto->kick_user(channel, user, reason);
                            tui.set_status("Kick requested: #" + channel + " → " + user);
                        } else if (channel == "server" || tui.is_active_channel_dm()) {
                            tui.set_status("Cannot kick from server channel or DM");
                        } else {
                            tui.set_status("Usage: /kick <user> [reason] OR /kick #<channel> <user> [reason]");
                        }
                    } else {
                        tui.set_status("Usage: /kick <user> [reason] OR /kick #<channel> <user> [reason]");
                    }
                } else if (cmd == "ban") {
                    // /ban <user> [minutes] [reason]
                    if (!args.empty()) {
                        std::istringstream iss(args);
                        std::string user; iss >> user;
                        std::string next; std::getline(iss, next);
                        // Trim leading space of next
                        if (!next.empty() && next[0] == ' ') next.erase(0,1);
                        int minutes = 0;
                        std::string reason;
                        if (!next.empty()) {
                            // Check if first token is a number
                            std::istringstream iss2(next);
                            int maybe_minutes;
                            if ( (iss2 >> maybe_minutes) ) {
                                minutes = maybe_minutes;
                                std::getline(iss2, reason);
                                if (!reason.empty() && reason[0]==' ') reason.erase(0,1);
                            } else {
                                // No minutes provided; treat remaining as reason
                                reason = next;
                            }
                        }
                        if (!user.empty()) {
                            proto->ban_user(user, minutes, reason);
                            tui.set_status("Ban requested for " + user);
                        }
                    } else {
                        tui.set_status("Usage: /ban <user> [minutes] [reason]");
                    }
                } else if (cmd == "unban") {
                    // /unban <user>
                    if (!args.empty()) {
                        std::string user = args;
                        // trim
                        user.erase(0, user.find_first_not_of(" \t"));
                        user.erase(user.find_last_not_of(" \t") + 1);
                        if (!user.empty()) {
                            proto->unban_user(user);
                            tui.set_status("Unban requested for " + user);
                        }
                    } else {
                        tui.set_status("Usage: /unban <user>");
                    }
                } else if (cmd == "clear") {
                    // /clear — clears the current channel or DM buffer
                    std::string channel = tui.get_active_channel();
                    if (!channel.empty()) {
                        tui.clear_channel_messages(channel);
                        // Show a system note in server channel for traceability?
                        tui.set_status("Cleared messages in " + (tui.is_active_channel_dm() ? std::string("@") : std::string("#")) + channel);
                    }
                } else if (cmd == "help" || cmd == "h") {
                    bool in_dm = tui.is_active_channel_dm();
                    std::string channel = tui.get_active_channel();
                    std::string common = "/help, /refresh, /list, /clear, /disconnect, /exit";
                    std::string dm_cmds = "/dm <user> [message], /msg <user> <message>, /me <action>";
                    std::string chan_cmds = "/join <channel> [password], /leave, /me <action>, /pv <message>, /topic [new_topic]";
                    std::string admin_cmds = "/kick <user> [reason], /ban <user> [minutes] [reason], /unban <user>";
                    ChatMessage help;
                    help.channel = channel;
                    help.username = "HELP";
                    help.timestamp = get_timestamp();
                    help.is_system = true;
                    help.is_emote = false;
                    help.message = std::string("Available: ") + (in_dm ? dm_cmds : (chan_cmds + "; Admin: " + admin_cmds)) + ". Also: " + common;
                    tui.add_message(help);
                }
            } else {
                // Send message to active channel or DM
                std::string channel = tui.get_active_channel();
                if (!channel.empty()) {
                    // If it's a DM, prepend "user:" prefix for the protocol
                    std::string target = tui.is_active_channel_dm() ? "user:" + channel : channel;
                    proto->send_message(target, input);
                    
                    // Echo own message
                    ChatMessage msg;
                    msg.channel = channel;
                    msg.username = username;
                    msg.message = input;
                    msg.timestamp = get_timestamp();
                    msg.is_emote = false;
                    msg.is_system = false;
                    tui.add_message(msg);
                }
            }
            
            tui.render();
        });
        
            // Run FTXUI event loop
            tui.run();
            
            // Save joined channels before disconnect
            std::vector<std::string> joined_channels = tui.get_joined_channels();
            config.set_joined_channels(host, joined_channels);
            config.save();
            
            // Cleanup threads — disconnect first to wake any blocking reads
            running = false;
            conn.disconnect();
            if (recv_thread && recv_thread->joinable()) {
                recv_thread->join();
            }
            if (file_transfer_thread.joinable()) {
                file_transfer_thread.join();
            }
            delete recv_thread;
            delete proto;
            recv_thread = nullptr;
            proto = nullptr;
            
            // Decide reconnection behavior
            if (connection_lost) {
                tui.show_error("Connection to server was lost.");
                // Loop will reconnect
                running = true;  // Reset running flag for reconnection
            } else if (user_requested_disconnect) {
                // Soft disconnect back to login
                running = true;
                want_reconnect = true;
            } else {
                // User quit intentionally
                want_reconnect = false;
                std::cout << "Disconnected from server." << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
