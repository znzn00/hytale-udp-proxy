#include <iostream>
#include <string>
#include <iostream>
#include <thread>
#include <string>
#include <csignal>
#include <chrono>
#include <regex>
#include <atomic>

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

// For Windows
#ifdef _WIN32
#include <winsock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// For Linux/macOS
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
// #include <unistd.h>

std::atomic<bool> running{true};

void signalHandler(int signum)
{
    std::cout << "Stopping proxy..." << std::endl;
    running = false;
}

int close_socket(SOCKET s)
{
#ifdef _WIN32
    return closesocket(s);
#else
    return close(s);
#endif
}

// Need to test it
int connect_to_ipv6(int proxySocket, in6_addr serverIp6, int port)
{
    char address[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &serverIp6, address, sizeof(address)) == nullptr)
    {
        std::cerr << "inet_ntop failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    std::cout << "IPv6: Connecting to [" << address << "]:" << port << std::endl;

    int serverSocket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (serverSocket < 0)
    {
        std::cout << "IPv6: Server socket creation failed." << std::endl;
        return 1;
    }
    sockaddr_in6 serverAddress{};
    serverAddress.sin6_family = AF_INET6;
    serverAddress.sin6_port = htons(port);
    serverAddress.sin6_addr = serverIp6;
    int s = connect(serverSocket, (sockaddr *)&serverAddress, sizeof(serverAddress));
    if (s < 0)
    {
        std::cout << "IPv6: Couldn't connect to [" << address << "]:" << port << std::endl;
        close_socket(serverSocket);
        return 1;
    }

    std::cout << "IPv6: Couldn't connect to [" << address << "]:" << port << std::endl;
    sockaddr_in firstClient{};
    socklen_t firstClientLen = sizeof(firstClient);
    sockaddr_in client{};
    socklen_t clientLen = sizeof(client);
    char buffer[2048];

#ifdef _WIN32
    int timeoutMs = 0;
    setsockopt(proxySocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&timeoutMs, sizeof(timeoutMs));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&tv, sizeof(tv));
#endif

    std::cout << "IPv6: Waiting for a client connection..." << std::endl;

    int n = recvfrom(
        proxySocket,
        buffer,
        sizeof(buffer),
        0,
        (sockaddr *)&firstClient,
        &firstClientLen);

#ifdef _WIN32
    timeoutMs = 5000; // 5 seconds
    setsockopt(proxySocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&timeoutMs, sizeof(timeoutMs));
#else

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&tv, sizeof(tv));
#endif

    while (true)
    {
        int sent = sendto(serverSocket,
                          buffer,
                          strlen(buffer),
                          0,
                          (sockaddr *)&client,
                          sizeof(client));

        if (sent < 0)
        {
#ifdef _WIN32
            std::cerr << "sendto failed: " << WSAGetLastError() << "\n";
#else
            perror("sendto");
#endif
        }

        n = recvfrom(
            proxySocket,
            buffer,
            sizeof(buffer),
            0,
            (sockaddr *)&firstClient,
            &firstClientLen);

        if (n < 0)
        {
#ifdef _WIN32
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                // timeout occurred
            }
#else
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                // timeout occurred
            }
#endif
        }
    }

    return 0;
}

std::atomic<bool> server_running{false};

void manage_server_response_ipv4(int proxySocket, int serverSocket, sockaddr_in client, socklen_t client_len)
{
    if (!server_running)
    {
        server_running = true;
    }
    char buffer[2048];
    while (server_running)
    {
        int n = recv(serverSocket, buffer, sizeof(buffer), 0);
        if (n >= 0)
        {
            // buffer[n] = '\0';
            int sends = sendto(proxySocket, buffer, n, 0, (sockaddr *)&client, client_len);
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

int connect_to_ipv4(int proxySocket, in_addr serverIp4, int port)
{
    char address[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &serverIp4, address, sizeof(address)) == nullptr)
    {
        std::cerr << "inet_ntop failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    std::cout << "IPv4: Connecting to server " << address << ":" << port << std::endl;

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0)
    {
        std::cout << "IPv4: Server socket creation failed." << std::endl;
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = serverIp4;
    int s = connect(serverSocket, (sockaddr *)&addr, sizeof(addr));
    if (s < 0)
    {
        std::cout << "IPv4: Couldn't connect to " << address << ":" << port << std::endl;
        close_socket(serverSocket);
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

    std::cout << "IPv4: Waiting for a client connection..." << std::endl;

    int n;

    while (running)
    {
        n = recvfrom(
            proxySocket,
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
            break;
        }
    }

    if (!running)
    {
        std::cout << "IPv4: Disconnected from servers." << std::endl;
        close_socket(serverSocket);
        return 0;
    }

    if (inet_ntop(AF_INET, &firstClient.sin_addr, address, sizeof(address)) == nullptr)
    {
        std::cerr << "inet_ntop failed: " << WSAGetLastError() << "\n";
        close_socket(serverSocket);
        return 1;
    }
    std::cout << "IPv4: A client has connected: " << address << ":" << firstClient.sin_port << std::endl;

    // std::cout << "IPv4: Waiting for a client connection..." << std::endl;
    std::thread server_thing(manage_server_response_ipv4, proxySocket, serverSocket, firstClient,
                             firstClientLen);

    while (running)
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
            proxySocket,
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
            break;
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
                break;
            }
        }
    }
    std::cout << "IPv4: Waiting for server connection to drop..." << std::endl;
    server_running = false;
    server_thing.join();

    close_socket(serverSocket);
    std::cout << "IPv4: Server connection has been dropped." << std::endl;

    return 0;
}

void connect_to_server(int proxySocket, int four, in_addr serverIp4,
                       int six, in6_addr serverIp6, int port)
{
    int state = 1;
    // not working for now
    // if (six && 0)
    // {
    //     state = connect_to_ipv6(proxySocket, serverIp6, port);
    //     if (state)
    //     {
    //         std::cout << "IPv6: Couldn't connect to server.";
    //     }
    // }

    if (four && state)
    {
        connect_to_ipv4(proxySocket, serverIp4, port);
    }
}

int main()
{
    std::signal(SIGINT, signalHandler);
#ifdef _WIN32
    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }
#endif
    // default Hytale server
    int targetProxySocket = 9520;
    std::string input;
    std::size_t pos;

    while (running)
    {
        targetProxySocket = 9520;
        std::cout << "Enter Proxy Socket (9520): ";
        std::getline(std::cin, input);

        if (input.empty())
        {
            break;
        }
        else
        {
            try
            {

                targetProxySocket = std::stoi(input, &pos);
                if (pos == input.length())
                {
                    if (targetProxySocket > 0 && targetProxySocket <= 65535)
                    {
                        break;
                    }
                    std::cerr << "Proxy port must be in range [1-65535]." << std::endl;
                    continue;
                }
                std::cerr << "Must be a valid number." << std::endl;
            }
            catch (const std::invalid_argument &e)
            {
                std::cerr << "Invalid argument error for input: " << e.what() << std::endl;
            }
            catch (const std::out_of_range &e)
            {
                std::cerr << "Out of range error for input: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "An unknown error occurred." << std::endl;
            }
        }
    }

    int proxySocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (proxySocket < 0)
    {
        perror("Proxy socket creation failed");
        exit(EXIT_FAILURE);
    }

    // specifying the address
    sockaddr_in proxyAddress;
    proxyAddress.sin_family = AF_INET;
    proxyAddress.sin_port = htons(targetProxySocket);
    proxyAddress.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxySocket, (struct sockaddr *)&proxyAddress, sizeof(proxyAddress)) == -1)
    {
        perror("Proxy bind failed");
        close_socket(proxySocket);
        exit(EXIT_FAILURE);
    }

    // Setting timeouts
#ifdef _WIN32
    int timeoutMs = 10000;
    setsockopt(proxySocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&timeoutMs, sizeof(timeoutMs));
#else
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(proxySocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&tv, sizeof(tv));
#endif

    char ipAddress[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(proxyAddress.sin_addr), ipAddress, INET_ADDRSTRLEN);
    std::cout << "Proxy bound to \"" << ipAddress << ":" << ntohs(proxyAddress.sin_port) << "\"." << std::endl;

    // Yes, I abuse RegEx, yes, it's a easy regex.
    // I know it isn't perfect but I just wanted it to be lightweight.
    // IPv6 aren't supported, but it should be encased in [], like [::1].
    // Only thing this validate correctly is if port is numbers, and it's a optional parameter.
    std::regex serverAddressRegex("^([^:\\[\\]]+|\\[[^\\[\\]]+\\])(?::([0-9]+))?$");
    std::smatch matches;

    struct in_addr serverIp4;
    struct in6_addr serverIp6;
    struct addrinfo *result = nullptr;
    struct addrinfo *p = nullptr;
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    char ip_string[INET6_ADDRSTRLEN];

    while (running)
    {
        int serverPort = 9520;
        std::cout << "Enter server address: ";
        std::getline(std::cin, input);
        if (input.empty())
        {
            std::cout << "Server address shouldn't be empty." << std::endl;
            continue;
        }
        else if (std::regex_search(input, matches, serverAddressRegex))
        {
            if (matches[2].matched)
            {
                try
                {
                    if (serverPort < 1 || serverPort > 65535)
                    {

                        std::cerr << "Server port must be in range [1-65535]." << std::endl;
                        continue;
                    }
                    serverPort = std::stoi(matches[2].str());
                }
                catch (const std::invalid_argument &e)
                {
                    std::cerr << "Invalid argument error for input: " << e.what() << std::endl;
                    continue;
                }
                catch (const std::out_of_range &e)
                {
                    std::cerr << "Out of range error for input: " << e.what() << std::endl;
                    continue;
                }
                catch (...)
                {
                    std::cerr << "An unknown error occurred." << std::endl;
                    continue;
                }
            }

            int status = getaddrinfo(matches[1].str().c_str(), NULL, &hints, &result);
            if (status != 0)
            {
                fprintf(stderr, "error in getaddrinfo: %s\n", gai_strerror(status));
                continue;
            }
            int four = 0;
            int six = 0;
            for (p = result; p != NULL; p = p->ai_next)
            {
                // void *addr;

                if (p->ai_family == AF_INET)
                { // IPv4
                    struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
                    // addr = &(ipv4->sin_addr);
                    serverIp4 = (ipv4->sin_addr);
                    four++;
                }
                else
                { // IPv6
                    struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
                    // addr = &(ipv6->sin6_addr);
                    serverIp6 = (ipv6->sin6_addr);
                    six++;
                }
            }
            freeaddrinfo(result);
            if (four > 1 || six > 1)
            {
                std::cout << "Currently, we don't support connection to domains with more than one IPv4 or IPv6." << std::endl;
                continue;
            }

            // std::thread connection_loop(connect_to_server, proxySocket, four, serverIp4, six, serverIp6, serverPort);
            while (running)
            {
                connect_to_server(proxySocket, four, serverIp4, six, serverIp6, serverPort);
            }
        }
        else
        {
            std::cout << "Invalid server address: " << input << std::endl;
            continue;
        }
    }

    std::cout << "Proxy has been stopped successfully." << std::endl;
    close_socket(proxySocket);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
