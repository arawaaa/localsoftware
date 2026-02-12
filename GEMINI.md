# Raspberry Pi WLAN Traffic Monitor & Lennox S40 Exploration

A real-time bandwidth monitoring system for Raspberry Pi and investigation tools for local Lennox S40 thermostat integration.

## Project Overview

### WLAN Monitor
- **Backend (`wlan_monitor.cpp`):** An asynchronous C++20 application utilizing `io_uring` for high-performance, multi-user handling. It polls `/sys/class/net/wlan0/statistics`, logs usage to `/var/log/wlan_monitor/`, and streams data over WebSockets (Port 8888).
- **AIO Landing Server:** A unified `io_uring` server that also listens on Port 80, serving a landing dashboard via `AioLandingHTTP`.
- **Architecture:** 
    - **Centralized Management:** Managed by `IoUringManager` (Singleton), which handles `io_uring` SQE allocation and event dispatching.
    - **Decoupled Events:** `IoEvent` subclasses register operations via `cache_call` without direct ring access.
    - **HTTP Protocol Support:** Utilizes `boost::beast` parsers and serializers within `InetSocketReadWriteEventHTTP` for efficient, non-blocking HTTP handling.
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
1. Copy files to the Pi (IP: `192.168.12.223`):
   ```bash
   scp wlan_monitor_bin src/bandwidth_monitor/monitoringindex.html src/aio_landing/dashboard.html config/wlan_monitor.service rapi@192.168.12.223:~/
   ```
2. Move to system locations:
   - Binary: `/usr/local/bin/wlan_monitor`
   - Monitoring Frontend: `/srv/monitoringindex.html`
   - Landing Dashboard: `/srv/dashboard.html`
   - Service: `/etc/systemd/system/wlan_monitor.service`
3. Initialize Logging:
   ```bash
   sudo mkdir -p /var/log/wlan_monitor && sudo chown rapi:rapi /var/log/wlan_monitor
   ```
4. Permissions & Restart:
   ```bash
   sudo setcap "cap_net_bind_service=+ep" /usr/local/bin/wlan_monitor
   sudo systemctl daemon-reload && sudo systemctl restart wlan_monitor.service
   ```

## Key Files

- `wlan_monitor.cpp`: Main application entry point and unified event loop.
- `io_uring_manager.hpp`: Singleton manager for `io_uring` submissions and event tracking.
- `io_event.hpp`: Clean base class for all asynchronous events.
- `inet_socket_read_write_event_http.hpp`: HTTP-aware socket reading/writing using Boost.Beast.
- `aio_landing_server.hpp`: Implementation of the AIO landing page HTTP server.
- `accept_event.hpp`: Handles new client connections for the bandwidth monitor.
- `bandwidth_data_read_event.hpp`: Handles WS handshakes and log retrieval.
- `get_zones_simple.py`: Robust script for retrieving expanded Lennox S40 zone JSON.
- `rpi-sysroot/`: Local sysroot containing ARM headers (Boost 1.83, liburing, OpenSSL).
