# Raspberry Pi WLAN Traffic Monitor

A real-time bandwidth monitoring system for Raspberry Pi, featuring persistent logging, a WebSocket-based binary streaming server, and a single-file web visualization frontend.

## Project Overview

- **Purpose:** Monitor WLAN bandwidth usage in real-time and provide historical logging.
- **Architecture:** 
    - **Backend (`wlan_monitor.c`):** A multithreaded C application that polls `/sys/class/net/wlan0/statistics`, logs hourly/daily usage, and streams live speed data over WebSockets (Port 8888).
    - **Frontend (`index.html`):** A self-hosted Chart.js application that visualizes live traffic and displays historical usage tables.
    - **Network Integration:** Implements a Layer 3 bridge using `parprouted` and custom NetworkManager dispatcher scripts for dynamic IP mirroring and DHCP relaying.
- **mDNS:** The system is configured to respond to `rapi.local` via `avahi-daemon`.

## Building and Running

### Cross-Compilation
The project is designed to be cross-compiled for ARM architectures (Raspberry Pi).

```bash
arm-linux-gnu-gcc --sysroot=./rpi-sysroot \
    -B./rpi-sysroot/usr/lib/arm-linux-gnueabihf/ \
    -I./rpi-sysroot/usr/include/arm-linux-gnueabihf \
    -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
    -L./rpi-sysroot/lib/arm-linux-gnueabihf \
    -lpthread -lssl -lcrypto -lz -lzstd -latomic \
    wlan_monitor.c -o wlan_monitor
```

### Deployment
1. Copy the binary and `index.html` to the Raspberry Pi home directory.
2. Install the systemd service:
   ```bash
   sudo cp wlan_monitor.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable --now wlan_monitor.service
   ```

### Runtime Configuration
- **Server Port:** 8888 (Handles both HTTP GET for `index.html` and WebSocket upgrades).
- **Log File:** `wlan_usage.log` (Format: `[UNIX_TIMESTAMP] D/M/Y HOUR TX RX`).
- **WiFi Hardware:** Optimized for Realtek RTL8822BU using the `88x2bu` community driver in Concurrent Mode (AP+STA).

## Development Conventions

- **Binary Protocol:** WebSocket communication uses a custom binary protocol:
    - **History:** 28-byte `LogRecord` structs.
    - **Transition:** 24-byte marker (3x `0xFFFFFFFFFFFFFFFF`).
    - **Live:** Continuous 16-byte frames (2x `double` for RX/TX speeds).
- **Self-Hosting:** the C server serves `index.html` on standard HTTP GET requests to `/` to simplify deployment.
- **Dynamic Networking:** Network configuration (IP mirroring to `eth0`, DHCP relaying) is handled by `/etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay`.

## Key Files

- `wlan_monitor.c`: Main application source code.
- `index.html`: Web dashboard source code.
- `wlan_monitor.service`: Systemd unit file for the monitoring service.
- `test_ws.py`: Diagnostic script for testing WebSocket handshakes.
- `rpi-sysroot/`: Local sysroot containing ARM headers and libraries for cross-compilation.
