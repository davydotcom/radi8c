#include "Protocol.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <iostream>
#include <algorithm>

Protocol::Protocol(Connection* connection, TUI* ui) 
    : conn(connection), tui(ui), authenticated(false), auth_error(false), auth_approved(false) {
    file_transfer_mgr = std::make_unique<FileTransferManager>(this, ui);
}

Protocol::~Protocol() {}

static std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    std::ostringstream oss;
    oss << "[" << std::setfill('0') << std::setw(2) << local_time->tm_hour
        << ":" << std::setfill('0') << std::setw(2) << local_time->tm_min << "]";
    return oss.str();
}

std::vector<std::string> Protocol::parse_message(const std::string& message, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    
    for (char c : message) {
        if (c == delimiter) {
            parts.push_back(current);
            current.clear();
        } else if (c != '\n' && c != '\r') {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    
    return parts;
}

std::string Protocol::escape_for_wire(const std::string& s) {
    // Replace ':' and newlines with placeholders
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ':') {
            out += "<colon>";
        } else if (c == '\n') {
            out += "<nl>";
        } else if (c == '\r') {
            // skip CR; server strips CR
        } else {
            out += c;
        }
    }
    return out;
}

std::string Protocol::unescape_from_wire(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 7, "<colon>") == 0) {
            out += ':';
            i += 7;
        } else if (s.compare(i, 4, "<nl>") == 0) {
            out += '\n';
            i += 4;
        } else {
            out += s[i++];
        }
    }
    return out;
}

bool Protocol::authenticate(const std::string& user, const std::string& password) {
    username = user;
    std::string auth_msg = "!name:" + user;
    if (!password.empty()) {
        auth_msg += ":" + password;
    }
    authenticated = conn->send_message(auth_msg);
    return authenticated;
}

bool Protocol::join_channel(const std::string& channel, const std::string& password) {
    std::string msg = "!jnchn:" + channel;
    if (!password.empty()) {
        msg += ":" + password;
    }
    return conn->send_message(msg);
}

bool Protocol::leave_channel(const std::string& channel) {
    return conn->send_message("!lvchn:" + channel);
}

bool Protocol::send_message(const std::string& channel, const std::string& message) {
    // Check if message contains file subprotocol tags - don't escape those
    if (message.find("<file|") != std::string::npos || message.find("</file|") != std::string::npos) {
        // File transfer message - send as-is without escaping
        return conn->send_message("!msg:" + channel + ":" + message);
    }
    return conn->send_message("!msg:" + channel + ":" + escape_for_wire(message));
}

bool Protocol::send_emote(const std::string& channel, const std::string& emote) {
    return conn->send_message("!emote:" + channel + ":" + escape_for_wire(emote));
}

bool Protocol::request_channel_list(bool clear_old) {
    // Note: We don't clear old channels - just request new list and let add_channel update
    return conn->send_message("!chanlist");
}

void Protocol::clear_channel_list() {
    tui->clear_unjoined_channels();
}

bool Protocol::request_user_list(const std::string& channel) {
    return conn->send_message("!userlist:" + channel);
}

bool Protocol::request_motd() {
    return conn->send_message("!motd");
}

bool Protocol::request_topic(const std::string& channel) {
    return conn->send_message("!topic:" + channel);
}

bool Protocol::set_topic(const std::string& channel, const std::string& topic) {
    return conn->send_message("!settopic:" + channel + ":" + topic);
}

bool Protocol::kick_user(const std::string& channel, const std::string& user, const std::string& reason) {
    // Intended wire format: kick:channel:user:reason (reason optional)
    std::string payload = "!kick:" + channel + ":" + user;
    if (!reason.empty()) {
        payload += ":" + escape_for_wire(reason);
    }
    return conn->send_message(payload);
}

bool Protocol::ban_user(const std::string& user, int minutes, const std::string& reason) {
    // Docs: !ban: user: time: reason (0=permanent)
    if (minutes < 0) minutes = 0;
    std::string r = reason.empty() ? std::string("no reason") : reason;
    return conn->send_message("!ban:" + user + ":" + std::to_string(minutes) + ":" + escape_for_wire(r));
}

bool Protocol::unban_user(const std::string& user) {
    // Docs: !unban: user
    return conn->send_message("!unban:" + user);
}

void Protocol::process_file_transfers() {
    if (file_transfer_mgr) {
        file_transfer_mgr->process_outgoing_transfers();
    }
}

void Protocol::process_server_message(const std::string& message) {
    if (message.empty() || message[0] != '!') {
        return;
    }
    
    std::vector<std::string> parts = parse_message(message, ':');
    if (parts.empty()) {
        return;
    }
    
    const std::string& cmd = parts[0];
    
    if (cmd == "!usrmsg") {
        handle_user_message(parts);
    } else if (cmd == "!usremt") {
        handle_user_emote(parts);
    } else if (cmd == "!godmsg") {
        handle_god_message(parts);
    } else if (cmd == "!err") {
        handle_error(parts);
    } else if (cmd == "!chanadd") {
        handle_channel_add(parts);
    } else if (cmd == "!usrjoind") {
        handle_user_joined(parts);
    } else if (cmd == "!usrleft") {
        handle_user_left(parts);
    } else if (cmd == "!topic") {
        handle_topic(parts);
    } else if (cmd == "!motd") {
        handle_motd(parts);
    } else if (cmd == "!apr") {
        handle_approval(parts);
    } else if (cmd == "!die") {
        handle_die(parts);
    }
}

void Protocol::handle_user_message(const std::vector<std::string>& parts) {
    // !usrmsg: chan_or_user: user: message
    if (parts.size() < 4) return;
    
    std::string chan_field = parts[1];
    std::string sender = parts[2];
    
    // Rejoin parts[3] onwards with ':' since message content may contain colons
    std::string raw_message = parts[3];
    for (size_t i = 4; i < parts.size(); i++) {
        raw_message += ":" + parts[i];
    }
    
    bool is_dm = false;
    std::string convo_name;
    
    // Two possible DM encodings exist:
    // 1) chan_field == "user" and parts: !usrmsg:user:<from>:<msg>
    // 2) chan_field starts with "user:" (older/doc-convention)
    if (chan_field == "user") {
        is_dm = true;
        convo_name = sender; // open DM with the sender
        tui->add_channel(convo_name, "", true);
    } else if (chan_field.rfind("user:", 0) == 0) {
        is_dm = true;
        convo_name = sender; // prefer sender as the pane name
        tui->add_channel(convo_name, "", true);
    } else {
        convo_name = chan_field; // normal channel
    }
    
    // Check for file transfer subprotocol (check raw message before unescaping)
    if (raw_message.find("<file|") == 0) {
        // Parse file chunk: <file|fd|filename_or_seq>base64data
        size_t close_bracket = raw_message.find('>');
        if (close_bracket != std::string::npos) {
            std::string header = raw_message.substr(6, close_bracket - 6);  // skip "<file|"
            std::string data = raw_message.substr(close_bracket + 1);
            
            // Parse header parts (using | as delimiter)
            size_t first_pipe = header.find('|');
            if (first_pipe != std::string::npos) {
                int fd = std::stoi(header.substr(0, first_pipe));
                std::string second_part = header.substr(first_pipe + 1);
                
                // Check if second part is a number (sequence) or filename
                try {
                    int seq = std::stoi(second_part);
                    // It's a sequence number
                    file_transfer_mgr->receive_chunk(sender, fd, seq, "", 0, data);
                } catch (...) {
                    // It's a filename (first chunk, sequence 0) - may include file size
                    // Format: filename|filesize or just filename
                    size_t second_pipe = second_part.find('|');
                    if (second_pipe != std::string::npos) {
                        std::string filename = second_part.substr(0, second_pipe);
                        size_t file_size = std::stoull(second_part.substr(second_pipe + 1));
                        file_transfer_mgr->receive_chunk(sender, fd, 0, filename, file_size, data);
                    } else {
                        // Old format without file size
                        file_transfer_mgr->receive_chunk(sender, fd, 0, second_part, 0, data);
                    }
                }
            }
        }
        return;  // Don't display file chunks as regular messages
    } else if (raw_message.find("</file|") == 0) {
        // Parse final marker: </file|fd|totalSeq>
        size_t close_bracket = raw_message.find('>');
        if (close_bracket != std::string::npos) {
            std::string params = raw_message.substr(7, close_bracket - 7);  // skip "</file|"
            size_t pipe = params.find('|');
            if (pipe != std::string::npos) {
                int fd = std::stoi(params.substr(0, pipe));
                int total_chunks = std::stoi(params.substr(pipe + 1));
                file_transfer_mgr->finalize_transfer(sender, fd, total_chunks);
            }
        }
        return;  // Don't display final marker as regular message
    }
    
    // Only unescape for regular messages (not file transfers)
    std::string message = unescape_from_wire(raw_message);
    
    ChatMessage msg;
    msg.channel = convo_name;
    msg.username = sender;
    msg.message = message;
    msg.timestamp = get_timestamp();
    msg.is_emote = false;
    msg.is_system = false;
    
    tui->add_message(msg);
}

void Protocol::handle_user_emote(const std::vector<std::string>& parts) {
    // !usremt: chan_or_user: user: emotion
    if (parts.size() < 4) return;
    
    std::string chan_field = parts[1];
    std::string sender = parts[2];
    std::string emotion = unescape_from_wire(parts[3]);
    
    bool is_dm = false;
    std::string convo_name;
    if (chan_field == "user" || chan_field.rfind("user:", 0) == 0) {
        is_dm = true;
        convo_name = sender;
        tui->add_channel(convo_name, "", true);
    } else {
        convo_name = chan_field;
    }
    
    ChatMessage msg;
    msg.channel = convo_name;
    msg.username = sender;
    msg.message = emotion;
    msg.timestamp = get_timestamp();
    msg.is_emote = true;
    msg.is_system = false;
    
    tui->add_message(msg);
}

void Protocol::handle_god_message(const std::vector<std::string>& parts) {
    // !godmsg: chan: message
    if (parts.size() < 3) return;
    
    // Rejoin parts[2] onwards with ':' since message content may contain colons
    std::string raw_message = parts[2];
    for (size_t i = 3; i < parts.size(); i++) {
        raw_message += ":" + parts[i];
    }
    
    ChatMessage msg;
    msg.channel = parts[1];
    msg.username = "SERVER";
    msg.message = unescape_from_wire(raw_message);
    msg.timestamp = get_timestamp();
    msg.is_emote = false;
    msg.is_system = true;
    
    tui->add_message(msg);
}

void Protocol::handle_error(const std::vector<std::string>& parts) {
    // !err:regarding:reason
    if (parts.size() < 3) return;
    
    // Check for authentication error (any !err:name:* indicates auth failure)
    if (parts[1] == "name") {
        auth_error = true;
    }
    
    ChatMessage msg;
    msg.channel = tui->get_active_channel();
    msg.username = "ERROR";
    msg.message = unescape_from_wire(parts[1] + ": " + parts[2]);
    msg.timestamp = get_timestamp();
    msg.is_emote = false;
    msg.is_system = true;
    
    tui->add_message(msg);
}

void Protocol::handle_channel_add(const std::vector<std::string>& parts) {
    // !chanadd:ChannelName:NumberUsers:Topic
    // OR !chanadd:ChannelName:NumberUsers (topic optional)
    if (parts.size() < 2) return;
    
    std::string channel = parts[1];
    std::string topic = (parts.size() >= 4) ? parts[3] : "";
    
    // Add as unjoined, browsable channel
    tui->add_channel(channel, topic, false, false);
}

void Protocol::handle_user_joined(const std::vector<std::string>& parts) {
    // !usrjoind:channel:username:permissions:allowvoice
    if (parts.size() < 3) return;
    
    std::string channel = parts[1];
    std::string user = parts[2];
    
    tui->add_user_to_channel(channel, user);
    
    ChatMessage msg;
    msg.channel = channel;
    msg.username = "SYSTEM";
    msg.message = user + " has joined the channel";
    msg.timestamp = get_timestamp();
    msg.is_emote = false;
    msg.is_system = true;
    
    tui->add_message(msg);
}

void Protocol::handle_user_left(const std::vector<std::string>& parts) {
    // !usrleft:channel:username:reason (reason optional)
    if (parts.size() < 3) return;
    
    std::string channel = parts[1];
    std::string user = parts[2];
    
    // Rejoin parts[3] onwards with ':' since reason may contain colons
    std::string reason;
    if (parts.size() >= 4) {
        reason = parts[3];
        for (size_t i = 4; i < parts.size(); i++) {
            reason += ":" + parts[i];
        }
        reason = unescape_from_wire(reason);
    }
    
    tui->remove_user_from_channel(channel, user);
    
    ChatMessage msg;
    msg.channel = channel;
    msg.username = "SYSTEM";
    msg.message = user + " has left the channel";
    if (!reason.empty()) {
        msg.message += " (" + reason + ")";
    }
    msg.timestamp = get_timestamp();
    msg.is_emote = false;
    msg.is_system = true;
    
    tui->add_message(msg);
}

void Protocol::handle_topic(const std::vector<std::string>& parts) {
    // !topic: chan:topic
    if (parts.size() < 3) return;
    
    std::string channel = parts[1];
    std::string topic = parts[2];
    
    tui->update_topic(channel, topic);
}

void Protocol::handle_motd(const std::vector<std::string>& parts) {
    // !motd:data
    // Note: MOTD is sent in chunks, we accumulate them and display when complete
    // The parse splits on ':', so we need to rejoin parts[1] onwards
    if (parts.size() < 2) return;
    
    // Ensure there is a pane to display MOTD in the main chat area.
    // "server" is a special reserved channel that is always joined.
    if (tui->get_active_channel().empty()) {
        tui->add_channel("server", "Server messages", false, true);
        tui->set_active_channel("server");
    }
    
    // Reconstruct the full MOTD content by joining all parts after the command with ':'
    std::string motd_raw;
    for (size_t i = 1; i < parts.size(); i++) {
        if (i > 1) motd_raw += ":";
        motd_raw += parts[i];
    }
    
    // Unescape the MOTD content (it may have <colon> and <nl> escapes)
    std::string motd_chunk = unescape_from_wire(motd_raw);
    
    // Accumulate the chunk
    motd_accumulator += motd_chunk;
    
    // Check if we have complete lines to display (look for newlines)
    size_t pos = 0;
    while ((pos = motd_accumulator.find('\n')) != std::string::npos) {
        std::string line = motd_accumulator.substr(0, pos);
        motd_accumulator.erase(0, pos + 1);
        
        // Display the complete line
        if (!line.empty()) {
            ChatMessage msg;
            msg.channel = tui->get_active_channel();
            msg.username = "MOTD";
            msg.message = line;
            msg.timestamp = get_timestamp();
            msg.is_emote = false;
            msg.is_system = true;
            tui->add_message(msg);
        }
    }
}

void Protocol::handle_approval(const std::vector<std::string>& parts) {
    // !apr:command:details
    if (parts.size() < 2) return;
    
    std::string approval_type = parts[1];
    
    if (approval_type == "name") {
        // Authentication approved
        auth_approved = true;
        authenticated = true;
    } else if (approval_type == "jnchn" && parts.size() >= 3) {
        std::string channel = parts[2];
        // Add the channel first if it doesn't exist and mark joined
        tui->add_channel(channel, "", false, true);
        tui->set_channel_joined(channel, true);
        tui->set_active_channel(channel);
        
        // Ensure we show ourself in the channel's user list immediately.
        // Some servers may not echo your own name in the user list response.
        if (!username.empty()) {
            tui->add_user_to_channel(channel, username);
        }
        
        request_user_list(channel);
        request_topic(channel);
    } else if (approval_type == "kick") {
        // Kick command approved - show confirmation in active channel
        ChatMessage msg;
        msg.channel = tui->get_active_channel();
        msg.username = "SYSTEM";
        msg.message = "Kick command executed successfully";
        msg.timestamp = get_timestamp();
        msg.is_emote = false;
        msg.is_system = true;
        tui->add_message(msg);
    }
}

void Protocol::handle_die(const std::vector<std::string>& parts) {
    // !die:channel:kick:reason
    // or
    // !die:channel:ban:reason
    if (parts.size() < 3) return;
    
    std::string channel = parts[1];
    std::string action = parts[2];
    std::string reason = (parts.size() >= 4) ? unescape_from_wire(parts[3]) : "no reason given";

    // Post a persistent notification in the 'server' channel (MOTD area)
    // Ensure the server channel exists and is joined
    tui->add_channel("server", "Server messages", false, true);

    ChatMessage server_msg;
    server_msg.channel = "server";
    server_msg.username = "SYSTEM";
    if (action == "kick") {
        server_msg.message = "You were kicked from #" + channel + ": " + reason;
    } else if (action == "ban") {
        server_msg.message = "You were banned from #" + channel + ": " + reason;
    } else {
        server_msg.message = "You were removed from #" + channel + " (" + action + "): " + reason;
    }
    server_msg.timestamp = get_timestamp();
    server_msg.is_emote = false;
    server_msg.is_system = true;
    tui->add_message(server_msg);

    // Remove the channel from the list
    tui->remove_channel(channel);
}
