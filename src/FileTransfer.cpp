#include "FileTransfer.h"
#include "Protocol.h"
#include "TUI.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <ctime>
#include <iomanip>
#include <cmath>

// Helper function to format file size in human-readable format
static std::string format_file_size(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    std::ostringstream oss;
    if (unit_index == 0) {
        oss << static_cast<int>(size) << " " << units[unit_index];
    } else {
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
    }
    return oss.str();
}

// Base64 encoding table
static const std::string base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

FileTransferManager::FileTransferManager(Protocol* protocol, TUI* ui)
    : proto(protocol), tui(ui), next_fd(1) {}

FileTransferManager::~FileTransferManager() {}

std::string FileTransferManager::base64_encode(const std::vector<uint8_t>& data) {
    std::string ret;
    int i = 0;
    int j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    size_t in_len = data.size();
    
    while (in_len--) {
        char_array_3[i++] = data[j++];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];
        
        while((i++ < 3))
            ret += '=';
    }
    
    return ret;
}

std::vector<uint8_t> FileTransferManager::base64_decode(const std::string& encoded) {
    size_t in_len = encoded.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::vector<uint8_t> ret;
    
    while (in_len-- && (encoded[in_] != '=') && 
           (isalnum(encoded[in_]) || (encoded[in_] == '+') || (encoded[in_] == '/'))) {
        char_array_4[i++] = encoded[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }
    
    if (i) {
        for (j = 0; j < i; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        
        for (j = 0; j < i - 1; j++)
            ret.push_back(char_array_3[j]);
    }
    
    return ret;
}

std::string FileTransferManager::get_download_dir() {
    const char* home = getenv("HOME");
    if (!home) home = getpwuid(getuid())->pw_dir;
    
    std::string dir = std::string(home) + "/radi8-files";
    
    // Create directory if it doesn't exist
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
        mkdir(dir.c_str(), 0755);
    }
    
    return dir;
}

bool FileTransferManager::send_file(const std::string& filepath, const std::string& channel) {
    std::lock_guard<std::mutex> lock(transfer_mutex);
    
    // Check if file exists and get size
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.close();
    
    // Get filename without path
    std::string filename = filepath;
    size_t last_slash = filepath.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        filename = filepath.substr(last_slash + 1);
    }
    
    // Calculate total chunks (16KB per chunk)
    const int CHUNK_SIZE = 16384;  // 16KB
    int total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    if (total_chunks == 0) total_chunks = 1;  // Empty files need at least 1 chunk
    
    // Create outgoing transfer
    OutgoingFileTransfer transfer;
    transfer.fd = next_fd++;
    transfer.filename = filename;
    transfer.filepath = filepath;
    transfer.channel = channel;
    transfer.file_size = file_size;
    transfer.total_chunks = total_chunks;
    transfer.chunks_sent = 0;
    
    outgoing_transfers[transfer.fd] = transfer;
    
    // Show status in UI
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    std::ostringstream oss;
    oss << "[" << std::setfill('0') << std::setw(2) << local_time->tm_hour
        << ":" << std::setfill('0') << std::setw(2) << local_time->tm_min << "]";
    
    ChatMessage msg;
    msg.channel = channel;
    msg.username = "SYSTEM";
    msg.message = "Sending File: " + filename;
    msg.timestamp = oss.str();
    msg.is_emote = false;
    msg.is_system = true;
    tui->add_message(msg);
    
    return true;
}

void FileTransferManager::process_outgoing_transfers() {
    std::lock_guard<std::mutex> lock(transfer_mutex);
    
    const int CHUNK_SIZE = 16384;  // 16KB
    
    for (auto& pair : outgoing_transfers) {
        OutgoingFileTransfer& transfer = pair.second;
        
        if (transfer.chunks_sent >= transfer.total_chunks) {
            continue;  // Already sent all chunks
        }
        
        // Open file and read one chunk
        std::ifstream file(transfer.filepath, std::ios::binary);
        if (!file.is_open()) {
            // File error - skip this transfer
            continue;
        }
        
        int seq = transfer.chunks_sent;
        size_t offset = seq * CHUNK_SIZE;
        size_t chunk_size = std::min((size_t)CHUNK_SIZE, transfer.file_size - offset);
        
        // Seek to position and read chunk
        file.seekg(offset, std::ios::beg);
        std::vector<uint8_t> chunk_data(chunk_size);
        file.read(reinterpret_cast<char*>(chunk_data.data()), chunk_size);
        file.close();
        
        std::string base64_chunk = base64_encode(chunk_data);
        
        // DEBUG: Log chunk sending
        // std::ofstream debug_log("/tmp/radi8_debug.log", std::ios::app);
        // debug_log << "[DEBUG] Sending chunk " << seq << ": raw=" << chunk_size 
        //           << " bytes, base64=" << base64_chunk.size() << " bytes" << std::endl;
        // debug_log.close();
        
        // Format message
        std::string message;
        if (seq == 0) {
            // First chunk: <file|fd|filename|filesize>base64
            message = "<file|" + std::to_string(transfer.fd) + "|" + transfer.filename + "|" + std::to_string(transfer.file_size) + ">" + base64_chunk;
        } else {
            // Subsequent chunks: <file|fd|seq>base64
            message = "<file|" + std::to_string(transfer.fd) + "|" + std::to_string(seq) + ">" + base64_chunk;
        }
        
        // Send through protocol
        proto->send_message(transfer.channel, message);
        
        transfer.chunks_sent++;
        
        // Update status bar with progress
        size_t bytes_sent = std::min((size_t)(transfer.chunks_sent * CHUNK_SIZE), transfer.file_size);
        std::string progress = "Sending " + transfer.filename + ": " + 
                              format_file_size(bytes_sent) + " / " + 
                              format_file_size(transfer.file_size);
        tui->set_status(progress);
        
        // If all chunks sent, send final marker
        if (transfer.chunks_sent >= transfer.total_chunks) {
            std::string final_msg = "</file|" + std::to_string(transfer.fd) + "|" + std::to_string(transfer.total_chunks) + ">";
            proto->send_message(transfer.channel, final_msg);
            
            // Show completion status
            std::time_t now = std::time(nullptr);
            std::tm* local_time = std::localtime(&now);
            std::ostringstream oss;
            oss << "[" << std::setfill('0') << std::setw(2) << local_time->tm_hour
                << ":" << std::setfill('0') << std::setw(2) << local_time->tm_min << "]";
            
            ChatMessage msg;
            msg.channel = transfer.channel;
            msg.username = "SYSTEM";
            msg.message = "Sending File Completed.";
            msg.timestamp = oss.str();
            msg.is_emote = false;
            msg.is_system = true;
            tui->add_message(msg);
            
            // Clear status bar
            tui->set_status("");
        }
    }
    
    // Clean up completed transfers
    auto it = outgoing_transfers.begin();
    while (it != outgoing_transfers.end()) {
        if (it->second.chunks_sent >= it->second.total_chunks) {
            it = outgoing_transfers.erase(it);
        } else {
            ++it;
        }
    }
}

void FileTransferManager::receive_chunk(const std::string& sender, int fd, int sequence, 
                                       const std::string& filename, size_t file_size, const std::string& base64_data) {
    std::lock_guard<std::mutex> lock(transfer_mutex);
    
    // Get or create incoming transfer
    IncomingFileTransfer& transfer = incoming_transfers[sender][fd];
    
    if (transfer.fd == 0) {
        // New transfer - create .part file
        transfer.fd = fd;
        transfer.sender = sender;
        transfer.filename = filename;
        transfer.file_size = file_size;
        transfer.bytes_received = 0;
        transfer.total_chunks = -1;
        transfer.completed = false;
        transfer.next_sequential_chunk = 0;
        transfer.chunks_received = 0;
        
        // Create temp file path
        transfer.temp_filepath = get_download_dir() + "/" + filename + ".part";
        
        // Handle temp file conflicts
        int counter = 1;
        while (access(transfer.temp_filepath.c_str(), F_OK) == 0) {
            transfer.temp_filepath = get_download_dir() + "/" + filename + ".part." + std::to_string(counter++);
        }
        
        // Create empty part file
        std::ofstream part_file(transfer.temp_filepath, std::ios::binary);
        part_file.close();
        
        // Show status with file size
        std::time_t now = std::time(nullptr);
        std::tm* local_time = std::localtime(&now);
        std::ostringstream oss;
        oss << "[" << std::setfill('0') << std::setw(2) << local_time->tm_hour
            << ":" << std::setfill('0') << std::setw(2) << local_time->tm_min << "]";
        
        ChatMessage msg;
        msg.channel = tui->get_active_channel();
        msg.username = "SYSTEM";
        if (file_size > 0) {
            msg.message = "Receiving File: " + filename + " (" + format_file_size(file_size) + ") from " + sender;
        } else {
            msg.message = "Receiving File: " + filename + " from " + sender;
        }
        msg.timestamp = oss.str();
        msg.is_emote = false;
        msg.is_system = true;
        tui->add_message(msg);
    }
    
    // Decode chunk data
    std::vector<uint8_t> chunk_data = base64_decode(base64_data);
    
    // DEBUG: Log chunk reception to file
    // std::ofstream debug_log("/tmp/radi8_debug.log", std::ios::app);
    // debug_log << "[DEBUG] Received chunk " << sequence << " from " << sender 
    //           << " (" << chunk_data.size() << " bytes, expecting seq " 
    //           << transfer.next_sequential_chunk << ")" << std::endl;
    // debug_log.close();
    
    // Check if this is the next chunk we're expecting
    if (sequence == transfer.next_sequential_chunk) {
        // Write this chunk immediately
        std::ofstream part_file(transfer.temp_filepath, std::ios::binary | std::ios::app);
        if (part_file.is_open()) {
            part_file.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
            part_file.close();
        }
        transfer.chunks_received++;
        transfer.bytes_received += chunk_data.size();
        transfer.next_sequential_chunk++;
        
        // Write any pending chunks that are now sequential
        while (transfer.pending_chunks.find(transfer.next_sequential_chunk) != transfer.pending_chunks.end()) {
            std::vector<uint8_t>& pending_data = transfer.pending_chunks[transfer.next_sequential_chunk];
            std::ofstream part_file(transfer.temp_filepath, std::ios::binary | std::ios::app);
            if (part_file.is_open()) {
                part_file.write(reinterpret_cast<const char*>(pending_data.data()), pending_data.size());
                part_file.close();
            }
            transfer.bytes_received += pending_data.size();
            transfer.pending_chunks.erase(transfer.next_sequential_chunk);
            // Don't increment chunks_received here - already counted when added to pending
            transfer.next_sequential_chunk++;
        }
        
        // Update status bar with progress
        if (transfer.file_size > 0) {
            std::string progress = "Receiving " + transfer.filename + ": " + 
                                  format_file_size(transfer.bytes_received) + " / " + 
                                  format_file_size(transfer.file_size);
            tui->set_status(progress);
        }
    } else if (sequence > transfer.next_sequential_chunk) {
        // Out of order - store for later (only if not already received)
        if (transfer.pending_chunks.find(sequence) == transfer.pending_chunks.end()) {
            transfer.pending_chunks[sequence] = chunk_data;
            transfer.chunks_received++;  // Count it now, even though not written yet
        }
    }
    // If sequence < next_sequential_chunk, it's a duplicate - ignore it
}

void FileTransferManager::finalize_transfer(const std::string& sender, int fd, int total_chunks) {
    std::lock_guard<std::mutex> lock(transfer_mutex);
    
    auto sender_it = incoming_transfers.find(sender);
    if (sender_it == incoming_transfers.end()) return;
    
    auto transfer_it = sender_it->second.find(fd);
    if (transfer_it == sender_it->second.end()) return;
    
    IncomingFileTransfer& transfer = transfer_it->second;
    transfer.total_chunks = total_chunks;
    
    // DEBUG: Log finalization to file
    std::ofstream debug_log("/tmp/radi8_debug.log", std::ios::app);
    debug_log << "[DEBUG] Finalizing transfer: " << transfer.filename 
              << ", received=" << transfer.chunks_received 
              << ", total=" << total_chunks 
              << ", pending=" << transfer.pending_chunks.size() << std::endl;
    debug_log.close();
    
    // Verify we received all expected chunks
    if (transfer.chunks_received != total_chunks) {
        // Missing chunks - show error with details
        ChatMessage msg;
        msg.channel = tui->get_active_channel();
        msg.username = "ERROR";
        msg.message = "File transfer incomplete: " + transfer.filename + 
                      " (received " + std::to_string(transfer.chunks_received) + 
                      " of " + std::to_string(total_chunks) + " chunks)";
        msg.timestamp = "";
        msg.is_emote = false;
        msg.is_system = true;
        tui->add_message(msg);
        
        // Clean up incomplete transfer
        remove(transfer.temp_filepath.c_str());
        sender_it->second.erase(transfer_it);
        return;
    }
    
    // Determine final output path
    std::string output_path = get_download_dir() + "/" + transfer.filename;
    
    // Handle filename conflicts
    int counter = 1;
    std::string base_name = transfer.filename;
    size_t dot_pos = base_name.find_last_of('.');
    std::string name_part = (dot_pos != std::string::npos) ? base_name.substr(0, dot_pos) : base_name;
    std::string ext_part = (dot_pos != std::string::npos) ? base_name.substr(dot_pos) : "";
    
    while (access(output_path.c_str(), F_OK) == 0) {
        output_path = get_download_dir() + "/" + name_part + "_" + std::to_string(counter++) + ext_part;
    }
    
    // Rename .part file to final filename
    if (rename(transfer.temp_filepath.c_str(), output_path.c_str()) == 0) {
        // Show completion
        std::time_t now = std::time(nullptr);
        std::tm* local_time = std::localtime(&now);
        std::ostringstream oss;
        oss << "[" << std::setfill('0') << std::setw(2) << local_time->tm_hour
            << ":" << std::setfill('0') << std::setw(2) << local_time->tm_min << "]";
        
        ChatMessage msg;
        msg.channel = tui->get_active_channel();
        msg.username = "SYSTEM";
        msg.message = "Receive Completed: " + transfer.filename + " -> " + output_path;
        msg.open_path = output_path;  // make message itself clickable in chat
        msg.timestamp = oss.str();
        msg.is_emote = false;
        msg.is_system = true;
        tui->add_message(msg);
        
        transfer.completed = true;
        
        // Track for /open command
        tui->set_last_download(output_path);
        
        // Clear status bar
        tui->set_status("");
    } else {
        // Rename failed - show error
        ChatMessage msg;
        msg.channel = tui->get_active_channel();
        msg.username = "ERROR";
        msg.message = "Failed to save file: " + transfer.filename;
        msg.timestamp = "";
        msg.is_emote = false;
        msg.is_system = true;
        tui->add_message(msg);
        
        // Clear status bar
        tui->set_status("");
    }
    
    // Clean up
    sender_it->second.erase(transfer_it);
}
