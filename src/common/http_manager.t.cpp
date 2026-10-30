#pragma once
#include "http_manager.h"

#include <string>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include <utility>
#include <iostream>

#include <boost/beast/http.hpp>
#include "visit_stats.h"

namespace http = boost::beast::http;
using namespace std;
namespace fs = std::filesystem;

template <typename Body, typename Fields>
optional<http::response<http::string_body>> HTTPManager::handle_request_with_pred(const http::request<Body, Fields>& req, PreEval pred) const {
    bool skip = true;
    auto resp = pred(req, skip);

    // TODO pass the response to handle_request for filling in if skip = false and there is non-nullopt response
    if (resp || (!resp && !skip)) {
        return resp;
    }

    return handle_request(req);
}

template <typename Body, typename Fields>
http::response<http::string_body> HTTPManager::handle_request(const http::request<Body, Fields>& req) const {
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

    try {
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
        }
    } catch (const exception& e) {
        // File not found or other filesystem error
        return error_response(req, http::status::not_found, "Not Found");
    }

    return error_response(req, http::status::not_found, "Not Found");
}

template <typename Body, typename Fields>
vector<pair<http::field, string>> HTTPManager::get_response_headers(const http::request<Body, Fields>& req) const {
    vector<pair<http::field, string>> headers;
    headers.push_back({http::field::server, "AIO-Landing"});
    if (auto it = req.find(http::field::connection); it != req.end()) {
        headers.push_back({http::field::connection, it->value()});
    }
    return headers;
}

// Use for logging
template <typename Body, typename Fields>
http::response<http::string_body> HTTPManager::error_response(
    const http::request<Body, Fields>& req,
    http::status status,
    const string& message) const {
    auto headers = get_response_headers(req);
    headers.emplace_back(http::field::content_type, "text/plain");
    return prepare_response(headers, status, message);
    // TODO Logging errors
}
