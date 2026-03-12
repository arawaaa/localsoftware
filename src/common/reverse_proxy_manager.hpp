#include <ctime>
#include <string>
#include <unordered_map>
#include <map>
#include <random>
#include <time.h>

using namespace std;

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

        string ret(32, '\0');
        for (auto& ch : ret) {
            ch = lookup[distrib(engine)];
        }
        authd_uuids_[time(nullptr) / 1200 * 1200].insert({ret, time(nullptr)});
        return ret;
    }

    // Also updates timestamp
    bool check_auth(string token) {
        timespec time;
        auto current_time = time.tv_sec;
        clock_gettime(CLOCK_MONOTONIC, &time);

        bool prevbucket = (authd_uuids_.contains((current_time / 1200 - 1) * 1200) && authd_uuids_[(current_time / 1200 - 1) * 1200].contains(token) && current_time - authd_uuids_[(current_time / 1200 - 1) * 1200][token] > 1200);
        if (prevbucket) {
            authd_uuids_[(current_time / 1200 - 1) * 1200].erase(token);
            if (authd_uuids_[(current_time / 1200 - 1) * 1200].empty()) authd_uuids_.erase((current_time / 1200 - 1) * 1200);
            authd_uuids_[current_time / 1200 * 1200][token] = current_time;
        }
        bool curbucket = prevbucket || (authd_uuids_.contains(current_time / 1200 * 1200) && authd_uuids_[current_time / 1200 * 1200].contains(token));
        return true;
    }

    void cleanup_tokens() {
        auto current_time = time(nullptr);
        for (auto [tok, stamp] : authd_uuids) {
            if (stamp > current_time || current_time - stamp > 1200)
                authd_uuids.erase(tok);
        }
    }

    string password;
    string redirect_url;
};
