#ifndef FILETRANSFER_H
#define FILETRANSFER_H

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <functional>
#include <chrono>

// Forward declaration
class Protocol;
class TUI;

struct FileChunk {
    int sequence;
    std::vector<uint8_t> data;
};

struct OutgoingFileTransfer {
    int fd;
    std::string filename;
    std::string filepath;
    std::string channel;
    size_t file_size;
    int total_chunks;
    int chunks_sent;
    std::chrono::steady_clock::time_point last_status_update;
};

struct IncomingFileTransfer {
    int fd;
    std::string sender;
    std::string filename;
    size_t file_size;  // Total file size in bytes
    size_t bytes_received;  // Total bytes received so far
    std::string temp_filepath;  // Path to .part file
    std::map<int, std::vector<uint8_t>> pending_chunks;  // Store out-of-order chunks temporarily
    int next_sequential_chunk;  // Next chunk we need to write sequentially
    int chunks_received;  // Total count of unique chunks received
    int total_chunks;  // -1 until we receive the final marker
    bool completed;
    std::chrono::steady_clock::time_point last_status_update;
    std::chrono::steady_clock::time_point finalization_requested_time;  // When finalization was first requested
    bool finalization_pending;  // True if waiting for missing chunks
};

class FileTransferManager {
private:
    Protocol* proto;
    TUI* tui;
    
    std::map<int, OutgoingFileTransfer> outgoing_transfers;
    std::map<std::string, std::map<int, IncomingFileTransfer>> incoming_transfers;  // sender -> fd -> transfer
    
    int next_fd;
    std::mutex transfer_mutex;
    
    // Base64 encoding/decoding
    std::string base64_encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> base64_decode(const std::string& encoded);
    
    // Helper to get download directory
    std::string get_download_dir();
    
public:
    FileTransferManager(Protocol* protocol, TUI* ui);
    ~FileTransferManager();
    
    // Send a file
    bool send_file(const std::string& filepath, const std::string& channel);
    
    // Receive file chunks
    void receive_chunk(const std::string& sender, int fd, int sequence, const std::string& filename, size_t file_size, const std::string& base64_data);
    
    // Finalize a file transfer
    void finalize_transfer(const std::string& sender, int fd, int total_chunks);
    
    // Background sending and finalization (call periodically or in thread)
    void process_outgoing_transfers();
    void process_pending_finalizations();
};

#endif
