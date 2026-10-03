#ifndef CONNECTION_H
#define CONNECTION_H

#include <string>
#include <mutex>
#include <atomic>
#include <openssl/ssl.h>
#include <openssl/err.h>

class Connection {
private:
    int sockfd;
    SSL *ssl;
    SSL_CTX *ssl_ctx;
    bool use_ssl;
    std::atomic<bool> connected;
    std::string hostname;
    int port;
    std::string last_error;
    std::mutex send_mutex;  // Keeps each outgoing line contiguous across sender threads
    std::mutex io_mutex;    // OpenSSL forbids concurrent SSL_read/SSL_write on one SSL*

public:
    Connection();
    ~Connection();
    
    bool connect_to_server(const std::string& host, int port, bool use_ssl);
    bool send_message(const std::string& message);
    std::string receive_message(int timeout_ms = 100);
    bool is_connected() const { return connected; }
    const std::string& get_last_error() const { return last_error; }
    
    // Wakes threads blocked on this connection. Safe to call from any thread.
    void interrupt();
    // Releases the socket and SSL state. Only call once I/O threads are joined.
    void disconnect();
    
private:
    bool init_ssl();
    void cleanup_ssl();
};

#endif
