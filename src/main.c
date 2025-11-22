#include "../include/client/client_api.h"
#include "../include/server/server_api.h"
#include "../include/networking/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#define MAX_DISCOVERED_SERVERS 32
#define INPUT_BUFFER 128

static void read_line(char *buffer, size_t size);
static int read_int_choice(void);
static int stdin_has_input(void);
static void run_server_flow(void);
static void run_manual_join(void);
static void run_scan_join(void);
static void run_client_flow(const char *address);

int main(void)
{
    printf("=== Project Armada Launcher ===\n");
    printf("Use this console to host or join LAN matches.\n\n");

    for (;;)
    {
        printf("Menu:\n");
        printf(" 1) Host a new server\n");
        printf(" 2) Join via LAN scan\n");
        printf(" 3) Join via manual IP\n");
        printf(" 4) Quit\n> ");
        fflush(stdout);

        int choice = read_int_choice();
        switch (choice)
        {
        case 1:
            run_server_flow();
            break;
        case 2:
            run_scan_join();
            break;
        case 3:
            run_manual_join();
            break;
        case 4:
            printf("Goodbye!\n");
            return 0;
        default:
            printf("Unknown option. Please try again.\n\n");
            break;
        }
    }
}

static void run_server_flow(void)
{
    ServerContext *server = server_create();
    if (!server)
    {
        fprintf(stderr, "Failed to allocate server context.\n");
        return;
    }

    if (server_init(server, MAX_PLAYERS) != 0)
    {
        fprintf(stderr, "Server initialization failed.\n");
        server_destroy(server);
        return;
    }

    server_start(server);
    if (!server->running)
    {
        fprintf(stderr, "Server failed to start.\n");
        server_destroy(server);
        return;
    }
    printf("Server listening on port %d. Press ENTER to stop.\n", DEFAULT_PORT);
    fflush(stdout);

    while (server->running)
    {
        if (stdin_has_input())
        {
            char buffer[INPUT_BUFFER];
            read_line(buffer, sizeof(buffer));
            break;
        }
        usleep(100000);
    }

    server_stop(server);
    server_destroy(server);
    printf("Server stopped.\n\n");
}

static void run_manual_join(void)
{
    char address[INPUT_BUFFER];
    printf("Enter server IP (blank to cancel): ");
    fflush(stdout);
    read_line(address, sizeof(address));
    if (address[0] == '\0')
    {
        printf("Manual join canceled.\n\n");
        return;
    }
    run_client_flow(address);
}

static void run_scan_join(void)
{
    char hosts[MAX_DISCOVERED_SERVERS][64];
    int found = net_discover_lan_servers(hosts, MAX_DISCOVERED_SERVERS, DEFAULT_PORT, 200);
    if (found <= 0)
    {
        printf("No servers discovered on the LAN.\n\n");
        return;
    }

    printf("Discovered servers:\n");
    for (int i = 0; i < found; ++i)
    {
        printf(" %d) %s\n", i + 1, hosts[i]);
    }
    printf("Select server number (0 to cancel): ");
    fflush(stdout);
    int choice = read_int_choice();
    if (choice <= 0 || choice > found)
    {
        printf("Scan join canceled.\n\n");
        return;
    }

    run_client_flow(hosts[choice - 1]);
}

static void run_client_flow(const char *address)
{
    if (!address)
        return;

    char name[32];
    printf("Enter player name (default Voyager): ");
    fflush(stdout);
    read_line(name, sizeof(name));
    if (name[0] == '\0')
    {
        strncpy(name, "Voyager", sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    ClientContext *client = client_create(name);
    if (!client)
    {
        fprintf(stderr, "Failed to allocate client context.\n\n");
        return;
    }

    if (client_connect(client, address) != 0)
    {
        fprintf(stderr, "Unable to connect to %s:%d.\n\n", address, DEFAULT_PORT);
        client_destroy(client);
        return;
    }

    printf("Connected to %s. Type 'quit' + ENTER to disconnect, 'end' to end your turn, or 'start' to begin the match if you are the host.\n", address);

    char input[INPUT_BUFFER];
    while (client->connected)
    {
        client_pump(client);

        if (stdin_has_input())
        {
            read_line(input, sizeof(input));
            if (strcmp(input, "quit") == 0 || input[0] == '\0')
            {
                break;
            }
            else if (strcmp(input, "end") == 0)
            {
                client_send_action(client, USER_ACTION_END_TURN, -1, 0, 0);
            }
            else if (strcmp(input, "start") == 0)
            {
                if (!client->is_host)
                {
                    printf("Only the lobby host can start the match.\n");
                }
                else
                {
                    client_request_match_start(client);
                }
            }
            else
            {
                printf("Unknown command. Type 'end', 'start', or 'quit'.\n");
            }
        }

        usleep(50000);
    }

    client_disconnect(client);
    client_destroy(client);
    printf("Disconnected from server.\n\n");
}

static void read_line(char *buffer, size_t size)
{
    if (!buffer || size == 0)
    {
        return;
    }

    if (!fgets(buffer, (int)size, stdin))
    {
        buffer[0] = '\0';
        clearerr(stdin);
        return;
    }

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
    {
        buffer[len - 1] = '\0';
    }
}

static int read_int_choice(void)
{
    char buffer[INPUT_BUFFER];
    read_line(buffer, sizeof(buffer));
    if (buffer[0] == '\0')
        return -1;
    return (int)strtol(buffer, NULL, 10);
}

static int stdin_has_input(void)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    return ready > 0 && FD_ISSET(STDIN_FILENO, &readfds);
}
