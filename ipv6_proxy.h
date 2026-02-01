#ifndef IPV6_PROXY_H
#define IPV6_PROXY_H

#include "proxy_common.h"

class IPv6Proxy
{
private:
    int proxySocket;
    std::atomic<int> state{PROXY_IDDLE};
    std::atomic<bool> running{false};
    void manage_server_response(int serverSocket, sockaddr_in6 client, socklen_t client_len);

public:
    IPv6Proxy(int proxySocket);
    int connect(in6_addr serverIp6, int port);
    int disconnect();
};

#endif