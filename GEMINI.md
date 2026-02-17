# Raspberry Pi WLAN Traffic Monitor & Lennox S40 Exploration

A real-time bandwidth monitoring system for Raspberry Pi and investigation tools for local Lennox S40 thermostat integration.

## Project Overview

### WLAN Monitor
- **Backend (`wlan_monitor.cpp`):** An asynchronous C++20 application utilizing `io_uring` for high-performance, multi-user handling. It polls `/sys/class/net/wlan0/statistics`, logs usage to `/var/log/wlan_monitor/`, and streams data over WebSockets (Port 8888).
- **AIO Landing Server:** A unified `io_uring` server that also listens on Port 80, serving a landing dashboard via `AioLandingHTTP`.
- **Architecture:** 
    - **Centralized Management:** Managed by `IoUringManager` (Singleton), which handles `io_uring` SQE allocation, event dispatching, and a centralized `SSL_CTX` for TLS connections.
    - **Unified Event Loop:** `wlan_monitor.cpp` processes both `io_uring` completions (CQEs) and an internal non-uring event queue for manual event triggering (e.g., re-processing cached data).
    - **Decoupled Events:** `IoEvent` subclasses register operations via `cache_call` or the non-uring `add` method.
    - **HTTP/TLS Protocol Support:** Utilizes `boost::beast` and OpenSSL via `InetSocketReadWriteEventHTTP`.
- **Frontend (`monitoringindex.html`):** A self-hosted Chart.js application served from `/srv/` that visualizes live traffic and historical logs.

### Lennox S40 Integration
- **Discovery:** Thermostat identified via mDNS at `rapi.local` / `192.168.12.10`.
- **Interface:** Local HTTPS REST API (`/zones`, `/equipments`).
- **Tools:** Python verification scripts (`verify_api.py`, `get_zones_simple.py`) for data extraction.

## Building and Running

### Cross-Compilation
The project is cross-compiled for 32-bit ARM using `clang++` and the `lld` linker with a local sysroot.

```bash
clang++ --target=arm-linux-gnueabihf --sysroot=./rpi-sysroot \
    -fuse-ld=lld \
    -I./rpi-sysroot/usr/include \
    -I./src \
    -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
    -L./rpi-sysroot/lib/arm-linux-gnueabihf \
    -luring -lssl -lcrypto -lpthread -latomic -lboost_json \
    -std=c++20 \
    src/wlan_monitor.cpp -o wlan_monitor_bin
```

### Deployment
The `redeploy.sh` script automates cross-compilation, file transfer, and service restart on the Pi (IP: `192.168.12.223`).

```bash
./redeploy.sh
```

1. **Manual Deployment (Legacy):**
   ```bash
   scp wlan_monitor_bin src/bandwidth_monitor/monitoringindex.html \
       src/aio_landing/dashboard.html src/aio_landing/landing.html \
       config/wlan_monitor.service rapi@192.168.12.223:~/
   ```
2. **Setup on Pi:**
   - Move files to `/usr/local/bin/` and `/srv/`.
   - Initialize logging in `/var/log/wlan_monitor/`.
   - Set capabilities: `sudo setcap "cap_net_bind_service=+ep" /usr/local/bin/wlan_monitor`.
   - Restart: `sudo systemctl daemon-reload && sudo systemctl restart wlan_monitor.service`.

## Key Files

- `wlan_monitor.cpp`: Main application entry point and unified event loop.
- `redeploy.sh`: Automated build and deployment script.
- `io_uring_manager.hpp`: Singleton manager for `io_uring`, centralized `SSL_CTX`, and non-uring events.
- `io_event.hpp`: Clean base class for all asynchronous events.
- `inet_socket_read_write_event_http.hpp`: HTTP/TLS-aware socket reading/writing.
- `aio_landing_server.hpp`: Implementation of the AIO landing page HTTP server.
- `landing.html`: Personal landing page overview.
- `accept_event.hpp`: Handles new client connections for the bandwidth monitor.
- `bandwidth_data_read_event.hpp`: Handles WS handshakes and log retrieval.
- `get_zones_simple.py`: Robust script for retrieving expanded Lennox S40 zone JSON.
- `rpi-sysroot/`: Local sysroot containing ARM headers (Boost 1.83, liburing, OpenSSL).

## Agent Interaction Policy
The user is working on this project for personal enjoyment and learning. 
- **Minimal Assistance:** Keep the level of assistance to a minimum.
- **Problem Reporting:** If a requested plan is flawed, impossible, or ill-advised, do not attempt to fix it or propose a better alternative. Simply point out the specific problem so the user can address it themselves.
- **Complexity Filter:** If a requested task is subjectively too complex (e.g., "implement TLS"), refuse to carry it out.
