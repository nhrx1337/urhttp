#include "../include/Request.hpp"

std::string Request::param(const std::string& key) const {
    auto it = params.find(key);
    if (it != params.end()) return it->second;
    return "";
}

Request parse_request(const std::string& raw_req) {
    Request req;
    std::istringstream ss(raw_req);
    std::string line;
    
    // Parse status line
    if (std::getline(ss, line)) {
        std::istringstream req_line(line);
        std::string http_ver;
        req_line >> req.method >> req.path >> http_ver;
    }

    // Parse headers
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if(line.empty()) 
            break;

        size_t split_pos = line.find(':');
        if (split_pos != std::string::npos) {
            std::string key = line.substr(0, split_pos);
            std::string value = line.substr(split_pos + 1);

            value.erase(0, value.find_first_not_of(" \t"));
            req.headers[key] = value;

            if (key == "Connection" || key == "connection") {
                if (value == "close") {
                    req.keep_alive = false;
                } else if (value == "keep-alive" || value == "Keep-Alive") {
                    req.keep_alive = true;
                }
            }
        }
    }

    // Read remaining body
    std::string body_line;
    while (std::getline(ss, body_line)) {
        if (!req.body.empty())
            req.body += "\n";

        req.body += body_line;
    }

    return req;
}
