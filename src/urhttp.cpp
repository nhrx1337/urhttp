#include "../include/urhttp.hpp"

Response urhttp::handle_not_found() {
    Response res;
    res.status_code = 404;
    res.body = "404 Not Found";
    return res;
}

// Dynamic path matching (e.g. /users/:id)
bool urhttp::match_route(const std::string& pattern, const std::string& path,
                          std::unordered_map<std::string, std::string>& params) {
    std::string_view pat(pattern);
    std::string_view pth(path);

    size_t pat_idx = 0, pth_idx = 0;

    while (pat_idx < pat.size() || pth_idx < pth.size()) {
        // Skip leading and duplicate slashes
        while (pat_idx < pat.size() && pat[pat_idx] == '/') pat_idx++;
        while (pth_idx < pth.size() && pth[pth_idx] == '/') pth_idx++;

        if (pat_idx == pat.size() && pth_idx == pth.size()) {
            return true;
        }

        if (pat_idx == pat.size() || pth_idx == pth.size()) {
            return false;
        }

        // Find next segment boundary
        size_t next_pat = pat.find('/', pat_idx);
        size_t next_pth = pth.find('/', pth_idx);

        if (next_pat == std::string_view::npos) next_pat = pat.size();
        if (next_pth == std::string_view::npos) next_pth = pth.size();

        std::string_view pat_seg = pat.substr(pat_idx, next_pat - pat_idx);
        std::string_view pth_seg = pth.substr(pth_idx, next_pth - pth_idx);

        // Dynamic path parameter (e.g. :id)
        if (!pat_seg.empty() && pat_seg[0] == ':') {
            std::string_view key = pat_seg.substr(1);
            params[std::string(key)] = std::string(pth_seg);
        } 
        else if (pat_seg != pth_seg) {
            return false;
        }

        pat_idx = next_pat;
        pth_idx = next_pth;
    }

    return true;
}


void urhttp::add_accept_request() {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) return;

    auto ctx = std::make_unique<ConnectionCtx>();
    ctx->type = EventType::ACCEPT;
    ctx->client_socket = -1;

    io_uring_prep_accept(sqe, server_socket, nullptr, nullptr, 0);
    io_uring_sqe_set_data(sqe, ctx.release());
    io_uring_submit(&ring);
}

void urhttp::add_read_request(int client_socket) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) return;

    auto ctx = std::make_unique<ConnectionCtx>();
    ctx->type = EventType::READ;
    ctx->client_socket = client_socket;

    io_uring_prep_recv(sqe, client_socket, ctx->buffer, sizeof ctx->buffer, 0);
    io_uring_sqe_set_data(sqe, ctx.release());
    io_uring_submit(&ring);
}

void urhttp::add_file_read_request(int client_socket, 
        const std::string& filepath, const std::string& content_type, bool keep_alive) {
    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        Response res = handle_not_found();
        add_write_request(client_socket, res.build(keep_alive), keep_alive);
        return;
    }

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        close(fd);
        return;
    }

    auto ctx = std::make_unique<ConnectionCtx>();
    ctx->total_file_size = std::filesystem::file_size(filepath);
    ctx->type = EventType::FILE_READ;
    ctx->client_socket = client_socket;
    ctx->file_fd = fd;
    ctx->content_type = content_type;
    ctx->keep_alive = keep_alive;
    ctx->is_chunked_file = true;

    io_uring_prep_read(sqe, fd, ctx->buffer, sizeof(ctx->buffer), ctx->file_offset);
    io_uring_sqe_set_data(sqe, ctx.release());
    io_uring_submit(&ring);
}

void urhttp::add_write_request(int client_socket, const std::string& response, bool keep_alive) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) return;

    auto ctx = std::make_unique<ConnectionCtx>();
    ctx->type = EventType::WRITE;
    ctx->client_socket = client_socket;
    ctx->response_data = response;
    ctx->keep_alive = keep_alive;


    io_uring_prep_send(sqe, client_socket, ctx->response_data.c_str(), ctx->response_data.size(), 0);
    io_uring_sqe_set_data(sqe, ctx.release());
    io_uring_submit(&ring);
}

void urhttp::process_request(ConnectionCtx* ctx, int bytes_read) {
    std::string raw_req(ctx->buffer, bytes_read);
    Request req = parse_request(raw_req);
    
    
    if (show_logs) {
        std::cout << "Method: " << req.method << '\n' 
            << "Path: " << req.path << '\n';
        for (const auto& header : req.headers) {
            std::cout << header.first << ": " << header.second << '\n';
        }
        if (!req.body.empty())
            std::cout << "Body: " << req.body << '\n';
    }

    // Currently only supporting GET and POST
    std::string method = req.method;
    for (char &c : method) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    if (method != "get" && method != "post") {
        Response res;
        res.status_code = 405;
        res.content_type = "text/html";
        add_write_request(ctx->client_socket, res.build(req.keep_alive), req.keep_alive);
        return;
    }

    // Static file routes
    auto file_it = file_routes.find(req.path);
    if (file_it != file_routes.end()) {
        add_file_read_request(ctx->client_socket, 
                file_it->second.first, file_it->second.second, req.keep_alive);
        return;
    }

    // Exact match routes
    auto it = routes.find(req.path);
    if (it != routes.end()) {
        Response res;
        res.status_code = 200;
        res.content_type = "text/html";
        it->second(req, res);
        add_write_request(ctx->client_socket, res.build(req.keep_alive), req.keep_alive);
        return;
    }

    // Dynamic pattern routes
    for (const auto& [pattern, handler] : routes) {
        std::unordered_map<std::string, std::string> params;

        if (match_route(pattern, req.path, params)) {
            req.params = params;

            Response res;
            res.status_code = 200;
            res.content_type = "text/html";
            handler(req, res);

            add_write_request(ctx->client_socket, res.build(req.keep_alive), req.keep_alive);
            return;
        }
    }

    Response res = handle_not_found();
    add_write_request(ctx->client_socket, res.build(req.keep_alive), req.keep_alive);
}

urhttp::urhttp(const char* port) {
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        std::cerr << "Failed to init io_uring\n";
        exit(1);
    }
    this->port = port;

    struct addrinfo hints, *res;

    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(NULL, port, &hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo error: " << gai_strerror(status) << '\n';
        exit(1);
    }
    
    server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_socket == -1) {
        std::cerr << "Socket creation failed" << ": " << strerror(errno) << '\n';
        freeaddrinfo(res);
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, 
                &reuse, sizeof(reuse)) == -1) {
        std::cerr << "setsockopt failed" << ": " << strerror(errno) << '\n';
        freeaddrinfo(res);
        exit(1);
    }
    
    if (bind(server_socket, res->ai_addr, res->ai_addrlen) == -1) {
        std::cerr << "Bind failed" << ": " << strerror(errno) << '\n';
        freeaddrinfo(res);
        exit(1);
    }
    
    freeaddrinfo(res);
    
    if (listen(server_socket, BACKLOG) == -1) {
        std::cerr << "Listen failed" << ": " << strerror(errno) << '\n';
        exit(1);
    }
}

urhttp::~urhttp() {
    if (server_socket != -1)
        close(server_socket);
    io_uring_queue_exit(&ring);
}

void urhttp::route(const std::string& path, RouteHandler handler) {
    routes[path] = handler;
}

void urhttp::route_file(const std::string& path, const std::string& filepath) {
    std::string content_type = "text/html";
    size_t dot_pos = filepath.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string ext = filepath.substr(dot_pos + 1);
        if (ext == "html" || ext == "htm") content_type = "text/html";
        else if (ext == "css")  content_type = "text/css";
        else if (ext == "js")   content_type = "application/javascript";
        else if (ext == "json") content_type = "application/json";
        else if (ext == "png")  content_type = "image/png";
        else if (ext == "pdf")  content_type = "application/pdf";
        else if (ext == "jpg" || ext == "jpeg") content_type = "image/jpeg";
        else if (ext == "gif")  content_type = "image/gif";
        else content_type = "application/octet-stream";
    }
    file_routes[path] = {filepath, content_type};
}

// Recursively map files in directory
void urhttp::static_dir(const std::string& path, const std::string& dir_path) {
    if (!std::filesystem::exists(dir_path) || 
            !std::filesystem::is_directory(dir_path)) {
        std::cerr << "Error: there is no static file -> " << dir_path << '\n';
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
        if (std::filesystem::is_regular_file(entry.path())) {
            std::string relative_path = entry.path().string();
            std::string sub_path = relative_path.substr(dir_path.size());
            std::string url_route = (path == "/" ? "" : path) + sub_path;
            
            route_file(url_route, relative_path);

            if (sub_path == "/index.html") {
                route_file("/", relative_path);
            }
        }
    }
}

// Event loop driven by io_uring
void urhttp::run() {
    struct io_uring_cqe *cqe;
    
    add_accept_request();
    if (show_logs) {
        std::cout << "Server running on port: " << port << '\n'; 
    }

    while (true) {
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            std::cerr << "io_uring_wait_cqe failed\n";
            break;
        }

        ConnectionCtx* raw_ctx = (ConnectionCtx*)io_uring_cqe_get_data(cqe);
        std::unique_ptr<ConnectionCtx> ctx(raw_ctx);

        if (cqe->res < 0) {
            std::cerr << "Async operation failed: " << cqe->res << "\n";
            if (ctx && ctx->client_socket != -1) 
                close(ctx->client_socket);
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        if (ctx) {
            switch (ctx->type) {
                case EventType::ACCEPT: {
                    int client_socket = cqe->res;
                    
                    add_accept_request(); // Keep accepting new connections
                    add_read_request(client_socket);
                    break;
                }
                case EventType::READ: {
                    int bytes_read = cqe->res;
                    if (bytes_read == 0) {
                        close(ctx->client_socket);
                    } else {
                        process_request(ctx.get(), bytes_read);
                    }
                    break;
                }

                case EventType::FILE_READ: {
                    int file_bytes = cqe->res;

                    if (file_bytes < 0) {
                        if (ctx->file_fd != -1) close(ctx->file_fd);
                        Response res = handle_not_found();
                        add_write_request(ctx->client_socket, res.build(ctx->keep_alive), ctx->keep_alive);
                        break;
                    }

                    std::string chunk_response = "";
                    
                    // Build initial headers on first chunk
                    if (ctx->file_offset == 0) {
                        Response res;
                        res.status_code = 200;
                        res.content_type = ctx->content_type;
                        res.is_chunked = true;

                        chunk_response += res.build(ctx->keep_alive);   
                    }

                    // Format chunk payload
                    if (file_bytes > 0) {
                        std::stringstream ss;
                        ss << std::hex << file_bytes;
                        chunk_response += ss.str() + "\r\n" + std::string(ctx->buffer, file_bytes) + "\r\n";
                        
                        ctx->file_offset += file_bytes;
                    }

                    // Terminate chunked payload if complete
                    if (ctx->file_offset >= ctx->total_file_size) {
                        chunk_response += "0\r\n\r\n";
                    }

                    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                    if (!sqe) {
                        if (ctx->file_fd != -1) close(ctx->file_fd);
                        close(ctx->client_socket);
                        break;
                    }

                    ctx->type = EventType::WRITE;
                    ctx->response_data = std::move(chunk_response);

                    io_uring_prep_send(sqe, ctx->client_socket, 
                            ctx->response_data.c_str(), ctx->response_data.size(), 0);
                    io_uring_sqe_set_data(sqe, ctx.release());
                    io_uring_submit(&ring);

                    break;
                }

                case EventType::WRITE: {
                    // Loop back to read next chunk if streaming a file
                    if (ctx->is_chunked_file && ctx->file_fd != -1) {
                        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                        if (!sqe) {
                            close(ctx->file_fd);
                            close(ctx->client_socket);
                            break;
                        }

                        ctx->type = EventType::FILE_READ;
                        
                        io_uring_prep_read(sqe, ctx->file_fd, ctx->buffer, sizeof(ctx->buffer), ctx->file_offset);
                        io_uring_sqe_set_data(sqe, ctx.release());
                        io_uring_submit(&ring);
                        
                        break; 
                    }

                    if (ctx->file_fd != -1) {
                        close(ctx->file_fd);
                        ctx->file_fd = -1;
                    }

                    if (ctx->keep_alive) {
                        add_read_request(ctx->client_socket);
                    } else {
                        close(ctx->client_socket);
                    }

                    break;
                }
            }
        }

        io_uring_cqe_seen(&ring, cqe);
    }
}

void urhttp::set_logs(bool status) {
    show_logs = status;
}
