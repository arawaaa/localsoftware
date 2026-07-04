#!/bin/bash

# Cross-compile
if [ "$1" == "o" ]; then
    clang++ -fuse-ld=lld -Wall -Wextra -Wno-deprecated-declarations -O3 -flto \
        -I./src -luring -ltbb -lssl -lcrypto -lpthread -latomic -lboost_json \
        -std=c++26 src/localsoftware.cpp -o localsoftware
else
    clang++ -fuse-ld=lld -Wall -Wextra -Wno-deprecated-declarations \
        -I./src -g -O0 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
        -luring -ltbb -lssl -lcrypto -lpthread -latomic -lboost_json \
        -std=c++26 src/localsoftware.cpp -o localsoftware
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
    ondevicedeploy.fish \
    ls-lp:~/

# Deploy and restart
ssh ls-lp "sudo fish ondevicedeploy.fish"
ssh ls-lp "cd localsoftware; and git pull"
