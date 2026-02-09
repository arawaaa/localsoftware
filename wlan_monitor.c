#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <ctype.h>
#include <openssl/sha.h>

#define INTERFACE "wlan0"
#define RX_FILE "/sys/class/net/" INTERFACE "/statistics/rx_bytes"
#define TX_FILE "/sys/class/net/" INTERFACE "/statistics/tx_bytes"
#define LOG_FILE "wlan_usage.log"
#define SERVER_PORT 8888
#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*
 * Global speed state
 */
double global_rx_speed = 0.0;
double global_tx_speed = 0.0;
pthread_mutex_t speed_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Binary Log Record Format
 */
struct __attribute__((packed)) LogRecord {
    uint64_t timestamp;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t hour; // 0-23 for hourly, 24 for daily
};

// --- Base64 Implementation ---
static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
static int mod_table[] = {0, 2, 1};

void base64_encode(const unsigned char *data,
                    size_t input_length,
                    char *encoded_data) {

    size_t output_length = 4 * ((input_length + 2) / 3);

    for (int i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[output_length - 1 - i] = '=';
    
    encoded_data[output_length] = '\0';
}

unsigned long long read_bytes(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;
    unsigned long long bytes;
    if (fscanf(f, "%llu", &bytes) != 1) bytes = 0;
    fclose(f);
    return bytes;
}

void log_entry(time_t ts, int d, int m, int y, int h, unsigned long long tx, unsigned long long rx) {
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "[%ld] %02d/%02d/%d %d %llu %llu\n", (long)ts, d, m, y, h, tx, rx);
        fclose(f);
    }
}

// Websocket helper: Send data frame (returns total bytes sent or -1 on error)
int send_websocket_frame_checked(int client_fd, const void *data, size_t len) {
    unsigned char frame[14];
    int header_size = 0;
    frame[0] = 0x82; // FIN + Binary Frame

    if (len <= 125) {
        frame[1] = (unsigned char)len;
        header_size = 2;
    } else if (len <= 65535) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        header_size = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (len >> ((7 - i) * 8)) & 0xFF;
        }
        header_size = 10;
    }

    if (send(client_fd, frame, header_size, 0) < 0) return -1;
    return send(client_fd, data, len, 0);
}

void send_websocket_frame(int client_fd, const void *data, size_t len) {
    send_websocket_frame_checked(client_fd, data, len);
}

void *server_thread(void *arg) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return NULL;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return NULL;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return NULL;
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        return NULL;
    }

    printf("WebSocket Server listening on port %d\n", SERVER_PORT);

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept");
            continue;
        }
        printf("New connection accepted from client\n");

        char buffer[2048];
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            printf("Failed to receive handshake or client closed\n");
            close(client_fd);
            continue;
        }
        buffer[n] = 0;
        printf("Received request:\n%s\n", buffer);

        if (strstr(buffer, "Upgrade: websocket")) {
            char *key_start = strcasestr(buffer, "Sec-WebSocket-Key:");
            if (key_start) {
                key_start += 18;
                while (*key_start == ' ' || *key_start == '\t') key_start++;
                
                char *key_end = strstr(key_start, "\r\n");
                if (key_end) {
                    *key_end = 0;
                    // Trim trailing space if any
                    char *tmp = key_end - 1;
                    while (tmp > key_start && (*tmp == ' ' || *tmp == '\t')) {
                        *tmp = 0;
                        tmp--;
                    }
                    
                    char key[128];
                    strncpy(key, key_start, sizeof(key) - 1);
                    key[sizeof(key)-1] = 0;
                    
                    char combined[256];
                    snprintf(combined, sizeof(combined), "%s%s", key, GUID);

                    unsigned char hash[SHA_DIGEST_LENGTH];
                    SHA1((unsigned char*)combined, (size_t)strlen(combined), hash);

                    char output[64];
                    base64_encode(hash, SHA_DIGEST_LENGTH, output);
                    printf("Handshake successful. Accepting with key: %s\n", output);

                    char response[512];
                    snprintf(response, sizeof(response),
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: %s\r\n"
                        "\r\n", output);
                    send(client_fd, response, strlen(response), 0);
                    printf("Handshake response sent\n");

                    // 1. Send Log Data (History) - Done AFTER handshake to avoid blocking the connection start
                    FILE *f = fopen(LOG_FILE, "r");
                    if (f) {
                        char line[256];
                        while (fgets(line, sizeof(line), f)) {
                            long ts;
                            int d, m, y, h;
                            unsigned long long tx, rx;
                            if (sscanf(line, "[%ld] %d/%d/%d %d %llu %llu", &ts, &d, &m, &y, &h, &tx, &rx) == 7) {
                                struct LogRecord record;
                                record.timestamp = (uint64_t)ts;
                                record.tx_bytes = (uint64_t)tx;
                                record.rx_bytes = (uint64_t)rx;
                                record.hour = (uint32_t)h;
                                send_websocket_frame(client_fd, &record, sizeof(record));
                            }
                        }
                        fclose(f);
                    }

                    // 2. Send Marker (0xFFFFFF x 3, interpreted as 3x 64-bit max)
                    uint64_t marker[3] = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};
                    send_websocket_frame(client_fd, marker, sizeof(marker));

                    // 3. Live Streaming Loop
                    while (1) {
                        sleep(1);
                        double speeds[2]; // RX, TX
                        
                        pthread_mutex_lock(&speed_mutex);
                        speeds[0] = global_rx_speed;
                        speeds[1] = global_tx_speed;
                        pthread_mutex_unlock(&speed_mutex);
                        
                        if (send_websocket_frame_checked(client_fd, speeds, sizeof(speeds)) < 0) {
                            break; // Connection closed
                        }
                    }
                }
            }
        } else if (strstr(buffer, "GET / ")) {
            printf("Serving index.html\n");
            FILE *f = fopen("index.html", "r");
            if (f) {
                fseek(f, 0, SEEK_END);
                long fsize = ftell(f);
                fseek(f, 0, SEEK_SET);

                char *content = malloc(fsize + 1);
                fread(content, 1, fsize, f);
                fclose(f);

                char header[256];
                snprintf(header, sizeof(header), 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Length: %ld\r\n"
                    "Connection: close\r\n"
                    "\r\n", fsize);
                send(client_fd, header, strlen(header), 0);
                send(client_fd, content, fsize, 0);
                free(content);
            } else {
                const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                send(client_fd, not_found, strlen(not_found), 0);
            }
        }
        close(client_fd);
    }
    return NULL;
}

int main() {
    printf("Starting bandwidth monitor for %s...\n", INTERFACE);
    printf("Logging hourly and daily totals to %s\n", LOG_FILE);
    
    pthread_t tid;
    if (pthread_create(&tid, NULL, server_thread, NULL) != 0) {
        perror("Failed to create server thread");
        return 1;
    }

    unsigned long long rx_prev = read_bytes(RX_FILE);
    unsigned long long tx_prev = read_bytes(TX_FILE);
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int cur_hour = t->tm_hour;
    int cur_mday = t->tm_mday;
    int cur_mon = t->tm_mon + 1;
    int cur_year = t->tm_year + 1900;

    unsigned long long hourly_rx = 0;
    unsigned long long hourly_tx = 0;
    unsigned long long daily_rx = 0;
    unsigned long long daily_tx = 0;

    while (1) {
        sleep(1);

        unsigned long long rx_curr = read_bytes(RX_FILE);
        unsigned long long tx_curr = read_bytes(TX_FILE);

        unsigned long long rx_diff = (rx_curr >= rx_prev) ? (rx_curr - rx_prev) : 0;
        unsigned long long tx_diff = (tx_curr >= tx_prev) ? (tx_curr - tx_prev) : 0;

        // Update global speed guarded by mutex
        pthread_mutex_lock(&speed_mutex);
        global_rx_speed = (double)rx_diff;
        global_tx_speed = (double)tx_diff;
        pthread_mutex_unlock(&speed_mutex);

        hourly_rx += rx_diff;
        hourly_tx += tx_diff;
        daily_rx += rx_diff;
        daily_tx += tx_diff;

        rx_prev = rx_curr;
        tx_prev = tx_curr;

        now = time(NULL);
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