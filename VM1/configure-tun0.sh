#!/bin/bash

g++ -std=c++11 -O3 -o tunnel64d /mnt/partage/tunnel64d.cpp
./tunnel64d &
sleep 3s
ip link set tun0 up
ip -6 a add fc00:1234:ffff::1/64 dev tun0
ip -6 r add fc00:1234:4::/64 dev tun0
fg