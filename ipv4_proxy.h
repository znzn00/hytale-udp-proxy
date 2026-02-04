#ifndef IPV4_PROXY_H
#define IPV4_PROXY_H

#include "proxy_common.h"

class IPv4Proxy
{
private:
    int proxySocket;
    std::atomic<int> state{PROXY_IDDLE};
    std::atomic<bool> running{false};
    void manage_server_response(int serverSocket, sockaddr_in client, socklen_t client_len);

public:
    IPv4Proxy(int proxySocket);
    int connect(in_addr serverIp4, int port);
    int disconnect();
    int get_state();
    bool is_running();
};

#endif