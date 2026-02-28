#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <vector>
#include <utility>

#include <boost/beast/http.hpp>

namespace http = boost::beast::http;
namespace fs = std::filesystem;

class HTTPManager {
public:
    using Handler = std::function<http::response<http::string_body>(const http::request<http::string_body>&)>;

    explicit HTTPManager(const std::string& base_dir) {
        try {
            base_path_ = fs::canonical(base_dir);
        } catch (const std::exception& e) {
            // If the base directory doesn't exist or is invalid, 
            // we'll store the absolute path and hope for the best, 
            // or let future operations fail.
            base_path_ = fs::absolute(base_dir);
        }
    }

    void add_endpoint(std::string endpoint, Handler handler) {
        handlers_[std::move(endpoint)] = std::move(handler);
    }

    static http::response<http::string_body> prepare_response(
        const std::vector<std::pair<http::field, std::string>>& headers,
        http::status status,
        const std::string& body,
        unsigned version = 11) {
        http::response<http::string_body> res{status, version};
        for (const auto& [field, value] : headers) {
            res.set(field, value);
        }
        res.body() = body;
        res.prepare_payload();
        return res;
    }

    template <typename Body, typename Fields>
    http::response<http::string_body> handle_request(const http::request<Body, Fields>& req) const {
        std::string target = std::string(req.target());
        
        // Remove query parameters if present
        auto query_pos = target.find('?');
        if (query_pos != std::string::npos) {
            target = target.substr(0, query_pos);
        }

        // Check for custom handlers
        if (auto it = handlers_.find(target); it != handlers_.end()) {
            if constexpr (std::is_same_v<Body, http::string_body>) {
                return it->second(req);
            } else {
                // If it's not string_body, we might need to create a temporary string_body request 
                // but for now we just fall through or handle only string_body as that's what's used.
            }
        }

        // Prevent directory traversal via target string directly if it contains ".."
        // although canonical() should handle it, it's good to be cautious.
        
        fs::path request_path = base_path_;
        // Append the target, removing the leading slash to make it relative to base_path_
        if (!target.empty() && target[0] == '/') {
            request_path /= target.substr(1);
        } else {
            request_path /= target;
        }

        // Default to index.html if a directory is requested
        if (fs::is_directory(request_path)) {
            request_path /= "index.html";
        }

        try {
            // Resolve symlinks and normalize
            fs::path canonical_path = fs::canonical(request_path);

            // Ensure the canonical path starts with the base path
            auto base_str = base_path_.string();
            auto canonical_str = canonical_path.string();

            if (canonical_str.size() >= base_str.size() &&
                canonical_str.compare(0, base_str.size(), base_str) == 0) {
                
                std::ifstream ifs(canonical_path, std::ios::binary);
                if (ifs) {
                    std::string content;
                    content.resize(fs::file_size(canonical_path));
                    ifs.read(&content[0], content.size());
                    
                    http::response<http::string_body> res{http::status::ok, req.version()};
                    res.set(http::field::server, "AIO-Landing");
                    res.set(http::field::content_type, get_mime_type(canonical_path));
                    res.set(http::field::connection, "keep-alive");
                    res.body() = std::move(content);
                    res.prepare_payload();
                    return res;
                }
            } else {
                // Path escaped base directory
                return error_response(req, http::status::forbidden, "Forbidden");
            }
        } catch (const fs::filesystem_error& e) {
            // File not found or other filesystem error
            return error_response(req, http::status::not_found, "Not Found");
        }

        return error_response(req, http::status::not_found, "Not Found");
    }

private:
    fs::path base_path_;
    std::map<std::string, Handler> handlers_;

    static std::string get_mime_type(const fs::path& path) {
        auto ext = path.extension().string();
        if (ext == ".html" || ext == ".htm") return "text/html";
        if (ext == ".css") return "text/css";
        if (ext == ".js") return "application/javascript";
        if (ext == ".json") return "application/json";
        if (ext == ".png") return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".gif") return "image/gif";
        if (ext == ".svg") return "image/svg+xml";
        if (ext == ".txt") return "text/plain";
        if (ext == ".pdf") return "application/pdf";
        return "application/octet-stream";
    }

    template <typename Body, typename Fields>
    static http::response<http::string_body> error_response(
        const http::request<Body, Fields>& req, 
        http::status status, 
        const std::string& message) {
        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::connection, "close");
        res.body() = message;
        res.prepare_payload();
        return res;
    }
};
