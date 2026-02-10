# Raspberry Pi WLAN Traffic Monitor & Lennox S40 Exploration

A real-time bandwidth monitoring system for Raspberry Pi and investigation tools for local Lennox S40 thermostat integration.

## Project Overview

### WLAN Monitor
- **Backend (`wlan_monitor.cpp`):** An asynchronous C++17 application utilizing `io_uring` for high-performance, multi-user handling. It polls `/sys/class/net/wlan0/statistics`, logs usage to `/var/log/wlan_monitor/`, and streams data over WebSockets (Port 8888).
- **Architecture:** 
    - Event-driven via `IoEvent` wrapper classes (`Accept`, `Read`, `Write`).
    - Robust handling of partial TCP reads/writes through event chaining and kernel-level linking (`IOSQE_IO_LINK`).
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
    -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
    -L./rpi-sysroot/lib/arm-linux-gnueabihf \
    -luring -lssl -lcrypto -lpthread -latomic \
    wlan_monitor.cpp -o wlan_monitor_bin
```

### Deployment
1. Copy files to the Pi (IP: `192.168.12.223`):
   ```bash
   scp wlan_monitor_bin monitoringindex.html wlan_monitor.service rapi@192.168.12.223:~/
   ```
2. Move to system locations:
   - Binary: `/usr/local/bin/wlan_monitor`
   - Frontend: `/srv/monitoringindex.html`
   - Service: `/etc/systemd/system/wlan_monitor.service`
3. Initialize Logging:
   ```bash
   sudo mkdir -p /var/log/wlan_monitor && sudo chown rapi:rapi /var/log/wlan_monitor
   ```
4. Restart Service:
   ```bash
   sudo systemctl daemon-reload && sudo systemctl restart wlan_monitor.service
   ```

## Key Files

- `wlan_monitor.cpp`: Main application entry point and event loop.
- `io_event.hpp`: Abstract base class for `io_uring` async events.
- `accept_event.hpp`: Handles new client connections.
- `bandwidth_data_read_event.hpp`: Handles HTTP/WebSocket handshakes and log retrieval.
- `bandwidth_data_write_event.hpp`: Handles asynchronous data transmission with partial-write support.
- `get_zones_simple.py`: Robust script for retrieving expanded Lennox S40 zone JSON.
- `rpi-sysroot/`: Local sysroot containing ARM headers (Boost 1.83, liburing, OpenSSL).
