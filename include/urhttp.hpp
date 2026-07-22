#ifndef URHTTP_HPP
#define URHTTP_HPP

#include <iostream>
#include <cstring>
#include <string_view>
#include <memory>
#include <functional>
#include <liburing.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <fstream>
#include <filesystem>

#include "Request.hpp"
#include "Response.hpp"

// Async operations tracked by io_uring
enum class EventType {
    ACCEPT, 
    READ, 
    WRITE, 
    FILE_READ
};

// State context associated with each io_uring SQE/CQE
struct ConnectionCtx {
    EventType type;
    int client_socket;
    int file_fd = -1;
    off_t file_offset = 0;
    off_t total_file_size = 0;

    char buffer[16384];
    std::string response_data;
    std::string content_type;
    bool keep_alive = false;
    bool is_chunked_file = false;

    ~ConnectionCtx() {
        if (file_fd != -1) {
            close(file_fd);
        }
    }
};

class urhttp {
private:
    bool show_logs = false;

    static constexpr int BACKLOG = 512;
    static constexpr int BUFFER_SIZE = 8192;
    static constexpr int QUEUE_DEPTH = 1024;
    const char* port;

    int server_socket = -1;
    struct io_uring ring;

    typedef std::function<void(const Request&, Response&)> RouteHandler;
    std::unordered_map<std::string, RouteHandler> routes;
    std::unordered_map<std::string, std::pair<std::string, std::string>> file_routes;
    
    Response handle_not_found();

    bool match_route(const std::string& pattern, const std::string& path, 
            std::unordered_map<std::string, std::string>& params);

    // Helpers to queue io_uring SQEs
    void add_accept_request();
    void add_read_request(int client_socket);
    void add_file_read_request(int client_socket, 
            const std::string& filepath, const std::string& content_type, bool keep_alive);
    void add_write_request(int client_socket, const std::string& response, bool keep_alive);
    void process_request(ConnectionCtx* ctx, int bytes_read);
public:
    urhttp(const char* port);
    ~urhttp();
    
    void route(const std::string& path, RouteHandler handler);
    void route_file(const std::string& path, const std::string& filepath);
    void static_dir(const std::string& path, const std::string& dir_path);
    void run();

    void set_logs(bool status);
};

#endif // URHTTP_HPP
