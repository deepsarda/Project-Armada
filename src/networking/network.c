#include "../../include/networking/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <mstcpip.h>
#else
#include <netinet/tcp.h>
#endif

#if defined(_WIN32)
static int net_platform_initialized = 0;

static void net_platform_cleanup(void)
{
    if (net_platform_initialized)
    {
        WSACleanup();
        net_platform_initialized = 0;
    }
}

static int net_ensure_platform_initialized(void)
{
    if (net_platform_initialized)
    {
        return 0;
    }

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }

    net_platform_initialized = 1;
    atexit(net_platform_cleanup);
    return 0;
}
#else
static int net_ensure_platform_initialized(void)
{
    return 0;
}
#endif

static void net_get_time(struct timeval *tv)
{
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    const unsigned long long WINDOWS_TICK = 10000000ULL;
    const unsigned long long SEC_TO_UNIX_EPOCH = 11644473600ULL;
    unsigned long long total_microseconds = (uli.QuadPart / 10ULL) - (SEC_TO_UNIX_EPOCH * 1000000ULL);
    tv->tv_sec = (long)(total_microseconds / 1000000ULL);
    tv->tv_usec = (long)(total_microseconds % 1000000ULL);
#else
    gettimeofday(tv, NULL);
#endif
}

void net_log_socket_error(const char *context)
{
#if defined(_WIN32)
    int err = WSAGetLastError();
    fprintf(stderr, "%s: WSA error %d\n", context ? context : "socket error", err);
#else
    perror(context);
#endif
}

/**
 * Create a TCP server socket bound to the given port.
 * Returns the socket file descriptor on success, -1 on failure.
 */
net_socket_t net_create_server_socket(int port)
{
    if (net_ensure_platform_initialized() != 0)
    {
        return NET_INVALID_SOCKET;
    }

    net_socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == NET_INVALID_SOCKET)
    {
        net_log_socket_error("socket");
        return NET_INVALID_SOCKET;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)) == NET_SOCKET_ERROR)
    {
        net_log_socket_error("setsockopt");
        net_close_socket(server_fd);
        return NET_INVALID_SOCKET;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == NET_SOCKET_ERROR)
    {
        net_log_socket_error("bind");
        net_close_socket(server_fd);
        return NET_INVALID_SOCKET;
    }

    if (listen(server_fd, 3) == NET_SOCKET_ERROR)
    {
        net_log_socket_error("listen");
        net_close_socket(server_fd);
        return NET_INVALID_SOCKET;
    }

    return server_fd;
}

/**
 * Connect to a TCP server at the given host and port.
 * Returns the socket file descriptor on success, -1 on failure.
 */
net_socket_t net_connect_to_server(const char *host, int port)
{
    if (net_ensure_platform_initialized() != 0)
    {
        return NET_INVALID_SOCKET;
    }

    net_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == NET_INVALID_SOCKET)
    {
        net_log_socket_error("socket");
        return NET_INVALID_SOCKET;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    int pton_result = inet_pton(AF_INET, host, &serv_addr.sin_addr);
    if (pton_result <= 0)
    {
        if (host && strcmp(host, "localhost") == 0)
        {
            pton_result = inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
        }
    }

    if (pton_result <= 0)
    {
        fprintf(stderr, "Invalid address: %s\n", host ? host : "(null)");
        net_close_socket(sock);
        return NET_INVALID_SOCKET;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == NET_SOCKET_ERROR)
    {
        net_log_socket_error("connect");
        net_close_socket(sock);
        return NET_INVALID_SOCKET;
    }

    // Enable TCP_NODELAY to reduce latency (disable Nagle's algorithm)
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

    return sock;
}

/**
 * Close a socket if valid.
 */
void net_close_socket(net_socket_t sock)
{
    if (sock != NET_INVALID_SOCKET)
    {
        NET_CLOSE_SOCKET(sock);
    }
}

/**
 * Send a GameEvent struct over the socket.
 * Returns 1 on success, 0 on failure.
 */
int net_send_event(net_socket_t sock, const GameEvent *event)
{
    if (sock == NET_INVALID_SOCKET)
        return 0;
    ssize_t sent = send(sock, (const char *)event, sizeof(GameEvent), 0);
    if (sent != (ssize_t)sizeof(GameEvent))
    {
        if (sent == NET_SOCKET_ERROR)
        {
            net_log_socket_error("send");
        }
        return 0;
    }
    return 1;
}

/**
 * Receive a GameEvent struct from the socket with flags.
 * Returns 1 on success, 0 if no data (non-blocking), -1 on error/disconnect.
 */
int net_receive_event_flags(net_socket_t sock, GameEvent *event, int flags)
{
    if (sock == NET_INVALID_SOCKET || !event)
        return -1;

    int wants_nonblock = (flags & NET_MSG_DONTWAIT) != 0;

    // Use select() for non-blocking check on all platforms for consistency
    if (wants_nonblock)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0; // Zero timeout = poll

#if defined(_WIN32)
        int ready = select(0, &readfds, NULL, NULL, &tv);
#else
        int ready = select((int)(sock + 1), &readfds, NULL, NULL, &tv);
#endif
        if (ready < 0)
        {
            int last_error = NET_ERRNO();
            if (last_error == NET_EINTR)
                return 0;
            net_log_socket_error("select");
            return -1;
        }
        if (ready == 0)
        {
            return 0; // No data available
        }
    }

    // Now do a blocking recv - we know data is available if non-blocking was requested
    ssize_t valread = recv(sock, (char *)event, sizeof(GameEvent), 0);

    if (valread == 0)
    {
        return -1; // disconnected
    }

    if (valread < 0)
    {
        int last_error = NET_ERRNO();
        if (last_error == NET_EWOULDBLOCK || last_error == NET_EAGAIN || last_error == NET_EINTR)
        {
            return 0; // no data available for non-blocking polls
        }
        net_log_socket_error("recv");
        return -1;
    }

    if (valread != (ssize_t)sizeof(GameEvent))
    {
        fprintf(stderr, "Warning: Partial event read %zd/%zu\n", valread, sizeof(GameEvent));
        return -1;
    }

    return 1;
}

/**
 * Receive a GameEvent struct from the socket (blocking).
 * Returns 1 on success, 0 on error/disconnect.
 */
int net_receive_event(net_socket_t sock, GameEvent *event)
{
    int result = net_receive_event_flags(sock, event, 0);
    return result > 0 ? 1 : 0;
}

/**
 * Receive a GameEvent struct from the socket with a timeout.
 * Returns 1 on success, 0 on timeout, -1 on error/disconnect.
 * This is useful for server client threads that need to periodically
 * check if the server is still running.
 */
int net_receive_event_timeout(net_socket_t sock, GameEvent *event, int timeout_ms)
{
    if (sock == NET_INVALID_SOCKET || !event)
        return -1;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

#if defined(_WIN32)
    int ready = select(0, &readfds, NULL, NULL, &tv);
#else
    int ready = select((int)(sock + 1), &readfds, NULL, NULL, &tv);
#endif
    if (ready < 0)
    {
        int last_error = NET_ERRNO();
        if (last_error == NET_EINTR)
            return 0; // Interrupted, treat as timeout
        net_log_socket_error("select");
        return -1;
    }
    if (ready == 0)
    {
        return 0; // Timeout, no data available
    }

    // Data is available, do a blocking recv
    ssize_t valread = recv(sock, (char *)event, sizeof(GameEvent), 0);

    if (valread == 0)
    {
        return -1; // disconnected
    }

    if (valread < 0)
    {
        int last_error = NET_ERRNO();
        if (last_error == NET_EWOULDBLOCK || last_error == NET_EAGAIN || last_error == NET_EINTR)
        {
            return 0; // Treat as timeout
        }
        net_log_socket_error("recv");
        return -1;
    }

    if (valread != (ssize_t)sizeof(GameEvent))
    {
        fprintf(stderr, "Warning: Partial event read %zd/%zu\n", valread, sizeof(GameEvent));
        return -1;
    }

    return 1;
}

/**
 * Check if candidate host is already in the hosts list.
 * Returns 1 if found, 0 otherwise.
 */
static int net_host_list_contains(char hosts[][64], int count, const char *candidate)
{
    if (!candidate)
    {
        return 0;
    }

    for (int i = 0; i < count; ++i)
    {
        if (strncmp(hosts[i], candidate, 64) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int net_discover_lan_servers_udp(char hosts[][64], int max_hosts, int port, int timeout_ms);

/**
 * Discover LAN servers by probing common local IPs.
 * Fills hosts array with found server IPs (up to max_hosts).
 * Returns number of servers found.
 */
int net_discover_lan_servers(char hosts[][64], int max_hosts, int port, int timeout_ms)
{
    if (!hosts || max_hosts <= 0)
    {
        return 0;
    }

    if (timeout_ms < 0)
    {
        timeout_ms = 300;
    }

    return net_discover_lan_servers_udp(hosts, max_hosts, port, timeout_ms);
}

static long net_elapsed_ms_since(const struct timeval *start)
{
    struct timeval now;
    net_get_time(&now);
    long sec = now.tv_sec - start->tv_sec;
    long usec = now.tv_usec - start->tv_usec;
    return sec * 1000L + usec / 1000L;
}

static int net_discover_lan_servers_udp(char hosts[][64], int max_hosts, int port, int timeout_ms)
{
    if (max_hosts <= 0)
    {
        return 0;
    }

    if (net_ensure_platform_initialized() != 0)
    {
        return 0;
    }

    net_socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == NET_INVALID_SOCKET)
    {
        return 0;
    }

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    const char *targets[] = {
        "255.255.255.255",
        "192.168.0.255",
        "192.168.1.255",
        "10.0.0.255",
        "127.0.0.1"};
    const size_t target_count = sizeof(targets) / sizeof(targets[0]);
    char payload[64];
    snprintf(payload, sizeof(payload), "%s %d", ARMADA_DISCOVERY_REQUEST, port);

    for (size_t i = 0; i < target_count; ++i)
    {
        if (inet_pton(AF_INET, targets[i], &addr.sin_addr) <= 0)
        {
            continue;
        }
        sendto(sock, payload, strlen(payload), 0, (struct sockaddr *)&addr, sizeof(addr));
    }

    int found = 0;
    struct timeval start;
    net_get_time(&start);

    while (found < max_hosts)
    {
        long elapsed = net_elapsed_ms_since(&start);
        if (elapsed >= timeout_ms)
        {
            break;
        }

        long remaining = timeout_ms - elapsed;
        struct timeval wait;
        wait.tv_sec = remaining / 1000;
        wait.tv_usec = (remaining % 1000) * 1000;
        if (wait.tv_sec == 0 && wait.tv_usec == 0)
        {
            wait.tv_usec = 1000;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

#if defined(_WIN32)
        int ready = select(0, &readfds, NULL, NULL, &wait);
#else
        int ready = select((int)(sock + 1), &readfds, NULL, NULL, &wait);
#endif
        if (ready < 0)
        {
            int last_error = NET_ERRNO();
            if (last_error == NET_EINTR)
            {
                continue;
            }
            break;
        }
        if (ready == 0)
        {
            continue;
        }

        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        char buffer[128];
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&from, &from_len);
        if (len <= 0)
        {
            if (len < 0)
            {
                int last_error = NET_ERRNO();
                if (last_error == NET_EINTR)
                {
                    continue;
                }
            }
            break;
        }

        buffer[len] = '\0';
        if (strncmp(buffer, ARMADA_DISCOVERY_RESPONSE, strlen(ARMADA_DISCOVERY_RESPONSE)) != 0)
        {
            continue;
        }

        char address[64];
        if (!inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address)))
        {
            continue;
        }

        if (net_host_list_contains(hosts, found, address))
        {
            continue;
        }

        strncpy(hosts[found], address, 63);
        hosts[found][63] = '\0';
        ++found;
    }

    net_close_socket(sock);
    return found;
}
