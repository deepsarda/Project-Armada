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
#include <fcntl.h>
#include <sys/select.h>

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

/**
 * Probe if a TCP server is running at host:port with timeout.
 * Returns 1 if server is reachable, 0 otherwise.
 */
int net_probe_server(const char *host, int port, int timeout_ms)
{
    if (!host)
    {
        return 0;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return 0;
    }

    // Set socket to non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1)
    {
        close(sock);
        return 0;
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        close(sock);
        return 0;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0)
    {
        close(sock);
        return 0;
    }

    // Try to connect
    int result = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (result == 0)
    {
        close(sock);
        return 1;
    }

    // If connection is in progress, wait for it
    if (errno != EINPROGRESS)
    {
        close(sock);
        return 0;
    }

    if (timeout_ms < 0)
    {
        timeout_ms = 300;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    result = select(sock + 1, NULL, &writefds, NULL, &tv);
    if (result <= 0)
    {
        close(sock);
        return 0;
    }

    // Check for socket errors
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0)
    {
        close(sock);
        return 0;
    }

    close(sock);
    return 1;
}

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

    int found = 0;
    const char *seed_hosts[] = {"127.0.0.1", "localhost"};
    const size_t seed_count = sizeof(seed_hosts) / sizeof(seed_hosts[0]);

    // Probe seed hosts
    for (size_t i = 0; i < seed_count && found < max_hosts; ++i)
    {
        if (net_probe_server(seed_hosts[i], port, timeout_ms))
        {
            const char *resolved = strcmp(seed_hosts[i], "localhost") == 0 ? "127.0.0.1" : seed_hosts[i];
            if (!net_host_list_contains(hosts, found, resolved))
            {
                strncpy(hosts[found], resolved, 63);
                hosts[found][63] = '\0';
                ++found;
            }
        }
    }

    // Probe common subnets
    const char *subnets[] = {"192.168.1.", "192.168.0.", "10.0.0."};
    const size_t subnet_count = sizeof(subnets) / sizeof(subnets[0]);
    const int hosts_per_subnet = 20;
    char candidate[64];

    for (size_t s = 0; s < subnet_count && found < max_hosts; ++s)
    {
        for (int host = 1; host <= hosts_per_subnet && found < max_hosts; ++host)
        {
            snprintf(candidate, sizeof(candidate), "%s%d", subnets[s], host);
            if (net_host_list_contains(hosts, found, candidate))
            {
                continue;
            }
            if (net_probe_server(candidate, port, timeout_ms))
            {
                strncpy(hosts[found], candidate, 63);
                hosts[found][63] = '\0';
                ++found;
            }
        }
    }

    return found;
}
