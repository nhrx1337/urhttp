#include "../include/Response.hpp"

std::string Response::get_status_text(HttpStatus status) {
    switch (status) {
        case HttpStatus::OK:                return "OK";
        case HttpStatus::NotFound:          return "Not Found";
        case HttpStatus::MethodNotAllowed:  return "Method Not Allowed";
        default:                            return "Unknown Status";
    }
}

std::string Response::build(bool keep_alive) {
    std::string status_text = get_status_text((HttpStatus)status_code);
    std::string conn_header = keep_alive ? "keep-alive" : "close";

    std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n"
                           "Content-Type: " + content_type + "\r\n";

    if (is_chunked) {
        response += "Transfer-Encoding: chunked\r\n";
    } else {
        response += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    }

    response += "Connection: " + conn_header + "\r\n\r\n" + body;

    return response;
}
