# Raspberry Pi WLAN Monitor & Lennox S40 Integration

An all in one C++ server to monitor bridge state, thermostat state. Very specific to my home automation setup. Also includes scripts and systemd service configurations to enable the 802.11<->802.3 bridge. Written by Arnav Rawat & Gemini 3. I (Arnav) designed the architecture for the io_uring server and the lennox data viewer, as well as helping debug the bridge configuration. Gemini wrote the majority of code here, but I took a driving role in specifying how the implementation should be done, essentially treating AI like an English language compiler.

## Compilation
```bash
clang++ --target=arm-linux-gnueabihf --sysroot=./rpi-sysroot \
    -fuse-ld=lld \
    -I./rpi-sysroot/usr/include -I./src \
    -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
    -L./rpi-sysroot/lib/arm-linux-gnueabihf \
    -luring -lssl -lcrypto -lpthread -latomic -lboost_json \
    src/wlan_monitor.cpp -o wlan_monitor_bin
```

## Deployment
```bash
# 1. Copy to Pi
scp wlan_monitor_bin src/bandwidth_monitor/monitoringindex.html src/lennox_server/dashboard.html config/wlan_monitor.service config/99-update-dnsmasq-relay rapi@<IP>:~/

# 2. Install on Pi
sudo mv wlan_monitor_bin /usr/local/bin/wlan_monitor
sudo mv monitoringindex.html /srv/monitoringindex.html
sudo mv dashboard.html /srv/dashboard.html
sudo mv wlan_monitor.service /etc/systemd/system/
sudo mv 99-update-dnsmasq-relay /etc/NetworkManager/dispatcher.d/
```

## Usage
- **Dashboard:** `http://<pi-ip>:8080/`
- **WebSocket Data:** `ws://<pi-ip>:8888/`
- **Climate Data:** `http://<pi-ip>:8080/temperature`
