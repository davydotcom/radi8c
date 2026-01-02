#include "TUI.h"
#include "Connection.h"
#include "Protocol.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>

std::atomic<bool> running(true);

void signal_handler(int signal) {
    running = false;
}

void receive_thread(Connection* conn, Protocol* proto, TUI* tui) {
    while (running && conn->is_connected()) {
        std::string message = conn->receive_message(100);
        if (!message.empty()) {
            // Process each line separately
            size_t start = 0;
            size_t pos = 0;
            while (pos < message.length()) {
                if (message[pos] == '\n') {
                    std::string line = message.substr(start, pos - start);
                    if (!line.empty() && line[0] == '!') {
                        proto->process_server_message(line);
                        tui->render();
                    }
                    start = pos + 1;
                }
                pos++;
            }
            
            // Handle last line if no trailing newline
            if (start < message.length()) {
                std::string line = message.substr(start);
                if (!line.empty() && line[0] == '!') {
                    proto->process_server_message(line);
                    tui->render();
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    TUI tui;
    Connection conn;
    
    try {
        tui.init();
        
        // Show login dialog
        std::string host, username, password;
        int port;
        bool use_ssl;
        
        if (!tui.show_login_dialog(host, port, use_ssl, username, password)) {
            tui.cleanup();
            std::cout << "Login cancelled." << std::endl;
            return 0;
        }
        
        tui.render_status_bar("Connecting to " + host + ":" + std::to_string(port) + "...");
        tui.render();
        
        // Connect to server
        if (!conn.connect_to_server(host, port, use_ssl)) {
            tui.show_error("Failed to connect to server");
            tui.cleanup();
            std::cerr << "Failed to connect to " << host << ":" << port << std::endl;
            return 1;
        }
        
        Protocol proto(&conn, &tui);
        
        // Authenticate
        if (!proto.authenticate(username, password)) {
            tui.show_error("Failed to authenticate");
            tui.cleanup();
            std::cerr << "Failed to authenticate" << std::endl;
            return 1;
        }
        
        tui.set_username(username);
        tui.render_status_bar("Connected as " + username);
        
        // Request MOTD
        proto.request_motd();
        
        // Request channel list
        proto.request_channel_list();
        
        // Start receive thread
        std::thread recv_thread(receive_thread, &conn, &proto, &tui);
        
        // Main loop
        while (running && conn.is_connected()) {
            std::string input = tui.get_input();
            
            if (!input.empty()) {
                // Handle commands
                if (input[0] == '/') {
                    // Parse command
                    size_t space_pos = input.find(' ');
                    std::string cmd = input.substr(1, space_pos - 1);
                    std::string args = (space_pos != std::string::npos) ? 
                                      input.substr(space_pos + 1) : "";
                    
                    if (cmd == "join" || cmd == "j") {
                        if (!args.empty()) {
                            proto.join_channel(args);
                        }
                    } else if (cmd == "leave" || cmd == "part" || cmd == "l") {
                        std::string channel = args.empty() ? tui.get_active_channel() : args;
                        if (!channel.empty()) {
                            proto.leave_channel(channel);
                            tui.remove_channel(channel);
                        }
                    } else if (cmd == "me") {
                        std::string channel = tui.get_active_channel();
                        if (!channel.empty() && !args.empty()) {
                            proto.send_emote(channel, args);
                        }
                    } else if (cmd == "topic") {
                        std::string channel = tui.get_active_channel();
                        if (!channel.empty()) {
                            if (args.empty()) {
                                proto.request_topic(channel);
                            } else {
                                proto.set_topic(channel, args);
                            }
                        }
                    } else if (cmd == "list") {
                        proto.request_channel_list();
                    } else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                        running = false;
                    } else if (cmd == "help" || cmd == "h") {
                        ChatMessage help;
                        help.channel = tui.get_active_channel();
                        help.username = "HELP";
                        help.message = "Commands: /join <channel>, /leave, /me <action>, "
                                      "/topic [new_topic], /list, /quit";
                        help.is_system = true;
                        help.is_emote = false;
                        tui.add_message(help);
                    }
                } else {
                    // Send message to active channel
                    std::string channel = tui.get_active_channel();
                    if (!channel.empty()) {
                        proto.send_message(channel, input);
                        
                        // Echo own message
                        ChatMessage msg;
                        msg.channel = channel;
                        msg.username = username;
                        msg.message = input;
                        msg.is_emote = false;
                        msg.is_system = false;
                        tui.add_message(msg);
                    }
                }
                
                tui.render();
            }
            
            // Small delay to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Cleanup
        running = false;
        if (recv_thread.joinable()) {
            recv_thread.join();
        }
        
        conn.disconnect();
        tui.cleanup();
        
        std::cout << "Disconnected from server." << std::endl;
        
    } catch (const std::exception& e) {
        tui.cleanup();
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
