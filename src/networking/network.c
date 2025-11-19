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

int net_create_server_socket(int port)
{
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int net_connect_to_server(const char *host, int port)
{
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0)
    {
        // Try to handle "localhost" manually if inet_pton fails or just assume 127.0.0.1 for localhost
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

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        close(sock);
        return -1;
    }

    return sock;
}

void net_close_socket(int sock)
{
    if (sock >= 0)
    {
        close(sock);
    }
}

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

int net_receive_event(int sock, GameEvent *event)
{
    if (sock < 0)
        return 0;
    ssize_t valread = read(sock, event, sizeof(GameEvent));
    if (valread <= 0)
    {
        // 0 means disconnected, -1 means error
        return 0;
    }
    if (valread != sizeof(GameEvent))
    {
        // NOTE: we assume struct is small enough to arrive in one packet or we fail.
        // TODO: Implement partial reading.
        fprintf(stderr, "Warning: Partial event read %zd/%lu\n", valread, sizeof(GameEvent));
        return 0;
    }
    return 1;
}
