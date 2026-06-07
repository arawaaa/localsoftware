#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <cstring>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <liburing.h>

#include <openssl/sha.h>

#include "common/io_uring_manager.cpp"
#include "test/test.cpp"
// #include "bandwidth_monitor/accept_event.cpp"
#include "aio_landing/aio_landing_accepter.cpp"
// #include "reverse_proxy/reverse_proxy_accept.cpp"
#include "logging/visit_stats.cpp"

using namespace std;

// Constants
const string INTERFACE = "wlan0";
const string RX_FILE = "/sys/class/net/" + INTERFACE + "/statistics/rx_bytes";
const string TX_FILE = "/sys/class/net/" + INTERFACE + "/statistics/tx_bytes";
const string LOG_FILE = "/var/log/wlan_monitor/wlan_usage.log";
constexpr int SERVER_PORT = 8443;
constexpr int RP_PORT = 18789;
constexpr int HTTP_PORT = 80;
constexpr int HTTPS_PORT = 443;

// Global speed state
double global_rx_speed = 0.0;
double global_tx_speed = 0.0;
mutex speed_mutex;

// Base64 Implementation
const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const int mod_table[] = {0, 2, 1};

string base64_encode(const unsigned char *data, size_t input_length) {
    size_t output_length = 4 * ((input_length + 2) / 3);
    string encoded_data(output_length, '\0');

    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[output_length - 1 - i] = '=';

    return encoded_data;
}

unsigned long long read_bytes(const string& filename) {
    ifstream f(filename);
    unsigned long long bytes = 0;
    if (f >> bytes) {
        return bytes;
    }
    return 0;
}

void log_entry(time_t ts, int d, int m, int y, int h, unsigned long long tx, unsigned long long rx) {
    ofstream f(LOG_FILE, ios::app);
    if (f) {
        f << "[" << ts << "] " 
          << setw(2) << setfill('0') << d << "/"
          << setw(2) << setfill('0') << m << "/"
          << y << " " << h << " " << tx << " " << rx << "\n";
    }

    ofstream f2("/var/log/wlan_monitor/access.log", ios::out);
    VisitStats::getInstance().get_pretty_stats(f2);
}

int setup_server_socket6(int port) {
    int server_fd;
    struct sockaddr_in6 address{
        .sin6_family = AF_INET6,
        .sin6_port = htons(port),
        .sin6_flowinfo = 0,
        .sin6_addr = IN6ADDR_ANY_INIT,
        .sin6_scope_id = 0
    };
    int opt = 1;

    if ((server_fd = socket(AF_INET6, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }
    return server_fd;
}

int setup_server_socket(int port) {
    int server_fd;
    struct sockaddr_in address{};
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }
    return server_fd;
}

void server_func() {
    // Single stack IPv6 for all surfaces outside of homepage, which is dual-stack
    // shared_ptr<File> ws_fd = make_shared<File>(setup_server_socket(SERVER_PORT));
    // if (ws_fd->get() < 0) return;
    //
    // shared_ptr<File> ws6_fd = make_shared<File>(setup_server_socket6(SERVER_PORT));
    // if (ws6_fd->get() < 0) return;
    //
    shared_ptr<File> http6_fd = make_shared<File>(setup_server_socket6(HTTP_PORT));
    if (http6_fd->get() < 0) return;

    shared_ptr<File> http_fd = make_shared<File>(setup_server_socket(HTTP_PORT));
    if (http_fd->get() < 0) return;
    //
    // shared_ptr<File> https_fd = make_shared<File>(setup_server_socket(HTTPS_PORT));
    // if (https_fd->get() < 0) return;
    //
    // shared_ptr<File> https6_fd = make_shared<File>(setup_server_socket6(HTTPS_PORT));
    // if (https6_fd->get() < 0) return;
    //
    // shared_ptr<File> rphttps_fd = make_shared<File>(setup_server_socket(RP_PORT));
    // if (rphttps_fd->get() < 0) return;

    // Initialize io_uring
    struct io_uring ring;
    if (io_uring_queue_init(512, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return;
    }

    auto& instance = AsyncHandler::self(1);

    // Setup WebSocket Accept Event
    // auto ws_files = {ws_fd, ws6_fd};
    // instance.initialize_root_event<BandwidthMonitorAcceptEvent>(ws_files, true, "/srv/bwith");
    //
    // // Setup HTTP Landing Accept Event
    vector<shared_ptr<File>> http_files = {http_fd, http6_fd};
    // instance.initialize_root_event<LandingAccept>(http_files, false, "/srv/landing");
    instance.initialize_root_event<TestClass>(http_files);
    //
    // // Setup HTTPS Landing Accept Event
    // vector<shared_ptr<File>> https_files = {https_fd, https6_fd};
    // instance.initialize_root_event<LandingAccept>(https_files, true, "/srv/landing");
    //
    // vector<shared_ptr<File>> rp_files = {rphttps_fd};
    // instance.initialize_root_event<ReverseProxyAccept>(rp_files, "/srv/rp");
    // instance.call_root_function<ReverseProxyAccept>(res, &ReverseProxyAccept::prepare_accept);

    instance.start();

    io_uring_queue_exit(&ring);
}

int main() {
    cout << "       /      //------   localsoftware: an io-uring web-server, proxy, and more\n" <<
            "      /     ///          © Arnav Rawat, GPLv3\n" <<
            "     /      //           github.com/arawaaa/localsoftware\n" <<
            "    /       /------/     handmade\n" <<
            "   /              //     \n" <<
            "  /              ///     \n" <<
            " /-----   ------//       \n";

    cout << "Bandwidth Monitor (HTTPS): " << SERVER_PORT << " | ";
    cout << "Landing Server (HTTP, HTTPS): " << HTTP_PORT << ", " << HTTPS_PORT << " | ";
    cout << "Reverse Proxy (HTTPS): " << RP_PORT << endl;
    
    // Start unified io_uring server thread
    thread server_t(server_func);
    server_t.detach();

    unsigned long long rx_prev = read_bytes(RX_FILE);

    unsigned long long tx_prev = read_bytes(TX_FILE);
    
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    int cur_hour = t->tm_hour;
    int cur_mday = t->tm_mday;
    int cur_mon = t->tm_mon + 1;
    int cur_year = t->tm_year + 1900;

    unsigned long long hourly_rx = 0;
    unsigned long long hourly_tx = 0;
    unsigned long long daily_rx = 0;
    unsigned long long daily_tx = 0;

    while (true) {
        this_thread::sleep_for(chrono::seconds(1));

        unsigned long long rx_curr = read_bytes(RX_FILE);
        unsigned long long tx_curr = read_bytes(TX_FILE);

        unsigned long long rx_diff = (rx_curr >= rx_prev) ? (rx_curr - rx_prev) : 0;
        unsigned long long tx_diff = (tx_curr >= tx_prev) ? (tx_curr - tx_prev) : 0;

        // Update global speed guarded by mutex
        {
            lock_guard<mutex> lock(speed_mutex);
            global_rx_speed = static_cast<double>(rx_diff);
            global_tx_speed = static_cast<double>(tx_diff);
        }

        hourly_rx += rx_diff;
        hourly_tx += tx_diff;
        daily_rx += rx_diff;
        daily_tx += tx_diff;

        rx_prev = rx_curr;
        tx_prev = tx_curr;

        now = time(nullptr);
        t = localtime(&now);

        if (t->tm_hour != cur_hour) {
            log_entry(now, cur_mday, cur_mon, cur_year, cur_hour, hourly_tx, hourly_rx);
            hourly_rx = 0;
            hourly_tx = 0;

            if (t->tm_mday != cur_mday) {
                log_entry(now, cur_mday, cur_mon, cur_year, 24, daily_tx, daily_rx);
                daily_rx = 0;
                daily_tx = 0;

                cur_mday = t->tm_mday;
                cur_mon = t->tm_mon + 1;
                cur_year = t->tm_year + 1900;
            }
            cur_hour = t->tm_hour;
        }
    }

    return 0;
}
