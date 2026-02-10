# Raspberry Pi WLAN Monitor & Lennox S40 Integration

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
scp wlan_monitor_bin src/bandwidth_monitor/monitoringindex.html config/wlan_monitor.service config/99-update-dnsmasq-relay rapi@<IP>:~/

# 2. Install on Pi
sudo mv wlan_monitor_bin /usr/local/bin/wlan_monitor
sudo mv monitoringindex.html /srv/monitoringindex.html
sudo mv wlan_monitor.service /etc/systemd/system/
sudo mv 99-update-dnsmasq-relay /etc/NetworkManager/dispatcher.d/
```

## Usage
- **Dashboard:** `http://<pi-ip>:8080/`
- **WebSocket Data:** `ws://<pi-ip>:8888/`
- **Climate Data:** `http://<pi-ip>:8080/temperature`