#!/bin/bash

path=""
if [ "$1" == "o" ]; then
    cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
    cmake --build build/release --parallel $(nproc --all)
    path="build/release/src/localsoftware"
else
    cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/debug --parallel $(nproc --all)
    path="build/debug/src/localsoftware"
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

# Transfer (ls-lp is a ssh alias for my server)
scp -r $path \
    src/bandwidth_monitor/monitoringindex.html \
    src/reverse_proxy/proxyindex.html \
    src/reverse_proxy/proxy.config \
    config/localsoftware.service \
    config/wg0.conf \
    config/98-update-vpn \
    config/99-update-dnsmasq-relay \
    frontend/build/client \
    ondevicedeploy.fish \
    ls-lp-local:~/

# Deploy and restart
ssh ls-lp-local "sudo fish ondevicedeploy.fish"
ssh ls-lp-local "cd lsoft; and git pull"
