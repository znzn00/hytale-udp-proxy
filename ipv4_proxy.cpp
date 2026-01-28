#include "ipv4_proxy.h"
#include "proxy_common.h"

void IPv4Proxy::manage_server_response(int serverSocket, sockaddr_in client, socklen_t client_len)
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
                std::cout << "IPv4: Why Failed to send to client." << std::endl;
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

int IPv4Proxy::connect(in_addr serverIp4, int port)
{
    if (this->state != PROXY_IDDLE || this->running)
    {
        return 1;
    }

    this->running = true;
    this->state = PROXY_CONNECTING;

    char address[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &serverIp4, address, sizeof(address)) == nullptr)
    {
        std::cerr << "inet_ntop failed: " << WSAGetLastError() << "\n";
        this->state = PROXY_IDDLE;
        return 1;
    }

    std::cout << "IPv4: Connecting to server " << address << ":" << port << std::endl;

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0)
    {
        std::cout << "IPv4: Server socket creation failed." << std::endl;
        this->state = PROXY_IDDLE;
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = serverIp4;
    int s = ::connect(serverSocket, (sockaddr *)&addr, sizeof(addr));
    if (s < 0)
    {
        std::cout << "IPv4: Couldn't connect to " << address << ":" << port << std::endl;
        close_socket(serverSocket);
        this->state = PROXY_IDDLE;
        return 1;
    }

    sockaddr_in local_client{};
    socklen_t local_client_len = sizeof(local_client);

    if (getsockname(serverSocket, (sockaddr *)&local_client, &local_client_len) == 0)
    {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local_client.sin_addr, ip, sizeof(ip));

        uint16_t port = ntohs(local_client.sin_port);

        std::cout << "IPv4: Connected to server with " << ip
                  << ":" << port << "\n";
    }

    sockaddr_in firstClient{};
    socklen_t firstClientLen = sizeof(firstClient);
    sockaddr_in client{};
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
        std::cout << "IPv4: Waiting for a client connection..." << std::endl;
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

        if (inet_ntop(AF_INET, &firstClient.sin_addr, address, sizeof(address)) == nullptr)
        {
            std::cerr << "inet_ntop failed: " << WSAGetLastError() << "\n";
            // close_socket(serverSocket);
            // return 1;
            std::cout << "IPv4: A client has connected: " << address << ":" << firstClient.sin_port << std::endl;
        }
        std::cout << "IPv4: A client has connected: " << address << ":" << firstClient.sin_port << std::endl;

        // std::cout << "IPv4: Waiting for a client connection..." << std::endl;
        std::thread server_thing(&IPv4Proxy::manage_server_response, this, serverSocket, firstClient,
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

            if (client.sin_family != firstClient.sin_family ||
                client.sin_port != firstClient.sin_port ||
                client.sin_addr.s_addr != firstClient.sin_addr.s_addr)
            {
                std::cout << "IPv4: New client connected, it should only support one, stopping..." << std::endl;
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
                    std::cout << "IPv4: Client " << address << ":" << firstClient.sin_port << " has been disconnected." << std::endl;
                    this->state = PROXY_READY;
                }
            }
        }

        // waiting for thread to die
        server_thing.join();
    }

    std::cout << "IPv4: Disconnected from servers." << std::endl;
    close_socket(serverSocket);
    this->state = PROXY_IDDLE;
    return 0;
}

int IPv4Proxy::disconnect()
{
    if (this->running || this->state == PROXY_IDDLE)
    {
        return 1;
    }
    this->running = false;
    std::cout << "IPv4: Stopping..." << std::endl;
    while (this->state != PROXY_IDDLE)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "IPv4: Stopped." << std::endl;
    return 0;
}