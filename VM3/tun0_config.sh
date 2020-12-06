ip link set tun0 up
ip -6 a add fc00:1234:ffff::10/64 dev tun0
ip -6 r add fc00:1234:3::/64 dev tun0
