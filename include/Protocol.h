#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "Connection.h"
#include "TUI.h"
#include "FileTransfer.h"

class Protocol {
private:
    Connection* conn;
    TUI* tui;
    std::string username;
    bool authenticated;
    bool auth_error;  // Set when authentication error received
    bool auth_approved;  // Set when !apr:name received
    std::string motd_accumulator;  // Accumulate MOTD chunks
    std::unique_ptr<FileTransferManager> file_transfer_mgr;
    
public:
    Protocol(Connection* connection, TUI* ui);
    ~Protocol();
    
    bool has_auth_error() const { return auth_error; }
    void clear_auth_error() { auth_error = false; }
    bool is_auth_approved() const { return auth_approved; }
    void clear_auth_approved() { auth_approved = false; }
    
    bool authenticate(const std::string& user, const std::string& password);
    bool join_channel(const std::string& channel, const std::string& password = "");
    bool leave_channel(const std::string& channel);
    bool send_message(const std::string& channel, const std::string& message);
    bool send_emote(const std::string& channel, const std::string& emote);
    bool request_channel_list(bool clear_old = false);
    void clear_channel_list();
    bool request_user_list(const std::string& channel);
    bool request_motd();
    bool request_topic(const std::string& channel);
    bool set_topic(const std::string& channel, const std::string& topic);

    // Admin/superuser commands
    // Channel kick: kick:channel:user:reason (reason optional)
    bool kick_user(const std::string& channel, const std::string& username, const std::string& reason = "");
    // Global ban/unban
    bool ban_user(const std::string& username, int minutes, const std::string& reason);
    bool unban_user(const std::string& username);
    
    void process_server_message(const std::string& message);
    void process_file_transfers();  // Call periodically to send file chunks
    
    FileTransferManager* get_file_transfer_manager() { return file_transfer_mgr.get(); }
    
private:
    std::vector<std::string> parse_message(const std::string& message, char delimiter = ':');
    std::string escape_for_wire(const std::string& s);
    std::string unescape_from_wire(const std::string& s);
    void handle_user_message(const std::vector<std::string>& parts);
    void handle_user_emote(const std::vector<std::string>& parts);
    void handle_god_message(const std::vector<std::string>& parts);
    void handle_error(const std::vector<std::string>& parts);
    void handle_channel_add(const std::vector<std::string>& parts);
    void handle_user_joined(const std::vector<std::string>& parts);
    void handle_user_left(const std::vector<std::string>& parts);
    void handle_topic(const std::vector<std::string>& parts);
    void handle_motd(const std::vector<std::string>& parts);
    void handle_approval(const std::vector<std::string>& parts);
    void handle_die(const std::vector<std::string>& parts);
};

#endif
