#include "proxy_common.h"

int close_socket(socket_t s)
{
#ifdef _WIN32
    return closesocket(s);
#else
    return close(s);
#endif
}
