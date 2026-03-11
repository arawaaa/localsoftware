# Raspberry Pi WLAN Traffic Monitor & Lennox S40 Exploration

A real-time bandwidth monitoring system for Raspberry Pi and investigation tools for local Lennox S40 thermostat integration.

## Project Overview

### WLAN Monitor
- **Backend (`wlan_monitor.cpp`):** An asynchronous C++20 application utilizing `io_uring` for high-performance, multi-user handling. It polls `/sys/class/net/wlan0/statistics`, logs usage to `/var/log/wlan_monitor/`, and streams data over WebSockets (Port 8888).
- **AIO Landing Server:** A unified `io_uring` server that also listens on Port 80, serving a landing dashboard via `AioLandingHTTP`.
- **Architecture:** 
    - **Centralized Management:** Managed by `IoUringManager` (Singleton), which handles `io_uring` SQE allocation, event dispatching, and a centralized `SSL_CTX` for TLS connections.
    - **Unified Event Loop:** `wlan_monitor.cpp` processes both `io_uring` completions (CQEs) and an internal non-uring event queue for manual event triggering.
    - **HTTP/TLS Protocol Support:** Utilizes `boost::beast` and OpenSSL via `InetSocketReadWriteEventHTTP`.
- **Frontend (`monitoringindex.html`):** A self-hosted Chart.js application served from `/srv/` that visualizes live traffic and historical logs.

### Network & Connectivity (GCP Gateway)
- **VPS Tunnel:** A WireGuard VPN tunnel connects the local Raspberry Pi (`10.0.0.2`) to a GCP e2-micro VPS (`10.0.0.1`).
- **Public Gateway:** The VPS acts as a transparent proxy. Ports 80 and 443 are forwarded via `iptables` DNAT to the Pi.
- **Real IP Support:** 
    - **Policy Routing:** The Pi uses a custom routing table (`Table 200`) and `ip rule` to ensure replies to tunnel traffic exit back through `wg0`.
    - **No Masquerade:** The VPS performs `DNAT` but preserves the original source IP. The Pi's `rp_filter` is adjusted to permit these asymmetric-looking packets.
- **Persistence:** Managed by `iptables-persistent` on the VPS and `wg-routing.service` on the Pi.

### Lennox S40 Integration
- **Discovery:** Thermostat identified via mDNS at `rapi.local` / `192.168.12.10`.
- **Interface:** Local HTTPS REST API (`/zones`, `/equipments`).
- **Tools:** Python verification scripts (`verify_api.py`, `get_zones_simple.py`) for data extraction.

## Building and Running

### Deployment
The `redeploy.sh` script automates cross-compilation, React frontend builds, file transfer, and service management for both the monitor and the WireGuard tunnel.

```bash
./redeploy.sh
```

1. **Setup on Pi:**
   - Moves files to `/usr/local/bin/`, `/srv/`, and `/etc/wireguard/`.
   - Configures systemd services for `wlan_monitor` and `wg-routing`.
   - Sets capabilities: `sudo setcap "cap_net_bind_service=+ep" /usr/local/bin/wlan_monitor`.

## Key Files

- `wlan_monitor.cpp`: Main application entry point.
- `redeploy.sh`: Automated build and deployment script.
- `config/wg0.conf`: WireGuard client configuration for the Pi.
- `config/wg-routing.service`: Custom systemd service for Pi-side policy routing and firewall rules.
- `io_uring_manager.hpp`: Singleton manager for `io_uring` and centralized `SSL_CTX`.
- `inet_socket_read_write_event_http.hpp`: HTTP/TLS-aware socket reading/writing.
- `aio_landing_server.hpp`: Implementation of the AIO landing page HTTP server.
- `get_zones_simple.py`: Robust script for retrieving expanded Lennox S40 zone JSON.

## Agent Interaction Policy
The user is working on this project for personal enjoyment and learning. 
- **Minimal Assistance:** Keep the level of assistance to a minimum.
- **Problem Reporting:** If a requested plan is flawed, point out the specific problem.
- **Complexity Filter:** Refuse tasks that are subjectively too complex.
