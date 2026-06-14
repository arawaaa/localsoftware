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
        src/localsoftware.cpp -o localsoftware
else
    clang++ --target=arm-linux-gnueabihf --sysroot=./rpi-sysroot \
        -fuse-ld=lld -Wall -Wextra -Wno-deprecated-declarations \
        -I./rpi-sysroot/usr/include \
        -I./src \
        -L./rpi-sysroot/usr/lib/arm-linux-gnueabihf \
        -L./rpi-sysroot/lib/arm-linux-gnueabihf \
        -luring -ltbb -lssl -lcrypto -lpthread -latomic -lboost_json \
        -std=c++26 \
        src/localsoftware.cpp -o localsoftware
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
scp -r localsoftware \
    src/bandwidth_monitor/monitoringindex.html \
    src/reverse_proxy/proxyindex.html \
    src/reverse_proxy/proxy.config \
    config/localsoftware.service \
    config/wg0.conf \
    config/98-update-vpn \
    config/99-update-dnsmasq-relay \
    frontend/build/client \
    ls-pi:~/

# Deploy and restart
ssh ls-pi "sudo -S mv /home/rapi/localsoftware /usr/local/bin/localsoftware && \
sudo mkdir -p /srv/landing && \
sudo mkdir -p /srv/bwith && \
sudo mkdir -p /srv/rp && \
sudo mkdir -p /etc/localsoftware && \
sudo rm -rf /srv/landing/* && \
sudo rm -rf /srv/bwith/* && \
sudo rm -rf /srv/rp/* && \
sudo mv /home/rapi/client/* /srv/landing/ && \
sudo rm -rf /home/rapi/client && \
sudo mv /home/rapi/monitoringindex.html /srv/bwith/index.html && \
sudo mv /home/rapi/proxyindex.html /srv/rp/index.html && \
sudo mv /home/rapi/localsoftware.service /etc/systemd/system/localsoftware.service && \
sudo mv /home/rapi/proxy.config /etc/localsoftware/proxy.config && \
sudo mkdir -p /etc/wireguard && \
sudo mv /home/rapi/wg0.conf /etc/wireguard/wg0.conf && \
sudo mv /home/rapi/98-update-vpn /etc/NetworkManager/dispatcher.d/98-update-vpn && \
sudo mv /home/rapi/99-update-dnsmasq-relay /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay && \
sudo chown root:root /etc/NetworkManager/dispatcher.d/98-update-vpn && \
sudo chown root:root /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay && \
sudo chmod +x /etc/NetworkManager/dispatcher.d/98-update-vpn && \
sudo chmod +x /etc/NetworkManager/dispatcher.d/99-update-dnsmasq-relay && \
sudo mkdir -p /var/log/localsoftware && echo rapi | sudo -S chown rapi:rapi /var/log/localsoftware && \
sudo rm -rf /var/log/wlan_monitor && \
sudo setcap 'cap_net_bind_service=+ep' /usr/local/bin/localsoftware && \
sudo systemctl daemon-reload && \
sudo systemctl restart localsoftware.service"
