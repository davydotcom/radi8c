#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <map>

struct ConnectionConfig {
    std::string host;
    int port;
    bool use_ssl;
    std::string username;
    // Note: password is NOT stored for security
};

class Config {
private:
    std::string config_path;
    ConnectionConfig last_connection;
    // Map of hostname -> list of channels that were joined
    std::map<std::string, std::vector<std::string>> joined_channels_by_host;
    
    std::string get_config_path() const;
    void parse_config_file();
    
public:
    Config();
    
    // Load config from ~/.radi8c
    bool load();
    
    // Save config to ~/.radi8c
    bool save();
    
    // Get last connection settings
    ConnectionConfig get_last_connection() const { return last_connection; }
    
    // Set last connection (call after successful connection)
    void set_last_connection(const std::string& host, int port, bool use_ssl, const std::string& username);
    
    // Get joined channels for a hostname
    std::vector<std::string> get_joined_channels(const std::string& host) const;
    
    // Set joined channels for a hostname (call before disconnect)
    void set_joined_channels(const std::string& host, const std::vector<std::string>& channels);
};

#endif
