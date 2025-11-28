# Part 1: Networking & Server Core Documentation

## 1. File: `src/networking/network.c`
This file implements the low-level transport layer, abstracting differences between Windows (Winsock) and POSIX (Linux/macOS) sockets.

### Platform Internals

#### `net_ensure_platform_initialized`
*   **Signature:** `static int net_ensure_platform_initialized(void)`
*   **Purpose:** Initializes the underlying socket library if it hasn't been initialized yet.
*   **Logic:**
    *   Checks a static flag `net_platform_initialized`.
    *   **Windows:** Calls `WSAStartup` to load the Winsock DLL. Registers `net_platform_cleanup` with `atexit()`.
    *   **POSIX:** Does nothing (sockets are kernel-level).
*   **Returns:** `0` on success, `-1` on failure (e.g., WSAStartup failed).

#### `net_platform_cleanup`
*   **Signature:** `static void net_platform_cleanup(void)`
*   **Purpose:** Cleans up platform resources on program exit.
*   **Logic:** Checks if initialized, then calls `WSACleanup()` (Windows only).

#### `net_set_blocking`
*   **Signature:** `static int net_set_blocking(net_socket_t sock, int should_block)`
*   **Purpose:** Toggles a specific socket between blocking and non-blocking mode.
*   **Parameters:**
    *   `sock`: The socket file descriptor.
    *   `should_block`: `1` for blocking, `0` for non-blocking.
*   **Logic:**
    *   **Windows:** Uses `ioctlsocket` with the `FIONBIO` command.
    *   **POSIX:** (Implementation stubbed in provided code as 0, need to implement `fcntl` with `O_NONBLOCK`).
*   **Returns:** `0` on success, non-zero on error.

#### `net_get_time`
*   **Signature:** `static void net_get_time(struct timeval *tv)`
*   **Purpose:** Gets the current system time with microsecond precision.
*   **Logic:**
    *   **Windows:** `GetSystemTimeAsFileTime` (converts 100ns ticks since 1601 to Unix Epoch).
    *   **POSIX:** Calls `gettimeofday`.

#### `net_log_socket_error`
*   **Signature:** `void net_log_socket_error(const char *context)`
*   **Purpose:** Prints the last socket error to `stderr`.
*   **Logic:**
    *   **Windows:** Calls `WSAGetLastError()` and prints the error code.
    *   **POSIX:** Calls `perror()` using `errno`.

### TCP Socket Operations

#### `net_create_server_socket`
*   **Signature:** `net_socket_t net_create_server_socket(int port)`
*   **Purpose:** Creates a listening TCP socket for the server.
*   **Logic:**
    1.  Calls `socket()` (AF_INET, SOCK_STREAM).
    2.  Sets `SO_REUSEADDR` to allow immediate restart on the same port.
    3.  `bind()` the socket to `INADDR_ANY` and the specified `port`.
    4.  `listen()` for incoming connections (backlog of 3).
*   **Returns:** Valid socket FD or `NET_INVALID_SOCKET` on failure.

#### `net_connect_to_server`
*   **Signature:** `net_socket_t net_connect_to_server(const char *host, int port)`
*   **Purpose:** Creates a client socket and connects to a remote server.
*   **Logic:**
    1.  Creates a TCP socket.
    2.  Resolves `host` string to an IP address using `inet_pton`. Handles "localhost" manually.
    3.  Calls `connect()`.
*   **Returns:** Valid socket FD or `NET_INVALID_SOCKET` on failure.

#### `net_close_socket`
*   **Signature:** `void net_close_socket(net_socket_t sock)`
*   **Purpose:** Closes a socket handle safely.
*   **Logic:** Wraps `closesocket` (Windows) or `close` (POSIX).

### Event Transmission

#### `net_send_event`
*   **Signature:** `int net_send_event(net_socket_t sock, const GameEvent *event)`
*   **Purpose:** Sends a `GameEvent` struct over the network.
*   **Logic:** calls `send()` with the size of the struct. Checks if all bytes were sent.
*   **Returns:** `1` on success, `0` on failure.

#### `net_receive_event`
*   **Signature:** `int net_receive_event(net_socket_t sock, GameEvent *event)`
*   **Purpose:** Blocking wait for an event.
*   **Logic:** Wraps `net_receive_event_flags` with flag `0`.
*   **Returns:** `1` success, `0` disconnect.

#### `net_receive_event_flags`
*   **Signature:** `int net_receive_event_flags(net_socket_t sock, GameEvent *event, int flags)`
*   **Purpose:** Reads a `GameEvent` from the socket with specific behavior (e.g., non-blocking).
*   **Parameters:** `flags` usually contains `NET_MSG_DONTWAIT` for non-blocking.
*   **Logic:**
    1.  If on Windows and non-blocking is requested, manually sets socket to non-blocking mode using `ioctlsocket`.
    2.  Calls `recv()`.
    3.  If on Windows, toggles socket back to blocking mode immediately after.
    4.  Handles `EWOULDBLOCK` / `EAGAIN` gracefully (returns 0).
    5.  Handles `0` bytes read (returns -1 for disconnect).
*   **Returns:** `1` (Data Read), `0` (No Data/Would Block), `-1` (Error/Disconnect).

### LAN Discovery (UDP)

#### `net_discover_lan_servers`
*   **Signature:** `int net_discover_lan_servers(char hosts[][64], int max_hosts, int port, int timeout_ms)`
*   **Purpose:** Public API to search for local servers.
*   **Logic:** Validates inputs and calls the internal UDP implementation.
*   **Returns:** Count of servers found.

#### `net_discover_lan_servers_udp`
*   **Signature:** `static int net_discover_lan_servers_udp(...)`
*   **Purpose:** Performs the actual UDP broadcast and listening.
*   **Logic:**
    1.  Creates a UDP socket (`SOCK_DGRAM`) and enables `SO_BROADCAST`.
    2.  Sends packet `ARMADA_DISCOVER_V1` to a list of broadcast IPs (255.255.255.255, 192.168.0.255, etc.).
    3.  Enters a `while` loop with `select()` to wait for responses until `timeout_ms` expires.
    4.  Parses responses looking for `ARMADA_SERVER_V1`.
    5.  Adds unique IPs to the `hosts` list.
*   **Returns:** Number of unique servers found.

#### `net_host_list_contains`
*   **Signature:** `static int net_host_list_contains(char hosts[][64], int count, const char *candidate)`
*   **Purpose:** Helper to prevent duplicate IPs in the discovery list.
*   **Returns:** `1` if found, `0` otherwise.

#### `net_elapsed_ms_since`
*   **Signature:** `static long net_elapsed_ms_since(const struct timeval *start)`
*   **Purpose:** Calculates time delta in milliseconds.


## 2. File: `src/networking/server.c`
This file implements the authoritative server logic, state machine, and threading model.

### Lifecycle & Management

#### `server_create`
*   **Signature:** `ServerContext *server_create()`
*   **Purpose:** Allocates memory for a new server instance.
*   **Logic:** `malloc`s `ServerContext`, zeroes memory, sets sockets to INVALID, initializes the `state_mutex`.
*   **Returns:** Pointer to new context.

#### `server_destroy`
*   **Signature:** `void server_destroy(ServerContext *ctx)`
*   **Purpose:** Frees resources.
*   **Logic:** Calls `server_stop` (if running), destroys mutex, frees memory.

#### `server_init`
*   **Signature:** `int server_init(ServerContext *ctx, int max_players)`
*   **Purpose:** Prepares the server state for a new game session.
*   **Logic:** Locks mutex, resets `GameState` struct, sets turn counters to 0, sets Host ID to -1. Calls callback `server_on_init`.

#### `server_start`
*   **Signature:** `void server_start(ServerContext *ctx)`
*   **Purpose:** Spins up the server.
*   **Logic:**
    1.  Calls `net_create_server_socket`.
    2.  Sets `running = 1`.
    3.  Calls `server_start_discovery_service` (UDP).
    4.  Spawns `server_accept_thread` (pthread).

#### `server_stop`
*   **Signature:** `void server_stop(ServerContext *ctx)`
*   **Purpose:** Shuts down the server.
*   **Logic:**
    1.  Sets `running = 0`.
    2.  Stops discovery service.
    3.  Closes server socket.
    4.  Joins `accept_thread`.
    5.  Loops through all connected players, closes their sockets, and marks them inactive.

### Thread Entry Points

#### `server_accept_thread`
*   **Signature:** `static void *server_accept_thread(void *arg)`
*   **Purpose:** Listens for new TCP connections.
*   **Logic:**
    *   Loops while `ctx->running`.
    *   Calls `accept()` (blocking).
    *   On connection: Creates `ClientThreadArgs`, spawns a new detached `server_client_thread`.

#### `server_client_thread`
*   **Signature:** `static void *server_client_thread(void *arg)`
*   **Purpose:** Dedicated thread for a specific player connection.
*   **Logic:**
    *   Loops calling `net_receive_event` (blocking).
    *   On data: Checks packet type.
        *   If `EVENT_PLAYER_JOIN_REQUEST`: Calls `server_handle_player_join`.
        *   Else: Calls `server_handle_event`.
    *   On disconnect: Calls `server_handle_disconnect`.

#### `server_discovery_thread`
*   **Signature:** `static void *server_discovery_thread(void *arg)`
*   **Purpose:** Responds to LAN discovery broadcasts.
*   **Logic:**
    *   Loops `recvfrom` on the UDP socket.
    *   If packet matches `ARMADA_DISCOVERY_REQUEST`, sends back `ARMADA_DISCOVERY_RESPONSE` with port and player count.

### Discovery Service
#### `server_start_discovery_service`
*   **Signature:** `static int server_start_discovery_service(ServerContext *ctx)`
*   **Purpose:** Sets up UDP socket and spawns thread.

#### `server_stop_discovery_service`
*   **Signature:** `static void server_stop_discovery_service(ServerContext *ctx)`
*   **Purpose:** Cleans up UDP socket and thread.

### Event Handlers (Logic)

#### `server_handle_event`
*   **Signature:** `static void server_handle_event(ServerContext *ctx, const GameEvent *event)`
*   **Purpose:** Route incoming packets.
*   **Logic:** Switch statement on `event->type`. Routes `EVENT_USER_ACTION` and `EVENT_MATCH_START_REQUEST` to specific functions.

#### `server_handle_player_join`
*   **Signature:** `static void server_handle_player_join(ServerContext *ctx, net_socket_t sender_socket, ...)`
*   **Purpose:** Processes a join request.
*   **Logic:**
    1.  Locks mutex.
    2.  Finds open slot via `server_find_open_slot`.
    3.  If full: sends ACK with failure.
    4.  If open:
        *   Initializes `PlayerState`.
        *   Assigns socket to slot.
        *   Selects host if none exists.
        *   Sends ACK with success.
        *   Broadcasts `EVENT_PLAYER_JOINED` to all other players.

#### `server_handle_user_action`
*   **Signature:** `static void server_handle_user_action(ServerContext *ctx, const EventPayload_UserAction *payload)`
*   **Purpose:** Executes gameplay moves.
*   **Logic:**
    1.  Validates `current_player_id` matches sender.
    2.  Calls `server_on_turn_action` (game rule logic).
    3.  Checks for Game Over (Victory or Star Goal).
    4.  Checks for Star Threshold (900 stars).
    5.  If Game Over: Broadcasts `EVENT_GAME_OVER`.
    6.  Else: Calls `server_advance_turn`.

#### `server_handle_match_start_request`
*   **Signature:** `static void server_handle_match_start_request(ServerContext *ctx, int requester_id)`
*   **Purpose:** Host attempts to start game.
*   **Logic:** Validates requester is Host, match not started, and player count >= 2. Calls `server_start_match`.

#### `server_handle_disconnect`
*   **Signature:** `static void server_handle_disconnect(ServerContext *ctx, net_socket_t socket_fd)`
*   **Purpose:** Clean up after a player drops.
*   **Logic:**
    1.  Finds player ID from socket.
    2.  Marks `is_active = 0`.
    3.  Reassigns host if the disconnected player was host.
    4.  Broadcasts `EVENT_PLAYER_LEFT`.
    5.  If it was their turn, calls `server_advance_turn`.

### State Management Helpers

#### `server_broadcast_event`
*   **Signature:** `static void server_broadcast_event(ServerContext *ctx, const GameEvent *event)`
*   **Purpose:** Loops through `player_sockets` and sends the event to every valid socket.

#### `server_send_event_to`
*   **Signature:** `static void server_send_event_to(ServerContext *ctx, int player_id, const GameEvent *event)`
*   **Purpose:** Sends event to a specific player ID.

#### `server_collect_active_players`
*   **Signature:** `static int server_collect_active_players(ServerContext *ctx, int *out_ids, int max_ids)`
*   **Purpose:** Fills an array with IDs of currently active players.

#### `server_build_player_snapshot`
*   **Signature:** `static int server_build_player_snapshot(ServerContext *ctx, int viewer_id, PlayerGameState *out_state)`
*   **Purpose:** **Fog of War Logic.** Creates a view of the game specifically for `viewer_id`.
*   **Logic:**
    *   Copies `viewer_id`'s full state to `out_state->self`.
    *   Iterates other players:
        *   Copies Ship/Planet Levels (Public info).
        *   **Hides** exact HP (converts to 0-100% blocks via `to_coarse_percent`).
        *   **Hides** Stars (only sets `show_stars` if > threshold).

#### `server_get_player`
*   **Signature:** `static PlayerState *server_get_player(ServerContext *ctx, int player_id)`
*   **Purpose:** Safe accessor for the player array.

#### `server_find_open_slot`
*   **Signature:** `static int server_find_open_slot(ServerContext *ctx)`
*   **Purpose:** Finds the first index in `players[]` where `is_active` is false.

#### `server_find_player_by_socket`
*   **Signature:** `static int server_find_player_by_socket(ServerContext *ctx, net_socket_t socket_fd)`
*   **Purpose:** Reverse lookup (Socket -> ID).

#### `server_reset_player`
*   **Signature:** `static void server_reset_player(PlayerState *player, int player_id, const char *name)`
*   **Purpose:** Initializes a `PlayerState` struct with default game values (100 HP, 100 Stars, Level 1).

#### `server_refresh_player_count`
*   **Signature:** `static void server_refresh_player_count(ServerContext *ctx)`
*   **Purpose:** Updates `ctx->game_state.player_count` by counting active flags.

#### `server_start_match`
*   **Signature:** `static void server_start_match(ServerContext *ctx)`
*   **Purpose:** Transitions server from Lobby to Game mode.
*   **Logic:**
    *   Sets `match_started = 1`.
    *   Sets turn to 1.
    *   Broadcasts `EVENT_MATCH_START`.
    *   Broadcasts the first turn via `server_broadcast_current_turn`.

#### `server_compute_valid_actions`
*   **Signature:** `static int server_compute_valid_actions(ServerContext *ctx, int player_id, int current_player_id)`
*   **Purpose:** Determines which buttons should be enabled on the client UI.
*   **Logic:**
    *   If `player_id != current_player_id`: No actions allowed.
    *   Checks logic: e.g., can only Attack if enemies exist; can only Repair if damaged.
*   **Returns:** Bitmask integer (`VALID_ACTION_ATTACK_PLANET | ...`).

#### `server_emit_turn_event`
*   **Signature:** `static void server_emit_turn_event(...)`
*   **Purpose:** Sends `EVENT_TURN_STARTED` to everyone.
*   **Logic:**
    *   Calls `server_build_player_snapshot` for *each* player to ensure they only see their specific Fog-of-War view.
    *   Sends unique packets to each player.

#### `server_broadcast_current_turn`
*   **Signature:** `static void server_broadcast_current_turn(...)`
*   **Purpose:** Helper wrapper that gathers current ID, next ID, and calls `server_emit_turn_event`.

#### `server_advance_turn`
*   **Signature:** `static void server_advance_turn(ServerContext *ctx, const EventPayload_UserAction *last_action)`
*   **Purpose:** Moves game forward.
*   **Logic:**
    1.  Calculates next player ID via `server_next_active_player`.
    2.  **Income Logic:** Calculates income for the *new* player based on their Planet Health % and Base Income. Adds to their Stars.
    3.  Increments Turn Number.
    4.  Broadcasts new turn state.

#### `server_next_active_player`
*   **Signature:** `static int server_next_active_player(ServerContext *ctx, int start_after)`
*   **Purpose:** Finds the next `is_active` player index, wrapping around the array.

#### `server_emit_threshold_event`
*   **Signature:** `static void server_emit_threshold_event(ServerContext *ctx, int player_id)`
*   **Purpose:** Broadcasts that `player_id` has crossed the star threshold.

#### `server_emit_host_update`
*   **Signature:** `static void server_emit_host_update(ServerContext *ctx, int host_id, const char *host_name)`
*   **Purpose:** Broadcasts new host info.

#### `server_send_error_event`
*   **Signature:** `static void server_send_error_event(ServerContext *ctx, int player_id, int error_code, const char *message)`
*   **Purpose:** Sends error feedback (e.g., "Not enough players").

#### `server_select_host_locked`
*   **Signature:** `static int server_select_host_locked(ServerContext *ctx)`
*   **Purpose:** Determines who acts as lobby host.
*   **Logic:** If current host is invalid/left, picks the first active player in the list.

#### `to_coarse_percent`
*   **Signature:** `static int to_coarse_percent(int current, int max)`
*   **Purpose:** Fog of War helper. Rounds values to 0, 25, 50, 75, 100.

#### `clamp_int`
*   **Signature:** `static int clamp_int(int value, int min, int max)`
*   **Purpose:** Helper to constrain values.


# Part 2: Client & Application Logic Documentation

## 3. File: `src/networking/client.c`
This file implements the client-side state machine, event polling, and networking.

### Lifecycle Management

#### `client_create`
*   **Signature:** `ClientContext *client_create(const char *name)`
*   **Purpose:** Allocates and initializes a new client instance.
*   **Logic:** `malloc`s `ClientContext`, zeroes memory, and calls `client_init`.
*   **Returns:** Pointer to context or `NULL`.

#### `client_destroy`
*   **Signature:** `void client_destroy(ClientContext *ctx)`
*   **Purpose:** Cleanup.
*   **Logic:** Calls `client_disconnect` (if connected) and frees memory.

#### `client_init`
*   **Signature:** `int client_init(ClientContext *ctx, const char *player_name)`
*   **Purpose:** Resets client state and sets the player name.
*   **Logic:**
    *   Copies `player_name` into the context. Defaults to "Player" if null.
    *   Resets flags (`connected`, `is_host`, `match_started`).
    *   Sets socket to `NET_INVALID_SOCKET`.
    *   Calls `client_on_init` callback.

#### `client_connect`
*   **Signature:** `int client_connect(ClientContext *ctx, const char *server_addr)`
*   **Purpose:** Establishes TCP connection and initiates the protocol.
*   **Logic:**
    1.  Calls `net_connect_to_server`.
    2.  If successful, sets `connected = 1`.
    3.  **Immediately** constructs and sends an `EVENT_PLAYER_JOIN_REQUEST` packet containing the player name.
    4.  Calls `client_on_join_request` callback.
*   **Returns:** `0` on success, `-1` on failure.

#### `client_disconnect`
*   **Signature:** `void client_disconnect(ClientContext *ctx)`
*   **Purpose:** Closes connection.
*   **Logic:**
    *   Closes the socket.
    *   Resets `connected`, `is_host`, `host_player_id`.
    *   Calls `client_on_disconnected`.

### Event Loop

#### `client_pump`
*   **Signature:** `void client_pump(ClientContext *ctx)`
*   **Purpose:** The heartbeat of the client. Checks for incoming data.
*   **Logic:**
    1.  Calls `net_receive_event_flags` with `NET_MSG_DONTWAIT` (Non-blocking).
    2.  If result is `0` (No data), returns immediately.
    3.  If result is `< 0` (Error/Disconnect), handles cleanup and calls `client_on_disconnected`.
    4.  If result is `1` (Data), passes the event to `client_handle_event`.

#### `client_handle_event`
*   **Signature:** `static void client_handle_event(ClientContext *ctx, const GameEvent *event)`
*   **Purpose:** Dispatches received events to specific state updates and callbacks.
*   **Logic:** Switches on `event->type`:
    *   `EVENT_PLAYER_JOIN_ACK`: Updates `player_id`, `host_player_id`, `is_host`.
    *   `EVENT_HOST_UPDATED`: Updates host tracking variables.
    *   `EVENT_MATCH_START`: Sets `match_started = 1`.
    *   `EVENT_TURN_STARTED`: Updates `player_game_state`, `current_turn_player_id`, `valid_actions`, and `turn_number`.
    *   `EVENT_GAME_OVER`: Resets `match_started`.
    *   Invokes the corresponding `client_on_*` callback for every event.

### Action Sending

#### `client_send_action`
*   **Signature:** `void client_send_action(ClientContext *ctx, UserActionType action_type, int target_player_id, int value, int metadata)`
*   **Purpose:** Constructs and sends a gameplay command.
*   **Logic:** Populates an `EventPayload_UserAction` inside a `GameEvent` and sends it via TCP.

#### `client_request_match_start`
*   **Signature:** `void client_request_match_start(ClientContext *ctx)`
*   **Purpose:** Sends request to begin game (Host only).
*   **Logic:** Sends `EVENT_MATCH_START_REQUEST`.


## 4. File: `src/server/main.c`
This file implements the **Server Callbacks**. These functions are called by the core server logic (`server.c`) to allow the application layer to log events or implement custom game rules.

#### `server_on_init`
*   **Purpose:** Hook called before server initialization. Returns 0.

#### `server_on_initialized`
*   **Purpose:** Logs "Initialized for up to X players."

#### `server_on_starting`
*   **Purpose:** Logs "Starting server on port...".

#### `server_on_start_failed`
*   **Purpose:** Logs failure messages.

#### `server_on_started`
*   **Purpose:** Logs "Server listening on port...".

#### `server_on_accept_thread_started` / `_failed`
*   **Purpose:** Logs thread lifecycle events.

#### `server_on_stopping`
*   **Purpose:** Logs "Stopping server...".

#### `server_on_client_connected`
*   **Purpose:** Logs when a new socket connects (before Join Request).

#### `server_on_client_disconnected`
*   **Purpose:** Logs when a socket disconnects.

#### `server_on_unhandled_event`
*   **Purpose:** Logs warning for unknown event types.

#### `server_on_unknown_action`
*   **Purpose:** Logs warning for unknown user action types.

#### `server_on_turn_action`
*   **Signature:** `void server_on_turn_action(ServerContext *ctx, const EventPayload_UserAction *action, ServerActionResult *result)`
*   **Purpose:** **Game Logic Hook.**
*   **Current Logic:**
    *   Copies input `action` to `result->applied_action`.
    *   Logs the action details (Player X did Y to Z).
    *   *TODO:*  function needes modify `ctx->game_state` (deduct stars, reduce HP) and set `result->game_over`.


## 5. File: `src/client/ui_notifications.cpp`
This file acts as a C++ bridge, allowing C code to send logs safely to the C++ UI thread.

#### `armada_ui_set_log_sink` / `armada_server_set_log_sink`
*   **Signature:** `void armada_ui_set_log_sink(ArmadaUiLogSink sink, void *userdata)`
*   **Purpose:** Registers a callback function (and a context pointer) to receive log messages.
*   **Logic:** Uses `std::mutex` to protect the global function pointer.

#### `armada_ui_log` / `armada_server_log`
*   **Signature:** `void armada_ui_log(const char *line)`
*   **Purpose:** Sends a raw string to the registered sink.
*   **Logic:** Locks mutex, checks if sink is not null, calls sink.

#### `armada_ui_vlogf` / `armada_server_vlogf`
*   **Signature:** `void armada_ui_vlogf(const char *fmt, va_list args)`
*   **Purpose:** Formats a string using `vsnprintf` and passes it to `_log`.

#### `armada_ui_logf` / `armada_server_logf`
*   **Signature:** `void armada_ui_logf(const char *fmt, ...)`
*   **Purpose:** Variadic wrapper (printf-style) for logging.


## 6. File: `src/client/launcher.cpp`
This is the main application file using `FTXUI`. It defines the `ArmadaApp` class and the `extern "C"` client callbacks.

### Global Entry Point

#### `armada_tui_run`
*   **Signature:** `int armada_tui_run(void)`
*   **Purpose:** Entry point called by `main.cpp`.
*   **Logic:** Instantiates `ArmadaApp`, calls `.run()`, and returns the result.

### Class: `ArmadaApp`

#### `run`
*   **Purpose:** Main application loop.
*   **Logic:**
    1.  Initializes `ScreenInteractive::Fullscreen()`.
    2.  Calls `build_components()`.
    3.  Registers log sinks (`log_thunk`).
    4.  Calls `start_join_scan()`.
    5.  Starts the UI loop (`screen_->Loop`).
    6.  Cleanup (stops threads, disconnects) on exit.

#### Component Builders
*   **`build_components`**: Calls builders for Host and Play tabs.
*   **`build_host_tab`**: Layouts the server hosting screen.
*   **`build_server_controls`**: Creates Start/Stop server buttons using `Maybe` to toggle visibility.
*   **`render_server_stats`**: Renders text listing connected players, current turn, and match status (Server side view).
*   **`render_server_logs`**: Renders the scrolling log of server events.
*   **`build_play_tab`**: Layouts the client gameplay screen.
*   **`build_join_view`**: Layouts the connection screen (Name input, LAN list, Connect button).
*   **`build_session_view`**: Layouts the active game screen (Turn indicator, stats, action buttons).
*   **`build_prematch_controls`**: Creates "Start Match" button (Host only) and Disconnect button.
*   **`render_game_logs`**: Renders client-side game event logs.
*   **`render_turn_indicator`**: Logic to display "YOUR TURN" in green or "Waiting..." in dim text.

#### LAN Scanning Logic
*   **`start_join_scan`**: Spawns a background thread.
*   **`scan_thread_` (Lambda)**: Loops periodically, calling `perform_lan_scan`. Sleeps for 10s or until `trigger_scan_now`.
*   **`stop_join_scan`**: Sets flag, wakes thread, joins thread.
*   **`trigger_scan_now`**: Forces immediate scan.
*   **`perform_lan_scan`**: Calls `net_discover_lan_servers` (C API) and updates `lan_hosts_`.
*   **`refresh_lan_hosts`**: Updates the UI Radiobox list with discovered IPs.

#### Client Session Logic
*   **`connect_to_selection`**: Connects to the IP selected in the LAN list.
*   **`connect_to_manual`**: Connects to the IP typed in the input box.
*   **`begin_client_session`**:
    1.  Creates `ClientContext`.
    2.  Calls `client_connect`.
    3.  Spawns `pump_loop` thread.
    4.  Switches UI to Session View.
*   **`stop_client_session`**: Stops pump thread, disconnects, switches UI back to Join View.
*   **`pump_loop`**: Background thread that calls `client_pump()` every 50ms and triggers `screen_->PostEvent(Custom)` to redraw UI.

#### Game Actions
*   **`send_start_request`**: Host clicks "Start Match". Calls `client_request_match_start`.
*   **`show_attack_dialog`**: Filters valid targets (players with HP > 0) and opens selection menu.
*   **`confirm_attack`**: Sends `USER_ACTION_ATTACK_PLANET`.
*   **`send_repair`**: Sends `USER_ACTION_REPAIR_PLANET`.
*   **`send_upgrade_planet` / `_ship`**: Sends respective upgrade actions.

#### Server Hosting Logic
*   **`start_local_server`**: Creates `ServerContext`, calls `server_init`, then `server_start`.
*   **`stop_local_server`**: Calls `server_stop` and destroys context.

#### Helpers
*   **`StyledButton`**: Helper to create buttons that dim when disabled.
*   **`log_thunk` / `server_log_thunk`**: Static bridge functions mapping C callbacks to `ArmadaApp::append_log`.
*   **`append_log`**: Adds string to deque, limits size to 200 lines, triggers redraw.

### Client C Callbacks (Extern "C")
They are called by `client.c` when events arrive.

*   **`client_on_init`**: Resets UI state data.
*   **`client_on_connecting` / `_connected`**: Logs connection progress.
*   **`client_on_connection_failed`**: Logs error.
*   **`client_on_disconnected`**: Logs disconnect.
*   **`client_on_join_request`**: Logs "Sending join request".
*   **`client_on_join_ack`**: Logs "Joined successfully" or rejection message.
*   **`client_on_player_joined`**: Logs "Player X joined".
*   **`client_on_player_left`**: Logs "Player X left".
*   **`client_on_host_update`**: Logs "Player X is now host".
*   **`client_on_match_start`**: Logs start, updates internal Host ID tracking.
*   **`client_on_turn_event`**: Logs turn details. Logs "Match phase starting" if it's the first turn.
*   **`client_on_threshold`**: Logs "Player X crossed 900 stars".
*   **`client_on_game_over`**: Logs winner.
*   **`client_on_action_sent`**: Logs "Action queued...".
*   **`client_on_match_stop`**: Logs error messages from server.