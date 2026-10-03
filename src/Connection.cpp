#include "Connection.h"
#include <cstring>
#include <thread>
#include <chrono>

#ifdef _WIN32
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define close closesocket
    #define SHUT_RDWR SD_BOTH
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/select.h>
    #include <cerrno>
#endif

static bool socket_would_block() {
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static void set_nonblocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
}

// Returns true if the socket became ready within timeout_ms.
static bool wait_for_socket(int fd, bool for_write, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ready = select(fd + 1, for_write ? nullptr : &set, for_write ? &set : nullptr, nullptr, &tv);
    return ready > 0;
}

Connection::Connection() : sockfd(-1), ssl(nullptr), ssl_ctx(nullptr), 
                           use_ssl(false), connected(false), port(0) {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

Connection::~Connection() {
    disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool Connection::init_ssl() {
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx) {
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
    last_error.clear();
    
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    struct addrinfo* results = nullptr;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &results) != 0 || !results) {
        last_error = "Could not resolve host " + host;
        return false;
    }
    
    for (struct addrinfo* ai = results; ai; ai = ai->ai_next) {
        int fd = static_cast<int>(socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == 0) {
            sockfd = fd;
            break;
        }
        close(fd);
    }
    freeaddrinfo(results);
    
    if (sockfd < 0) {
        last_error = "Could not connect to " + host + ":" + port_str;
        return false;
    }
    
    // SSL handshake if needed (performed while the socket is still blocking)
    if (use_ssl) {
        if (!init_ssl() || !(ssl = SSL_new(ssl_ctx))) {
            last_error = "Failed to initialize SSL";
            cleanup_ssl();
            close(sockfd);
            sockfd = -1;
            return false;
        }
        
        SSL_set_fd(ssl, sockfd);
        SSL_set_tlsext_host_name(ssl, host.c_str());
        
        if (SSL_connect(ssl) <= 0) {
            last_error = "SSL handshake failed (is SSL enabled on the server?)";
            ERR_clear_error();
            cleanup_ssl();
            close(sockfd);
            sockfd = -1;
            return false;
        }
    }
    
    set_nonblocking(sockfd);
    connected = true;
    return true;
}

bool Connection::send_message(const std::string& message) {
    std::lock_guard<std::mutex> send_lock(send_mutex);
    
    std::string msg = message + "\n";
    size_t total_sent = 0;
    
    while (total_sent < msg.length()) {
        if (!connected) {
            return false;
        }
        
        int bytes_sent;
        bool would_block = false;
        {
            std::lock_guard<std::mutex> io_lock(io_mutex);
            if (sockfd < 0) {
                return false;
            }
            int remaining = static_cast<int>(msg.length() - total_sent);
            if (ssl) {
                bytes_sent = SSL_write(ssl, msg.data() + total_sent, remaining);
                if (bytes_sent <= 0) {
                    int ssl_err = SSL_get_error(ssl, bytes_sent);
                    if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
                        would_block = true;
                    } else {
                        connected = false;
                        return false;
                    }
                }
            } else {
                bytes_sent = send(sockfd, msg.data() + total_sent, remaining, 0);
                if (bytes_sent < 0) {
                    if (socket_would_block()) {
                        would_block = true;
                    } else {
                        connected = false;
                        return false;
                    }
                }
            }
        }
        
        if (would_block) {
            // Released io_mutex so the reader can make progress meanwhile
            wait_for_socket(sockfd, true, 50);
            continue;
        }
        
        total_sent += bytes_sent;
    }
    
    return true;
}

std::string Connection::receive_message(int timeout_ms) {
    if (!connected || sockfd < 0) {
        return "";
    }
    
    bool buffered = false;
    {
        std::lock_guard<std::mutex> io_lock(io_mutex);
        buffered = ssl && SSL_pending(ssl) > 0;
    }
    if (!buffered && !wait_for_socket(sockfd, false, timeout_ms)) {
        return "";
    }
    
    char buffer[65536];
    int bytes_received;
    
    std::lock_guard<std::mutex> io_lock(io_mutex);
    if (ssl) {
        bytes_received = SSL_read(ssl, buffer, sizeof(buffer));
        if (bytes_received <= 0) {
            int ssl_err = SSL_get_error(ssl, bytes_received);
            if (ssl_err != SSL_ERROR_WANT_READ && ssl_err != SSL_ERROR_WANT_WRITE) {
                connected = false;
            }
            return "";
        }
    } else {
        bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0 || !socket_would_block()) {
                connected = false;
            }
            return "";
        }
    }
    
    return std::string(buffer, bytes_received);
}

void Connection::interrupt() {
    connected = false;
    if (sockfd >= 0) {
        ::shutdown(sockfd, SHUT_RDWR);
    }
}

void Connection::disconnect() {
    connected = false;
    std::lock_guard<std::mutex> io_lock(io_mutex);
    cleanup_ssl();
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
}
