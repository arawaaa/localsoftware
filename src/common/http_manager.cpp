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

HTTPManager::HTTPManager(const string& base_dir, string name) {
    base_path_ = fs::canonical(base_dir);
    service_ = base_path_.string() + ":" + name;
    VisitStats::getInstance().register_service(service_);
}

void HTTPManager::add_endpoint(string endpoint, Handler handler, http::verb verb) {
    handlers_[std::move(endpoint)][verb] = std::move(handler);
}

bool HTTPManager::remove_endpoint(string endpoint) {
    return handlers_.erase(endpoint);
}

bool HTTPManager::endpoint_serviceable(string endpoint) const {
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

fs::path HTTPManager::assure_path(string& target) const {
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
