#include "Config.h"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

Config::Config() {
    config_path = get_config_path();
    // Initialize defaults
    last_connection.host = "localhost";
    last_connection.port = 1337;
    last_connection.use_ssl = false;
    last_connection.username = "";
}

std::string Config::get_config_path() const {
    // Get home directory
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    return std::string(home) + "/.radi8c";
}

bool Config::load() {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return false; // File doesn't exist yet, that's OK
    }
    
    parse_config_file();
    file.close();
    return true;
}

void Config::parse_config_file() {
    std::ifstream file(config_path);
    if (!file.is_open()) return;
    
    std::string line;
    std::string current_host;
    
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Parse key=value pairs
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (key == "host") {
                last_connection.host = value;
                current_host = value;
            } else if (key == "port") {
                try {
                    last_connection.port = std::stoi(value);
                } catch (...) {
                    last_connection.port = 1337;
                }
            } else if (key == "ssl") {
                last_connection.use_ssl = (value == "true" || value == "1" || value == "yes");
            } else if (key == "username") {
                last_connection.username = value;
            } else if (key == "channels" && !current_host.empty()) {
                // Parse comma-separated channel list
                std::vector<std::string> channels;
                std::istringstream channel_stream(value);
                std::string channel;
                while (std::getline(channel_stream, channel, ',')) {
                    // Trim whitespace
                    channel.erase(0, channel.find_first_not_of(" \t"));
                    channel.erase(channel.find_last_not_of(" \t") + 1);
                    if (!channel.empty()) {
                        channels.push_back(channel);
                    }
                }
                joined_channels_by_host[current_host] = channels;
            }
        }
    }
}

bool Config::save() {
    std::ofstream file(config_path);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# radi8c2 configuration file\n";
    file << "# Last connection settings\n";
    file << "host=" << last_connection.host << "\n";
    file << "port=" << last_connection.port << "\n";
    file << "ssl=" << (last_connection.use_ssl ? "true" : "false") << "\n";
    file << "username=" << last_connection.username << "\n";
    
    // Save joined channels for each host
    for (const auto& entry : joined_channels_by_host) {
        if (!entry.second.empty()) {
            file << "\n# Joined channels for " << entry.first << "\n";
            file << "channels=";
            for (size_t i = 0; i < entry.second.size(); i++) {
                if (i > 0) file << ",";
                file << entry.second[i];
            }
            file << "\n";
        }
    }
    
    file.close();
    return true;
}

void Config::set_last_connection(const std::string& host, int port, bool use_ssl, const std::string& username) {
    last_connection.host = host;
    last_connection.port = port;
    last_connection.use_ssl = use_ssl;
    last_connection.username = username;
}

std::vector<std::string> Config::get_joined_channels(const std::string& host) const {
    auto it = joined_channels_by_host.find(host);
    if (it != joined_channels_by_host.end()) {
        return it->second;
    }
    return std::vector<std::string>();
}

void Config::set_joined_channels(const std::string& host, const std::vector<std::string>& channels) {
    joined_channels_by_host[host] = channels;
}
