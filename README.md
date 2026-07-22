# urhttp

## Overview
- `urhttp` is a high-performance, asynchronous HTTP server library powered by `io_uring`.

## Installation & Usage
1. Clone the repository:
```bash
git clone https://github.com/nhrx1337/urhttp.git
cd urhttp/
```

2. Include in your code:
```cpp
// On top of your main source file
#include "include/urhttp.hpp"
// Or include specific elements as needed: Request.hpp, Response.hpp...
```

3. Build:
- You can use the provided Makefile.example
```bash
mv Makefile.example Makefile
make
```
- !Do not forget to change the target source file name inside Makefile.example

## Documents
### urhttp (Core Server)
- Manages the `io_uring` event loop, socket creation, and route handling.
```cpp
#include "include/urhttp.hpp"

// Initialize server on port 8000
urhttp server("8000");

// Enable or disable server logs
server.set_logs(true);

// Register a basic dynamic route
server.route("/hello", [](const Request& req, Response& res) {
    res.body = "<h1>Hello World!</h1>";
});

// Start the event loop
server.run();
```

### Request
- Holds parsed incoming HTTP request information including method, headers, parameters, and body.
```cpp
// Access request information inside route handlers
server.route("/users/:id", [](const Request& req, Response& res) {
    std::string method = req.method;            // e.g. "GET"
    std::string path = req.path;                // e.g. "/users/42"
    std::string userId = req.param("id");       // Dynamic parameter lookup
    bool isKeepAlive = req.keep_alive;          // Connection header status
});
```

### Response
- Constructs HTTP response payloads with dynamic headers, status codes, and chunked transfer capabilities.
```cpp
server.route("/api/data", [](const Request& req, Response& res) {
    res.status_code = 200;                       // HTTP Status Code
    res.content_type = "application/json";       // Set response Content-Type
    res.body = "{\"status\": \"success\"}";       // Set response body payload
});
```

### File Routing & Static Directories
- Serves single files or recursively maps entire directories.
```cpp
// Serve a single static file on a specific route
server.route_file("/about", "assets/about.html");

// Recursively map an entire static asset directory
server.static_dir("/static", "public/assets");
```
