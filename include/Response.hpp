#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <string>

enum class HttpStatus {
    OK = 200,
    NotFound = 404,
    MethodNotAllowed = 405
};

class Response {
private:
    std::string get_status_text(HttpStatus status);
public:
    std::string body;
    int status_code = 200;
    std::string content_type = "text/html";
    bool is_chunked = false;

    std::string build(bool keep_alive = false);
};

#endif // RESPONSE_HPP
