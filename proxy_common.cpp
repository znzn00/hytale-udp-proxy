#include "proxy_common.h"
#include <ctime>
#include <cstdlib>
#include <regex>

int close_socket(socket_t s)
{
#ifdef _WIN32
    return closesocket(s);
#else
    return close(s);
#endif
}

typedef struct
{
    char *bytes;
    int size;
} udp_packet;

void create_quic_initial_packet(char *buffer, int *n)
{
    srand(time(0));
    int len = 0;
    // QUIC headerbit
    buffer[len++] = 0xc0;
    // QUIC version
    buffer[len++] = 0x00;
    buffer[len++] = 0x00;
    buffer[len++] = 0x00;
    buffer[len++] = 0x01;

    // Destination Connection ID
    // First, length
    buffer[len++] = 0x08;
    int limit = len + 8;
    while (len < limit)
    {
        buffer[len++] = rand();
    }

    // Source Connection ID
    buffer[len++] = 0x08;
    limit = len + 8;
    while (len < limit)
    {
        buffer[len++] = rand();
    }

    // Token Length
    buffer[len++] = 0x00;

    while (len < 1200) // filling everything with 0 because it doesn't matter
    {
        buffer[len++] = 0x00;
    }

    *n = len;
}

int test_ipv4_quic(in_addr ipv4, int port)
{
    char address[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &ipv4, address, sizeof(address)) == nullptr)
    {
        std::cerr << "inet_ntop failed: " << WSAGetLastError() << std::endl;
        return 1;
    }
    std::cout << "QUIC IPv4: Testing " << address << ":" << port << std::endl;

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0)
    {
        std::cout << "QUIC IPv4: Server socket creation failed." << std::endl;

        return 2;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = ipv4;
    int s = ::connect(serverSocket, (sockaddr *)&addr, sizeof(addr));
    if (s < 0)
    {
        std::cout << "QUIC IPv4: Couldn't connect to " << address << ":" << port << std::endl;
        close_socket(serverSocket);
        return 3;
    }

    sockaddr_in local_client{};
    socklen_t local_client_len = sizeof(local_client);

    if (getsockname(serverSocket, (sockaddr *)&local_client, &local_client_len) == 0)
    {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local_client.sin_addr, ip, sizeof(ip));

        uint16_t lport = ntohs(local_client.sin_port);

        std::cout << "QUIC IPv4: Connected to server " << address << ":" << port << " with " << ip
                  << ":" << lport << std::endl;
    }
    char buffer[2048];
    int n;

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

    create_quic_initial_packet(buffer, &n);
    std::cout << "QUIC IPv4: Sending QUIC packet." << std::endl;
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
        std::cout << "QUIC IPv4: Failed to send QUIC request to " << address << ":" << port << std::endl;
        return 1;
    }
    std::cout << "QUIC IPv4: Packet has been sent. Waiting for response..." << std::endl;
    n = recv(serverSocket, buffer, sizeof(buffer), 0);
    if (n < 0)
    {
#ifdef _WIN32
        if (WSAGetLastError() == WSAETIMEDOUT)
#else
        if (errno == EWOULDBLOCK || errno == EAGAIN)
#endif
        {
            std::cout << "QUIC IPv4: Server response timeout. " << std::endl;
        }
        else
        {
            std::cout << "QUIC IPv4: Server response error. " << std::endl;
        }

        close_socket(serverSocket);
        return 1;
    }

    close_socket(serverSocket);
    return 0;
}

int test_ipv6_quic(in6_addr ipv6, int port)
{
    char address[INET6_ADDRSTRLEN];
    if (inet_ntop(AF_INET6, &ipv6, address, sizeof(address)) == nullptr)
    {
        std::cerr << "inet_ntop failed: " << WSAGetLastError() << std::endl;
        return 1;
    }
    std::cout << "QUIC IPv6: Testing [" << address << "]:" << port << std::endl;

    int serverSocket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (serverSocket < 0)
    {
        std::cout << "QUIC IPv6: Server socket creation failed." << std::endl;

        return 2;
    }
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = ipv6;
    int s = ::connect(serverSocket, (sockaddr *)&addr, sizeof(addr));
    if (s < 0)
    {
        std::cout << "QUIC IPv6: Couldn't connect to " << address << ":" << port << std::endl;
        close_socket(serverSocket);
        return 3;
    }

    sockaddr_in6 local_client{};
    socklen_t local_client_len = sizeof(local_client);

    if (getsockname(serverSocket, (sockaddr *)&local_client, &local_client_len) == 0)
    {
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &local_client.sin6_addr, ip, sizeof(ip));

        uint16_t lport = ntohs(local_client.sin6_port);

        std::cout << "QUIC IPv6: Connected to server [" << address << "]:" << port << " with [" << ip
                  << "]:" << lport << std::endl;
    }
    char buffer[2048];
    int n;

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

    create_quic_initial_packet(buffer, &n);
    std::cout << "QUIC IPv6: Sending QUIC packet." << std::endl;
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
        std::cout << "QUIC IPv6: Failed to send QUIC request to " << address << ":" << port << std::endl;
        return 1;
    }
    std::cout << "QUIC IPv6: Packet has been sent. Waiting for response..." << std::endl;
    n = recv(serverSocket, buffer, sizeof(buffer), 0);
    if (n < 0)
    {
#ifdef _WIN32
        if (WSAGetLastError() == WSAETIMEDOUT)
#else
        if (errno == EWOULDBLOCK || errno == EAGAIN)
#endif
        {
            std::cout << "QUIC IPv6: Server response timeout. " << std::endl;
        }
        else
        {
            std::cout << "QUIC IPv6: Server response error. " << std::endl;
        }

        close_socket(serverSocket);
        return 1;
    }

    close_socket(serverSocket);
    return 0;
}

std::tuple<eAddressType, std::string, int> resolve_server_address(std::string address)
{
    int port = PROXY_DEFAULT_PORT;
    std::regex serverAddressRegex("^([^:\\[\\]]+|\\[[^\\[\\]]+\\])(?::([0-9]+))?$");
    std::regex domainRegex("^(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\\.)+[a-zA-Z]{2,}$");
    std::smatch matches;
    in_addr ipv4;
    in6_addr ipv6;

    if (std::regex_search(address, matches, serverAddressRegex))
    {
        std::string address = matches[1].str();
        if (address.empty())
        {
            return {eAddressType::Invalid, "", -1};
        }
        if (matches[2].matched)
        {
            try
            {
                port = std::stoi(matches[2].str());
                if (port < 1 || port > 65535)
                {
                    port = -1;
                }
            }
            catch (...)
            {
                port = -1;
            }
        }

        if (inet_pton(AF_INET, address.c_str(), &ipv4) == 1)
        {
            return {eAddressType::IPv4, address, port};
        }
        std::string raw_ipv6 = address.substr(1, address.length() - 2);
        if (address.length() > 1 && inet_pton(AF_INET6, raw_ipv6.c_str(), &ipv6) == 1)
        {
            return {eAddressType::IPv6, raw_ipv6, port};
        }

        // most expensive check
        if (std::regex_match(address, domainRegex))
        {
            return {eAddressType::Domain, address, port};
        }
    }

    return {eAddressType::Invalid, "", -1};
}