#!/bin/bash

# Cross-compile
if [ "$1" == "o" ]; then
    clang++ --target=arm-linux-gnueabihf --sysroot=./rpi-sysroot \
        -fuse-ld=lld -Wall -Wextra -Wno-deprecated-declarations -O3 -flto \
        -I./rpi-sysroot/usr/include \
        -I./src \
        -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
        -L./rpi-sysroot/lib/arm-linux-gnueabihf \
        -luring -ltbb -lssl -lcrypto -lpthread -latomic -lboost_json \
        -std=c++26 \
        src/wlan_monitor.cpp -o wlan_monitor_bin
else
    clang++ --target=arm-linux-gnueabihf --sysroot=./rpi-sysroot \
        -fuse-ld=lld -Wall -Wextra -Wno-deprecated-declarations \
        -I./rpi-sysroot/usr/include \
        -I./src \
        -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
        -L./rpi-sysroot/lib/arm-linux-gnueabihf \
        -luring -ltbb -lssl -lcrypto -lpthread -latomic -lboost_json \
        -std=c++26 \
        src/wlan_monitor.cpp -o wlan_monitor_bin
fi

if [ $? -ne 0 ]; then
    echo "Compilation failed"
    exit 1
fi

# Build React Frontend
echo "Building React frontend..."
cd frontend && npm run build
if [ $? -ne 0 ]; then
    echo "Frontend build failed"
    exit 1
fi
cd ..

# Transfer (ls-pi is a ssh alias for my server)
scp -r wlan_monitor_bin \
    src/bandwidth_monitor/monitoringindex.html \
    src/reverse_proxy/proxyindex.html \
    src/reverse_proxy/proxy.config \
    config/wlan_monitor.service \
    config/wg0.conf \
    config/98-update-vpn \
    config/99-update-dnsmasq-relay \
    frontend/build/client \
    ls-pi:~/

# Deploy and restart
ssh ls-pi "sudo -S mv /home/rapi/wlan_monitor_bin /usr/local/bin/wlan_monitor && \
sudo mkdir -p /srv/landing && \
sudo mkdir -p /srv/bwith && \
sudo mkdir -p /srv/rp && \
sudo mkdir -p /etc/wlan_monitor && \
sudo rm -rf /srv/landing/* && \
sudo rm -rf /srv/bwith/* && \
sudo rm -rf /srv/rp/* && \
sudo mv /home/rapi/client/* /srv/landing/ && \
sudo rm -rf /home/rapi/client && \
sudo mv /home/rapi/monitoringindex.html /srv/bwith/index.html && \
sudo mv /home/rapi/proxyindex.html /srv/rp/index.html && \
sudo mv /home/rapi/wlan_monitor.service /etc/systemd/system/wlan_monitor.service && \
sudo mv /home/rapi/proxy.config /etc/wlan_monitor/proxy.config && \
sudo mkdir -p /etc/wireguard && \
sudo mv /home/rapi/wg0.conf /etc/wireguard/wg0.conf && \
sudo mv /home/rapi/98-update-vpn /etc/NetworkManager/dispatcher.d/98-update-vpn && \
sudo mv /home/rapi/99-update-dnsmasq-relay /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay && \
sudo chown root:root /etc/NetworkManager/dispatcher.d/98-update-vpn && \
sudo chown root:root /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay && \
sudo chmod +x /etc/NetworkManager/dispatcher.d/98-update-vpn && \
sudo chmod +x /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay && \
sudo mkdir -p /var/log/wlan_monitor && echo rapi | sudo -S chown rapi:rapi /var/log/wlan_monitor && \
sudo setcap 'cap_net_bind_service=+ep' /usr/local/bin/wlan_monitor && \
sudo systemctl daemon-reload && \
sudo systemctl restart wlan_monitor.service"
