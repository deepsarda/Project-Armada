#include "../../include/networking/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

/**
 * Create a TCP server socket bound to the given port.
 * Returns the socket file descriptor on success, -1 on failure.
 */
int net_create_server_socket(int port)
{
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        return -1;
    }

    // Allow address reuse
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    // Set address info
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind socket to address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    // Start listening
    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

/**
 * Connect to a TCP server at the given host and port.
 * Returns the socket file descriptor on success, -1 on failure.
 */
int net_connect_to_server(const char *host, int port)
{
    int sock = 0;
    struct sockaddr_in serv_addr;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert host address to binary form
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0)
    {
        // Handle "localhost" manually
        if (strcmp(host, "localhost") == 0)
        {
            if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
            {
                perror("Invalid address/ Address not supported");
                close(sock);
                return -1;
            }
        }
        else
        {
            perror("Invalid address/ Address not supported");
            close(sock);
            return -1;
        }
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        close(sock);
        return -1;
    }

    return sock;
}

/**
 * Close a socket if valid.
 */
void net_close_socket(int sock)
{
    if (sock >= 0)
    {
        close(sock);
    }
}

/**
 * Send a GameEvent struct over the socket.
 * Returns 1 on success, 0 on failure.
 */
int net_send_event(int sock, const GameEvent *event)
{
    if (sock < 0)
        return 0;
    ssize_t sent = send(sock, event, sizeof(GameEvent), 0);
    if (sent != sizeof(GameEvent))
    {
        perror("send");
        return 0;
    }
    return 1;
}

/**
 * Receive a GameEvent struct from the socket with flags.
 * Returns 1 on success, 0 if no data (non-blocking), -1 on error/disconnect.
 */
int net_receive_event_flags(int sock, GameEvent *event, int flags)
{
    if (sock < 0)
        return -1;

    ssize_t valread = recv(sock, event, sizeof(GameEvent), flags);
    if (valread == 0)
    {
        return -1; // disconnected
    }

    if (valread < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            return 0; // no data available for non-blocking polls
        }
        perror("recv");
        return -1;
    }

    if (valread != (ssize_t)sizeof(GameEvent))
    {
        fprintf(stderr, "Warning: Partial event read %zd/%lu\n", valread, sizeof(GameEvent));
        return -1;
    }

    return 1;
}

/**
 * Receive a GameEvent struct from the socket (blocking).
 * Returns 1 on success, 0 on error/disconnect.
 */
int net_receive_event(int sock, GameEvent *event)
{
    int result = net_receive_event_flags(sock, event, 0);
    return result > 0 ? 1 : 0;
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
    gettimeofday(&now, NULL);
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

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return 0;
    }

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

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
    gettimeofday(&start, NULL);

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

        int ready = select(sock + 1, &readfds, NULL, NULL, &wait);
        if (ready < 0)
        {
            if (errno == EINTR)
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
            if (len < 0 && errno == EINTR)
            {
                continue;
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

    close(sock);
    return found;
}
