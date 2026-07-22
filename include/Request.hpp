#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>
#include <sstream>
#include <unordered_map>

struct Request {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map <std::string, std::string> headers;
    std::unordered_map<std::string, std::string> params;
    bool keep_alive = true;

    std::string param(const std::string& key) const;
};

Request parse_request(const std::string& raw_req);

#endif // REQUEST_HPP
