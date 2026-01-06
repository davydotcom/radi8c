#ifndef CONNECTION_H
#define CONNECTION_H

#include <string>
#include <mutex>
#include <openssl/ssl.h>
#include <openssl/err.h>

class Connection {
private:
    int sockfd;
    SSL *ssl;
    SSL_CTX *ssl_ctx;
    bool use_ssl;
    bool connected;
    std::string hostname;
    int port;
    std::mutex send_mutex;  // Protect concurrent sends

public:
    Connection();
    ~Connection();
    
    bool connect_to_server(const std::string& host, int port, bool use_ssl);
    bool send_message(const std::string& message);
    std::string receive_message(int timeout_ms = 0);
    bool is_connected() const { return connected; }
    void disconnect();
    
private:
    bool init_ssl();
    void cleanup_ssl();
};

#endif
