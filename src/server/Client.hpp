#pragma once

#include <iostream>
#include <string>
#include <netinet/in.h>

class Client {
public:
    Client(int fd, std::string &ip);
    ~Client();

    // getters
    const int &getFd() const;
    const std::string &getNickname() const;
    const std::string &getIP() const;

private:
    int fd;
    std::string nickname;
    std::string ip;
};
