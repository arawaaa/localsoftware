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

// Constants
const std::string INTERFACE = "wlan0";
const std::string RX_FILE = "/sys/class/net/" + INTERFACE + "/statistics/rx_bytes";
const std::string TX_FILE = "/sys/class/net/" + INTERFACE + "/statistics/tx_bytes";
const std::string LOG_FILE = "wlan_usage.log";
constexpr int SERVER_PORT = 8888;
const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Global speed state
double global_rx_speed = 0.0;
double global_tx_speed = 0.0;
std::mutex speed_mutex;

// Binary Log Record Format
struct __attribute__((packed)) LogRecord {
    uint64_t timestamp;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t hour; // 0-23 for hourly, 24 for daily
};

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
          << y << " " << h << " " << tx << " " << rx << "
";
    }
}

int send_websocket_frame_checked(int client_fd, const void *data, size_t len) {
    std::vector<unsigned char> frame;
    frame.reserve(14 + len); // Max header + data

    frame.push_back(0x82); // FIN + Binary Frame

    if (len <= 125) {
        frame.push_back(static_cast<unsigned char>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 0; i < 8; i++) {
            frame.push_back((len >> ((7 - i) * 8)) & 0xFF);
        }
    }

    // Send header
    if (send(client_fd, frame.data(), frame.size(), 0) < 0) return -1;
    // Send payload
    return send(client_fd, data, len, 0);
}

void server_func() {
    int server_fd, client_fd;
    struct sockaddr_in address{};
    int opt = 1;
    socklen_t addrlen = sizeof(address);

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

    std::cout << "WebSocket Server listening on port " << SERVER_PORT << std::endl;

    while (true) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept");
            continue;
        }
        std::cout << "New connection accepted from client" << std::endl;

        char buffer[2048];
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            std::cout << "Failed to receive handshake or client closed" << std::endl;
            close(client_fd);
            continue;
        }
        buffer[n] = 0;
        std::cout << "Received request:
" << buffer << std::endl;

        std::string req(buffer);
        
        // Simple check for upgrade header
        if (req.find("Upgrade: websocket") != std::string::npos) {
            size_t key_pos = req.find("Sec-WebSocket-Key:");
            if (key_pos != std::string::npos) {
                key_pos += 18;
                // Skip whitespace
                while (key_pos < req.length() && (req[key_pos] == ' ' || req[key_pos] == '	')) key_pos++;
                
                size_t eol = req.find("
", key_pos);
                if (eol != std::string::npos) {
                    std::string key = req.substr(key_pos, eol - key_pos);
                    // Trim trailing spaces
                    size_t last = key.find_last_not_of(" 	");
                    if (last != std::string::npos) key = key.substr(0, last + 1);

                    std::string combined = key + GUID;
                    unsigned char hash[SHA_DIGEST_LENGTH];
                    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);

                    std::string output = base64_encode(hash, SHA_DIGEST_LENGTH);
                    std::cout << "Handshake successful. Accepting with key: " << output << std::endl;

                    std::ostringstream response;
                    response << "HTTP/1.1 101 Switching Protocols
"
                             << "Upgrade: websocket
"
                             << "Connection: Upgrade
"
                             << "Sec-WebSocket-Accept: " << output << "
"
                             << "
";
                    std::string resp_str = response.str();
                    send(client_fd, resp_str.c_str(), resp_str.length(), 0);
                    std::cout << "Handshake response sent" << std::endl;

                    // 1. Send Log Data (History)
                    std::ifstream log_f(LOG_FILE);
                    if (log_f) {
                        std::string line;
                        while (std::getline(log_f, line)) {
                            long ts;
                            int d, m, y, h;
                            unsigned long long tx, rx;
                            // Parsing logic
                            if (sscanf(line.c_str(), "[%ld] %d/%d/%d %d %llu %llu", &ts, &d, &m, &y, &h, &tx, &rx) == 7) {
                                LogRecord record{};
                                record.timestamp = static_cast<uint64_t>(ts);
                                record.tx_bytes = static_cast<uint64_t>(tx);
                                record.rx_bytes = static_cast<uint64_t>(rx);
                                record.hour = static_cast<uint32_t>(h);
                                send_websocket_frame_checked(client_fd, &record, sizeof(record));
                            }
                        }
                    }

                    // 2. Send Marker
                    uint64_t marker[3] = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};
                    send_websocket_frame_checked(client_fd, marker, sizeof(marker));

                    // 3. Live Streaming Loop
                    while (true) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        double speeds[2]; // RX, TX
                        
                        {
                            std::lock_guard<std::mutex> lock(speed_mutex);
                            speeds[0] = global_rx_speed;
                            speeds[1] = global_tx_speed;
                        }
                        
                        if (send_websocket_frame_checked(client_fd, speeds, sizeof(speeds)) < 0) {
                            break; // Connection closed
                        }
                    }
                }
            }
        } else if (req.find("GET / ") != std::string::npos) {
            std::cout << "Serving index.html" << std::endl;
            std::ifstream f("index.html", std::ios::binary | std::ios::ate);
            if (f) {
                std::streamsize fsize = f.tellg();
                f.seekg(0, std::ios::beg);

                std::vector<char> content(fsize);
                if (f.read(content.data(), fsize)) {
                    std::ostringstream header;
                    header << "HTTP/1.1 200 OK
"
                           << "Content-Type: text/html
"
                           << "Content-Length: " << fsize << "
"
                           << "Connection: close
"
                           << "
";
                    std::string h = header.str();
                    send(client_fd, h.c_str(), h.length(), 0);
                    send(client_fd, content.data(), fsize, 0);
                }
            } else {
                const char *not_found = "HTTP/1.1 404 Not Found
Content-Length: 0

";
                send(client_fd, not_found, strlen(not_found), 0);
            }
        }
        close(client_fd);
    }
}

int main() {
    std::cout << "Starting bandwidth monitor for " << INTERFACE << "..." << std::endl;
    std::cout << "Logging hourly and daily totals to " << LOG_FILE << std::endl;
    
    // Start server thread
    std::thread server_t(server_func);
    server_t.detach(); // Detach since we run infinite loops

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
