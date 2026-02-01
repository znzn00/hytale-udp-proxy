#ifndef PROXY_COMMON_H
#define PROXY_COMMON_H

// using libraries
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <utility>

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
using socket_t = SOCKET;
#pragma comment(lib, "ws2_32.lib")
// For Linux/macOS
#else
using socket_t = int;
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define PROXY_IDDLE 0         // Proxy isn't connected to any server.
#define PROXY_CONNECTING 1    // Proxy is trying to make a server connnection
#define PROXY_READY 2         // Proxy is ready to accept a client connection.
#define PROXY_ESTABLISHED 3   // Proxy has connected the client and is starting to send to server.
#define PROXY_DISCONNECTING 4 // Proxy is clearing.

#define PROXY_DEFAULT_PORT 9520

int close_socket(socket_t s);

int test_ipv4_quic(in_addr ipv4, int port);
int test_ipv6_quic(in6_addr ipv6, int port);

enum eAddressType
{
    IPv4,
    IPv6,
    Domain,
    Invalid
};

std::tuple<eAddressType, std::string, int> resolve_server_address(std::string address);

#endif