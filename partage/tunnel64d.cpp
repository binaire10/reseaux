#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <signal.h>

#include <iostream>
#include <array>
#include <algorithm>
#include <fstream>

int tun_alloc(std::string &dev) {
    struct ifreq ifr;
    int fd, err;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        perror("alloc tun");
        exit(-1);
    }

    memset(&ifr, 0, sizeof(ifr));

    /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
     *        IFF_TAP   - TAP device
     *
     *        IFF_NO_PI - Do not provide packet information
     */
    ifr.ifr_flags = IFF_TUN;
    std::fill_n(ifr.ifr_name, IFNAMSIZ, 0);
    std::copy_n(dev.begin(), IFNAMSIZ < dev.size() ? IFNAMSIZ : dev.size(), ifr.ifr_name);

    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
        perror("ioctl");
        close(fd);
        return err;
    }
    std::cout << ifr.ifr_name << '\n';
    return fd;
}

int copy_desc_data(int src, int dest) {
    std::array<char, 65535 + 40> buffer;
    ssize_t taille = read(src, buffer.data(), buffer.size());
    if (taille < 0) {
        perror("read");
        return -1;
    }
    write(dest, buffer.data(), taille);
    return taille;
}

int main(int argc, char **argv) {
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    sockaddr_in sockaddr_serveur;
    sockaddr_serveur.sin_family = AF_INET;
    sockaddr_serveur.sin_addr.s_addr = INADDR_ANY;

    sockaddr_in sockaddr_ext;
    sockaddr_ext.sin_family = AF_INET;

    std::string tun;

    {
        bool foundIpDistant = false;
        bool foundPortDistant = false;
        bool foundPortLocal = false;
        bool foundTunLabel = false;
        std::ifstream config{"/vagrant/tunnel64d.conf"};
        std::string line;
        while (std::getline(config, line)) {
            size_t equal = line.find('=');
            if (equal == std::string::npos)
                continue;
            std::string sub = line.substr(0, equal);
            if (sub == "inip") {
                inet_aton(line.data() + equal + 1, &sockaddr_serveur.sin_addr);
                std::cout << line << '\n';
            } else if (sub == "inport") {
                foundPortLocal = true;
                sockaddr_serveur.sin_port = htons(std::stoi(line.substr(equal + 1)));
                std::cout << line << '\n';
            } else if (sub == "options") {}
            else if (sub == "outip") {
                foundIpDistant = true;
                inet_aton(line.data() + equal + 1, &sockaddr_ext.sin_addr);
                std::cout << line << '\n';
            } else if (sub == "outport") {
                foundPortDistant = true;
                sockaddr_ext.sin_port = htons(std::stoi(line.substr(equal + 1)));
                std::cout << line << '\n';
            } else if (sub == "tun") {
                foundTunLabel = true;
                tun = line.substr(equal + 1);
                std::cout << line << '\n';
            }
        }
        if (!foundIpDistant || !foundPortDistant || !foundPortLocal || !foundTunLabel) {
            std::cerr << "invalid or not found config\n";
            return -1;
        }
    }

    tun.reserve(IFNAMSIZ);
    int fdTun = tun_alloc(tun);
    int fdServer = socket(AF_INET, SOCK_STREAM, 0);
    int fdExt = socket(AF_INET, SOCK_STREAM, 0);
    int listener = -1;


    int yes = 1;
    setsockopt(fdServer, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(fdServer, (sockaddr *) &sockaddr_serveur, sizeof(sockaddr_serveur)) == -1) {
        perror("bind");
        exit(-1);
    }
    if (listen(fdServer, 1) == -1) {
        perror("listen");
        exit(-1);
    }
    std::cout << "server running\n";

    int r;
    for (unsigned i{}; i < 30 && (r = connect(fdExt, (sockaddr *) &sockaddr_ext, sizeof(sockaddr_in))) < 0; ++i) {
        close(fdExt);
        sleep(1);
        fdExt = socket(AF_INET, SOCK_STREAM, 0);
    }

    std::cout << fdTun << ' ' << fdServer << ' ' << fdExt << '\n';

    if (r < 0) {
        perror("connect");
        return -1;
    }
    std::cout << "connected to extremity\n";

    fd_set pending_event;
    fd_set current_event;
    FD_ZERO(&pending_event);
    FD_SET(STDIN_FILENO, &pending_event);
    FD_SET(fdServer, &pending_event);
    FD_SET(fdExt, &pending_event);
    FD_SET(fdTun, &pending_event);
    for (;;) {
        current_event = pending_event;
        if (select(FD_SETSIZE, &current_event, nullptr, nullptr, nullptr) == -1)
            perror("select");
        // if (FD_ISSET(STDIN_FILENO, &current_event)) {
        //     char c;
        //     if (!(std::cin >> c)) {
        //         close(listener);
        //         close(fdTun);
        //         close(fdExt);
        //         close(fdServer);
        //         return 0;
        //     }
        // }

        if (FD_ISSET(fdExt, &current_event))
            if (copy_desc_data(fdExt, fdTun) < 0) {
                close(listener);
                close(fdTun);
                close(fdExt);
                close(fdServer);
                return -1;
            }

        if (FD_ISSET(fdTun, &current_event))
            if (copy_desc_data(fdTun, listener) < 0) {
                close(listener);
                close(fdTun);
                close(fdExt);
                close(fdServer);
                return -1;
            }

        if (FD_ISSET(fdServer, &current_event)) {
            sockaddr_in client;
            socklen_t len = sizeof(client);
            if (listener != -1) {
                close(listener);
                close(fdTun);
                close(fdExt);
                close(fdServer);
                return -1;
            }
            listener = accept(fdServer, (sockaddr *) &client, &len);
            std::cout << "Connection of " << inet_ntoa(client.sin_addr) << ':' << ntohs(client.sin_port) << std::endl;
        }
    }

    return 0;
}