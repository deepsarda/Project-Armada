#ifndef NETWORK_H
#define NETWORK_H

#include "../common/events.h"
#include "net_platform.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DEFAULT_PORT 8080
#define ARMADA_DISCOVERY_REQUEST "ARMADA_DISCOVER_V1"
#define ARMADA_DISCOVERY_RESPONSE "ARMADA_SERVER_V1"

    // Basic socket operations
    net_socket_t net_create_server_socket(int port);
    net_socket_t net_connect_to_server(const char *host, int port);
    void net_close_socket(net_socket_t sock);

    // Data transmission
    // Returns 1 on success, 0 on failure/disconnect
    int net_send_event(net_socket_t sock, const GameEvent *event);
    int net_receive_event(net_socket_t sock, GameEvent *event);
    int net_receive_event_flags(net_socket_t sock, GameEvent *event, int flags);
    // Returns 1 on success, 0 on timeout, -1 on error/disconnect
    int net_receive_event_timeout(net_socket_t sock, GameEvent *event, int timeout_ms);

    // Discovery helpers
    int net_discover_lan_servers(char hosts[][64], int max_hosts, int port, int timeout_ms);

    // Logging helpers
    void net_log_socket_error(const char *context);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_H
