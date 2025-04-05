#!/bin/bash
docker compose down
sudo rm -rf ./tmp
cmake --workflow --preset=x64-linux-dynamic
docker build --no-cache -t spkdfs:latest -f Dockerfiles/Dockerfile.dev .
docker compose up -d
# 集群启动后才进行测试?
# ctest --test-dir build/x64-linux-release
