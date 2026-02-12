#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <cstring>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <liburing.h>

#include "common/io_event.hpp"
#include "common/io_uring_manager.hpp"
#include "bandwidth_monitor/accept_event.hpp"
#include "bandwidth_monitor/bandwidth_data_read_event.hpp"
#include "bandwidth_monitor/bandwidth_data_write_event.hpp"
#include "lennox_server/simple_webserver.hpp"

// Constants
const std::string INTERFACE = "wlan0";
const std::string RX_FILE = "/sys/class/net/" + INTERFACE + "/statistics/rx_bytes";
const std::string TX_FILE = "/sys/class/net/" + INTERFACE + "/statistics/tx_bytes";
const std::string LOG_FILE = "/var/log/wlan_monitor/wlan_usage.log";
constexpr int SERVER_PORT = 8888;
constexpr int HTTP_PORT = 80;

// Global speed state
double global_rx_speed = 0.0;
double global_tx_speed = 0.0;
std::mutex speed_mutex;

// Base64 Implementation
const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const int mod_table[] = {0, 2, 1};

std::string base64_encode(const unsigned char *data, size_t input_length) {
    size_t output_length = 4 * ((input_length + 2) / 3);
    std::string encoded_data(output_length, '\0');

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

unsigned long long read_bytes(const std::string& filename) {
    std::ifstream f(filename);
    unsigned long long bytes = 0;
    if (f >> bytes) {
        return bytes;
    }
    return 0;
}

void log_entry(time_t ts, int d, int m, int y, int h, unsigned long long tx, unsigned long long rx) {
    std::ofstream f(LOG_FILE, std::ios::app);
    if (f) {
        f << "[" << ts << "] " 
          << std::setw(2) << std::setfill('0') << d << "/" 
          << std::setw(2) << std::setfill('0') << m << "/" 
          << y << " " << h << " " << tx << " " << rx << "\n";
    }
}

void server_func() {
    int server_fd;
    struct sockaddr_in address{};
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return;
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        return;
    }

    std::cout << "io_uring WebSocket Server listening on port " << SERVER_PORT << std::endl;

    // Initialize io_uring
    struct io_uring ring;
    if (io_uring_queue_init(256, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return;
    }

    // Wrap the server socket in a File object and transfer ownership to the AcceptEvent
    auto server_file = std::make_unique<File>(server_fd);
    auto* accept_ev = new BandwidthMonitorAcceptEvent(std::move(server_file), &ring);
    accept_ev->prepare_accept();
    IoUringManager::getInstance().submit_events(&ring);
    io_uring_submit(&ring);

    while (true) {
        struct io_uring_cqe* cqe;
        // Wait for completions
        if (io_uring_wait_cqe(&ring, &cqe) < 0) {
            perror("io_uring_wait_cqe");
            continue;
        }

        IoEvent* ev = reinterpret_cast<IoEvent*>(io_uring_cqe_get_data(cqe));
        if (ev) {
            auto [success, result_code] = ev->abstract_event_success(cqe->res);
            if (success) {
                ev->post(result_code);
            }
        }

        io_uring_cqe_seen(&ring, cqe);
        
        // Ensure pending submissions (like from post()) are sent
        // Note: In high load, you might want to batch this or use SQPOLL
        IoUringManager::getInstance().submit_events(&ring);
        io_uring_submit(&ring); 
    }

    io_uring_queue_exit(&ring);
}

void http_server_func() {
    SimpleWebserver server;
    server.run(HTTP_PORT);
}

int main() {
    std::cout << "Starting bandwidth monitor for " << INTERFACE << "..." << std::endl;
    std::cout << "Logging hourly and daily totals to " << LOG_FILE << std::endl;
    
    // Start io_uring websocket server thread
    std::thread server_t(server_func);
    server_t.detach();

    // Start serial HTTP server thread (Lennox integration)
    std::thread http_t(http_server_func);
    http_t.detach();

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
        std::this_thread::sleep_for(std::chrono::seconds(1));

        unsigned long long rx_curr = read_bytes(RX_FILE);
        unsigned long long tx_curr = read_bytes(TX_FILE);

        unsigned long long rx_diff = (rx_curr >= rx_prev) ? (rx_curr - rx_prev) : 0;
        unsigned long long tx_diff = (tx_curr >= tx_prev) ? (tx_curr - tx_prev) : 0;

        // Update global speed guarded by mutex
        {
            std::lock_guard<std::mutex> lock(speed_mutex);
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
