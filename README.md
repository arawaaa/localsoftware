# Raspberry Pi WLAN Monitor & Lennox S40 Integration

A high-performance asynchronous bandwidth monitoring system for Raspberry Pi, integrated with local Lennox S40 thermostat data extraction.

## Features

- **Real-time Bandwidth Monitoring:**
    - Asynchronous C++17 backend utilizing `io_uring` for multi-client WebSocket streaming.
    - High-performance polling of `/sys/class/net/wlan0/statistics`.
    - Integrated history logging to `/var/log/wlan_monitor/`.
    - Live-streaming frontend using Chart.js and Luxon.
- **Lennox S40 Integration:**
    - Secondary `Boost.Beast` based webserver for climate data.
    - Automatic discovery and local HTTPS API communication with S40 thermostats.
    - Serial processing model: updates climate cache after each request.
- **Pseudo-Bridge Networking:**
    - Custom NetworkManager dispatcher for dynamic DHCP relay configuration.
    - `parprouted` integration for transparent Layer 3 bridging between WiFi and Ethernet.

## Architecture

### Backend (io_uring)
The primary server (Port 8888) uses an event-driven state machine:
- `AcceptEvent`: Handles new TCP connections.
- `ReadEvent`: Manages HTTP GET requests and WebSocket handshakes.
- `WriteEvent`: Streams historical logs in chunks and transitions to 1s live updates.
- `TimerEvent`: Drives the 1-second live data push.
- `File`: RAII wrapper for safe file descriptor management.

### Lennox Integration (Boost.Beast)
A secondary thread (Port 8080) runs a serial HTTP server:
- `/`: Serves the monitoring dashboard.
- `/temperature`: Returns current Zone 0 temperature.
- `/humidity`: Returns current Zone 0 humidity.

## Development

### Prerequisites
- `clang++` with ARM cross-compilation support.
- `liburing-dev`, `libssl-dev`, `libboost-all-dev`.
- A local sysroot for Raspberry Pi (32-bit ARM).

### Compilation
```bash
clang++ --target=arm-linux-gnueabihf --sysroot=./rpi-sysroot \
    -fuse-ld=lld \
    -I./rpi-sysroot/usr/include \
    -I./src \
    -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
    -L./rpi-sysroot/lib/arm-linux-gnueabihf \
    -luring -lssl -lcrypto -lpthread -latomic -lboost_json \
    src/wlan_monitor.cpp -o wlan_monitor_bin
```

### Deployment
1. Copy files to the Pi:
   ```bash
   scp wlan_monitor_bin monitoringindex.html config/wlan_monitor.service config/99-update-dnsmasq-relay rapi@<IP>:~/
   ```
2. Setup system locations:
   - Binary: `/usr/local/bin/wlan_monitor`
   - Frontend: `/srv/monitoringindex.html`
   - Service: `/etc/systemd/system/wlan_monitor.service`
   - Dispatcher: `/etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay`

## Usage
- **Dashboard:** `http://<pi-ip>:8080/`
- **WebSocket Data:** `ws://<pi-ip>:8888/`
- **Climate Data:** `http://<pi-ip>:8080/temperature`
