#include "ipv6_proxy.h"
#include "proxy_common.h"

IPv6Proxy::IPv6Proxy(int proxySocket)
{
    this->proxySocket = proxySocket;
}

void IPv6Proxy::manage_server_response(int serverSocket, sockaddr_in6 client, socklen_t client_len)
{
    char buffer[2048];
    while (this->running && this->state == PROXY_ESTABLISHED)
    {
        int n = recv(serverSocket, buffer, sizeof(buffer), 0);
        if (n >= 0)
        {
            // buffer[n] = '\0';
            int sends = sendto(this->proxySocket, buffer, n, 0, (sockaddr *)&client, client_len);
            if (sends < 0)
            {
                std::cout << "IPv6: Why Failed to send to client." << std::endl;
            }
        }
        else
        {
#ifdef _WIN32
            if (WSAGetLastError() == WSAETIMEDOUT)
#else
            if (errno == EWOULDBLOCK || errno == EAGAIN)
#endif
            {
                // std::cout << "Server timeout." << std::endl;
            }
        }
    }
}

int IPv6Proxy::connect(in6_addr serverIp6, int port)
{
    if (this->state != PROXY_IDDLE || this->running)
    {
        return 1;
    }

    this->running = true;
    this->state = PROXY_CONNECTING;

    char address[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &serverIp6, address, sizeof(address)) == nullptr)
    {
        std::cerr << "inet_ntop failed: " << WSAGetLastError() << "\n";
        this->state = PROXY_IDDLE;
        return 1;
    }

    std::cout << "IPv6: Connecting to server [" << address << "]:" << port << std::endl;

    int serverSocket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (serverSocket < 0)
    {
        std::cout << "IPv6: Server socket creation failed." << std::endl;
        this->state = PROXY_IDDLE;
        return 1;
    }
    sockaddr_in6 serverAddress{};
    serverAddress.sin6_family = AF_INET6;
    serverAddress.sin6_port = htons(port);
    serverAddress.sin6_addr = serverIp6;
    int s = ::connect(serverSocket, (sockaddr *)&serverAddress, sizeof(serverAddress));
    if (s < 0)
    {
        std::cout << "IPv6: Couldn't connect to [" << address << "]:" << port << std::endl;
        close_socket(serverSocket);
        this->state = PROXY_IDDLE;
        return 1;
    }

    sockaddr_in6 local_client{};
    socklen_t local_client_len = sizeof(local_client);

    if (getsockname(serverSocket, (sockaddr *)&local_client, &local_client_len) == 0)
    {
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &local_client.sin6_addr, ip, sizeof(ip));

        uint16_t port = ntohs(local_client.sin6_port);

        std::cout << "IPv6: Connected to server with [" << ip
                  << "]:" << port << "\n";
    }

    sockaddr_in6 firstClient{};
    socklen_t firstClientLen = sizeof(firstClient);
    sockaddr_in6 client{};
    socklen_t clientLen = sizeof(client);
    char buffer[2048];

#ifdef _WIN32

#else

#endif

#ifdef _WIN32
    int timeoutMs = 10000;
    setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&timeoutMs, sizeof(timeoutMs));
#else
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&tv, sizeof(tv));
#endif
    int n;

    this->state = PROXY_READY;

    while (this->running)
    {
        std::cout << "IPv6: Waiting for a client connection..." << std::endl;
        while (this->running && this->state == PROXY_READY)
        {
            n = recvfrom(
                this->proxySocket,
                buffer,
                sizeof(buffer),
                0,
                (sockaddr *)&firstClient,
                &firstClientLen);

            if (n < 0)
            {
#ifdef _WIN32
                if (WSAGetLastError() == WSAETIMEDOUT)
#else
                if (errno == EWOULDBLOCK || errno == EAGAIN)
#endif
                { // If there's a timeout, it can continue
                    continue;
                }
            }
            else
            {
                // received a connection
                this->state = PROXY_ESTABLISHED;
            }
        }

        if (inet_ntop(AF_INET6, &firstClient.sin6_addr, address, sizeof(address)) == nullptr)
        {
            std::cerr << "inet_ntop failed: " << WSAGetLastError() << "\n";
            // close_socket(serverSocket);
            // return 1;
        }
        std::cout << "IPv6: A client has connected: [" << address << "]:" << firstClient.sin6_port << std::endl;

        // std::cout << "IPv6: Waiting for a client connection..." << std::endl;
        std::thread server_thing(&IPv6Proxy::manage_server_response, this, serverSocket, firstClient,
                                 firstClientLen);

        while (this->running && this->state == PROXY_ESTABLISHED)
        {
            int sentd = send(serverSocket,
                             buffer,
                             n,
                             0);

            if (sentd < 0)
            {
#ifdef _WIN32
                std::cerr << "sendto failed: " << WSAGetLastError() << "\n";
#else
                perror("sendto");
#endif
            }

            n = recvfrom(
                this->proxySocket,
                buffer,
                sizeof(buffer),
                0,
                (sockaddr *)&client,
                &clientLen);

            if (client.sin6_scope_id != firstClient.sin6_scope_id ||
                client.sin6_port != firstClient.sin6_port ||
                memcmp(&client.sin6_addr, &firstClient.sin6_addr, sizeof(struct in6_addr)) != 0)
            {
                std::cout << "IPv6: New client connected, it should only support one, stopping..." << std::endl;
                this->state = PROXY_READY;
            }

            if (n < 0)
            {
#ifdef _WIN32
                if (WSAGetLastError() == WSAETIMEDOUT)
#else
                if (errno == EWOULDBLOCK || errno == EAGAIN)
#endif
                { // If timeout, it's probable the connection dropped.
                    std::cout << "IPv6: Client [" << address << "]:" << firstClient.sin6_port << " has been disconnected." << std::endl;
                    this->state = PROXY_READY;
                }
            }
        }

        // waiting for thread to die
        server_thing.join();
    }

    std::cout << "IPv6: Disconnected from servers." << std::endl;
    close_socket(serverSocket);
    this->state = PROXY_IDDLE;
    return 0;
}

int IPv6Proxy::disconnect()
{
    if (!this->running || this->state == PROXY_IDDLE)
    {
        return 1;
    }
    this->running = false;
    std::cout << "IPv6: Stopping..." << std::endl;
    while (this->state != PROXY_IDDLE)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "IPv6: Stopped." << std::endl;
    return 0;
}