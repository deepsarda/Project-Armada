#ifndef NETWORK_H
#define NETWORK_H

#include "../common/events.h"
#include <stddef.h>

#define DEFAULT_PORT 8080

// Basic socket operations
int net_create_server_socket(int port);
int net_connect_to_server(const char *host, int port);
void net_close_socket(int sock);

// Data transmission
// Returns 1 on success, 0 on failure/disconnect
int net_send_event(int sock, const GameEvent *event);
int net_receive_event(int sock, GameEvent *event);
int net_receive_event_flags(int sock, GameEvent *event, int flags);

// Discovery helpers
int net_probe_server(const char *host, int port, int timeout_ms);
int net_discover_lan_servers(char hosts[][64], int max_hosts, int port, int timeout_ms);

#endif // NETWORK_H
