#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <vector>
#include <utility>
#include <iostream>

#include <boost/beast/http.hpp>
#include <logging/visit_stats.cpp>

namespace http = boost::beast::http;
using namespace std;
namespace fs = std::filesystem;

class HTTPManager {
public:
    using Handler = function<http::response<http::string_body>(const http::request<http::string_body>&)>;

    explicit HTTPManager(const string& base_dir, string name = {}) {
        base_path_ = fs::canonical(base_dir);
        service_ = base_path_.string() + ":" + name;
        VisitStats::getInstance().register_service(service_);
    }

    void add_endpoint(string endpoint, Handler handler, http::verb verb = http::verb::get) {
        handlers_[std::move(endpoint)][verb] = std::move(handler);
    }

    bool remove_endpoint(string endpoint) {
        return handlers_.erase(endpoint);
    }

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
    optional<http::response<http::string_body>> handle_request_with_pred(const http::request<Body, Fields>& req, PreEval pred) const {
        bool skip = true;
        auto resp = pred(req, skip);

        // TODO pass the response to handle_request for filling in if skip = false and there is non-nullopt response
        if (resp || (!resp && !skip)) {
            return resp;
        }

        return handle_request(req);
    }

    bool endpoint_serviceable(string endpoint) const {
        auto query_pos = endpoint.find('?');
        if (query_pos != string::npos) {
            endpoint = endpoint.substr(0, query_pos);
        }

        if (handlers_.contains(endpoint))
            return true;

        try {
            auto path = assure_path(endpoint);
            return fs::exists(path);
        } catch (exception& e) {
            return false;
        }
    }

    fs::path assure_path(string& target) const {
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

        fs::path canonical_path = fs::canonical(request_path);

        // Ensure the canonical path starts with the base path
        auto base_str = base_path_.string();
        auto canonical_str = canonical_path.string();

        if (canonical_str.size() < base_str.size() || canonical_str.compare(0, base_str.size(), base_str) != 0)
            throw runtime_error{"Invalid path accessed"};

        return canonical_path;
    }

    template <typename Body, typename Fields>
    http::response<http::string_body> handle_request(const http::request<Body, Fields>& req) const {
        string target = string(req.target());

        // Remove query parameters if present
        auto query_pos = target.find('?');
        if (query_pos != string::npos) {
            target = target.substr(0, query_pos);
        }

        // Check for custom handlers
        if (auto it = handlers_.find(target); it != handlers_.end()) {
            if constexpr (is_same_v<Body, http::string_body>) {
                VisitStats::getInstance().add_access(service_, target);
                if (it->second.contains(req.method())) {
                    return it->second.at(req.method())(req);
                } else {
                    auto headers = get_response_headers(req);
                    return prepare_response(headers, http::status::method_not_allowed);
                }
            } else {
                throw runtime_error{"Unsupported body type"};
            }
        }

        // try {
            // Resolve symlinks and normalize
            fs::path canonical_path = assure_path(target);
                
            ifstream ifs(canonical_path, ios::binary);
            if (ifs) {
                string content;
                content.resize(fs::file_size(canonical_path));
                ifs.read(&content[0], content.size());

                VisitStats::getInstance().add_access(service_, canonical_path);

                auto headers = get_response_headers(req);
                headers.emplace_back(http::field::content_type, get_mime_type(canonical_path));
                return prepare_response(headers, http::status::ok, std::move(content));;
            }/*
        } catch (const exception& e) {
            // File not found or other filesystem error
            return error_response(req, http::status::not_found, "Not Found");
        }*/

        return error_response(req, http::status::not_found, "Not Found");
    }

    template <typename Body, typename Fields>
    vector<pair<http::field, string>> get_response_headers(const http::request<Body, Fields>& req) const {
        vector<pair<http::field, string>> headers;
        headers.push_back({http::field::server, "AIO-Landing"});
        if (auto it = req.find(http::field::connection); it != req.end()) {
            headers.push_back({http::field::connection, it->value()});
        }
        return headers;
    }

    // Use for logging
    template <typename Body, typename Fields>
    http::response<http::string_body> error_response(
        const http::request<Body, Fields>& req,
        http::status status,
        const string& message) const {
        auto headers = get_response_headers(req);
        headers.emplace_back(http::field::content_type, "text/plain");
        return prepare_response(headers, status, message);
        // TODO Logging errors
    }

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
