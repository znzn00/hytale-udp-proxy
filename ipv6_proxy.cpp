#ifndef IPV6_PROXY_H
#define IPV6_PROXY_H

#include "proxy_common.h"

class IPv6Proxy
{
private:
    int proxySocket;
    std::atomic<int> state{PROXY_IDDLE};

public:
    IPv6Proxy(int proxySocket)
    {
        this->proxySocket = proxySocket;
    }

    int start(in_addr serverIp4, int port);
    int stop();

};

#endif