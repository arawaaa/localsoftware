#pragma once

#include <string>
#include <filesystem>
#include <functional>
#include <map>
#include <vector>
#include <utility>

#include <boost/beast/http.hpp>

namespace http = boost::beast::http;
using namespace std;
namespace fs = std::filesystem;

class HTTPManager {
public:
    using Handler = function<http::response<http::string_body>(const http::request<http::string_body>&)>;

    explicit HTTPManager(const string& base_dir, string name = {});

    void add_endpoint(string endpoint, Handler handler, http::verb verb = http::verb::get);

    bool remove_endpoint(string endpoint);

    static http::response<http::string_body> prepare_response(
        const vector<pair<http::field, string>>& headers,
        http::status status,
        const string& body = {},
        unsigned version = 11) {
        http::response<http::string_body> res{status, version};
        for (const auto& [field, value] : headers) {
            res.set(field, value);
        }
        res.body() = body;
        res.prepare_payload();
        return res;
    }

    using PreEval = function<optional<http::response<http::string_body>>(const http::request<http::string_body>&, bool&)>;

    // Useful for pre-empting responses if the requests satisfy some condition
    template <typename Body, typename Fields>
    optional<http::response<http::string_body>> handle_request_with_pred(const http::request<Body, Fields>& req, PreEval pred) const;

    bool endpoint_serviceable(string endpoint) const;

    fs::path assure_path(string& target) const;

    template <typename Body, typename Fields>
    http::response<http::string_body> handle_request(const http::request<Body, Fields>& req) const;

    template <typename Body, typename Fields>
    vector<pair<http::field, string>> get_response_headers(const http::request<Body, Fields>& req) const;

    // Use for logging
    template <typename Body, typename Fields>
    http::response<http::string_body> error_response(
        const http::request<Body, Fields>& req,
        http::status status,
        const string& message) const;

private:
    string service_;
    fs::path base_path_;
    map<string, map<http::verb, Handler>> handlers_;

    static string get_mime_type(const fs::path& path) {
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
};

#include "http_manager.t.cpp"
