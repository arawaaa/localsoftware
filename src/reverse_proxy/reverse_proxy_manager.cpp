#pragma once

#include <ctime>
#include <string>
#include <unordered_map>
#include <map>
#include <random>
#include <optional>
#include <time.h>

#include <boost/beast/http.hpp>

using namespace std;
namespace http = boost::beast::http;

class ReverseProxyManager {
    unordered_map<string, time_t> authd_uuids;
    // Binned by 20 minute segments. For valid items, must check both current and previous bin
    map<time_t, unordered_map<string, time_t>> authd_uuids_;

public:
    string create_new_token() {
        char lookup[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
        random_device device;
        mt19937 engine(device());
        uniform_int_distribution distrib(0, 15);
        timespec time;
        clock_gettime(CLOCK_MONOTONIC, &time);

        string ret(32, '\0');
        for (auto& ch : ret) {
            ch = lookup[distrib(engine)];
        }
        authd_uuids_[time.tv_sec / 1200 * 1200].insert({ret, time.tv_sec});
        return ret;
    }

    // Also updates timestamp
    bool check_auth(string token) {
        timespec time;
        clock_gettime(CLOCK_MONOTONIC, &time);
        auto current_time = time.tv_sec;

        bool prevbucket = (authd_uuids_.contains((current_time / 1200 - 1) * 1200) && authd_uuids_[(current_time / 1200 - 1) * 1200].contains(token) && current_time - authd_uuids_[(current_time / 1200 - 1) * 1200][token] > 1200);
        if (prevbucket) {
            authd_uuids_[(current_time / 1200 - 1) * 1200].erase(token);
            if (authd_uuids_[(current_time / 1200 - 1) * 1200].empty()) authd_uuids_.erase((current_time / 1200 - 1) * 1200);
            authd_uuids_[current_time / 1200 * 1200][token] = current_time;
        }
        bool curbucket = prevbucket || (authd_uuids_.contains(current_time / 1200 * 1200) && authd_uuids_[current_time / 1200 * 1200].contains(token));
        return curbucket;
    }

    void cleanup_tokens() {
        auto current_time = time(nullptr);
        for (auto [tok, stamp] : authd_uuids) {
            if (stamp > current_time || current_time - stamp > 1200)
                authd_uuids.erase(tok);
        }
    }

    static optional<string> get_token(const http::request<http::string_body>& req) {
        string cookie_str = req.at(http::field::cookie);
        size_t pos = cookie_str.find("token=");
        if (pos == string::npos)
            return nullopt;

        pos += 6;
        size_t epos = cookie_str.find_first_of("; ", pos);

        if (epos == string::npos) epos = cookie_str.size();

        string token = cookie_str.substr(pos, epos - pos);
        return token;
    }

    static optional<string> get_token(const string& req) {
        size_t pos = req.find("token=");
        if (pos == string::npos)
            return nullopt;

        pos += 6;
        size_t epos = req.find_first_of("; ", pos);

        if (epos == string::npos) epos = req.size();

        string token = req.substr(pos, epos - pos);
        return token;
    }

    string peer_addr;
    int port;
    string password;
    string redirect_url;
};
