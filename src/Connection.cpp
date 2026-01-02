#include "Connection.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <cerrno>

Connection::Connection() : sockfd(-1), ssl(nullptr), ssl_ctx(nullptr), 
                           use_ssl(false), connected(false), port(0) {}

Connection::~Connection() {
    disconnect();
}

bool Connection::init_ssl() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx) {
        ERR_print_errors_fp(stderr);
        return false;
    }
    
    // Don't verify certificate for client (accept self-signed)
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr);
    
    return true;
}

void Connection::cleanup_ssl() {
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ssl = nullptr;
    }
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }
}

bool Connection::connect_to_server(const std::string& host, int p, bool use_ssl_param) {
    hostname = host;
    port = p;
    use_ssl = use_ssl_param;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    // Resolve hostname
    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
        std::cerr << "Failed to resolve hostname: " << host << std::endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }
    
    // Connect
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);
    
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Failed to connect to server" << std::endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }
    
    // SSL handshake if needed
    if (use_ssl) {
        if (!init_ssl()) {
            std::cerr << "Failed to initialize SSL" << std::endl;
            close(sockfd);
            sockfd = -1;
            return false;
        }
        
        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            std::cerr << "Failed to create SSL structure" << std::endl;
            cleanup_ssl();
            close(sockfd);
            sockfd = -1;
            return false;
        }
        
        SSL_set_fd(ssl, sockfd);
        
        if (SSL_connect(ssl) <= 0) {
            std::cerr << "SSL handshake failed" << std::endl;
            ERR_print_errors_fp(stderr);
            cleanup_ssl();
            close(sockfd);
            sockfd = -1;
            return false;
        }
    }
    
    connected = true;
    return true;
}

bool Connection::send_message(const std::string& message) {
    if (!connected || sockfd < 0) {
        return false;
    }
    
    std::string msg = message + "\n";
    size_t total_sent = 0;
    size_t msg_len = msg.length();
    
    // Keep sending until all bytes are sent
    while (total_sent < msg_len) {
        int bytes_sent;
        
        if (use_ssl && ssl) {
            bytes_sent = SSL_write(ssl, msg.c_str() + total_sent, msg_len - total_sent);
            
            if (bytes_sent <= 0) {
                int ssl_err = SSL_get_error(ssl, bytes_sent);
                if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
                    // Temporary condition - retry after a short delay
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                } else {
                    // Real error
                    return false;
                }
            }
        } else {
            bytes_sent = write(sockfd, msg.c_str() + total_sent, msg_len - total_sent);
            
            if (bytes_sent <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Temporary condition - retry after a short delay
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                } else {
                    // Real error
                    return false;
                }
            }
        }
        
        total_sent += bytes_sent;
    }
    
    return true;
}

std::string Connection::receive_message(int timeout_ms) {
    if (!connected || sockfd < 0) {
        return "";
    }
    
    char buffer[131072];  // 128KB to match server buffer size
    int bytes_received;
    
    if (use_ssl && ssl) {
        bytes_received = SSL_read(ssl, buffer, sizeof(buffer) - 1);
        
        if (bytes_received <= 0) {
            int ssl_err = SSL_get_error(ssl, bytes_received);
            
            // Check if it's a real error or just no data available
            if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                // Not an error, just no data available yet
                return "";
            } else if (ssl_err == SSL_ERROR_ZERO_RETURN) {
                // Clean SSL shutdown
                connected = false;
                return "";
            } else {
                // Real error
                connected = false;
                return "";
            }
        }
    } else {
        bytes_received = read(sockfd, buffer, sizeof(buffer) - 1);
        
        if (bytes_received <= 0) {
            connected = false;
            return "";
        }
    }
    
    if (bytes_received <= 0) {
        // This shouldn't happen for SSL after the above check, but keep it for non-SSL
        return "";
    }
    
    buffer[bytes_received] = '\0';
    std::string result(buffer);
    
    return result;
}

void Connection::disconnect() {
    // Proactively wake any blocking readers before tearing down SSL
    if (sockfd >= 0) {
        // Shutdown both directions to interrupt blocking SSL_read/recv in other threads
        ::shutdown(sockfd, SHUT_RDWR);
    }
    if (use_ssl) {
        cleanup_ssl();
    }
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
    connected = false;
}
