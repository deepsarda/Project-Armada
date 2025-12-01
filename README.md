# Project Armada: Multiplayer Strategy Game
**Project Armada** is a terminal-based (TUI), turn-based multiplayer strategy game written in C and C++. It features a custom TCP/UDP networking engine, a threaded server architecture, and a reactive client interface built with `FTXUI`.


## 🔭 Overview
In **Project Armada**, players assume the role of a planetary governor. Your goal is to build the most prosperous civilization in the galaxy. You must manage your economy, upgrade your defenses, and sabotage your rivals.

The game uses a **Client-Server architecture**. One player acts as the "Host" (Server + Client), while others join as Clients via LAN discovery or direct IP connection.

## 🚀 Features

*   **Cross-Platform Networking:** Custom C socket library supporting both Windows (Winsock) and Linux (BSD Sockets).
*   **TUI Interface:** Responsive, mouse-capable terminal interface using FTXUI.
*   **LAN Discovery:** Automatic local server discovery via UDP broadcasting.
*   **Lobby System:** Host/Join functionality with dynamic player lists.
*   **Economic Strategy:** The goal is pure accumulation. Manage resources, upgrade infrastructure, and sabotage rivals

## 🎮 Gameplay Mechanics

### The Core Loop
The game proceeds in turns. When it is your turn, you receive income based on your Planet Level. You may perform **one** action or simply end your turn to bank your resources.

### Resources & Stats
1.  **⭐ Stars:** The currency of the game. Used for upgrades and repairs.
2.  **🪐 Planet:**
    *   **Level:** Determines base income and max health.
    *   **Health:** If this reaches 0, your civilization collapses temporarily (see *Combat*).
3.  **🚀 Ship:**
    *   **Level:** Determines attack damage.

### Actions
*   **🛠 Repair Planet:** Restores your planet to full health. (Cost: 50 Stars).
*   **🏗 Upgrade Planet:** Increases income and max health. (Cost increases with level).
*   **⚔ Upgrade Ship:** Increases damage dealt to rivals. (Cost increases with level).
*   **🔥 Attack:** Strike a rival player. Deals damage based on your Ship Level. You gain stars based on the damage dealt. *Strategic Note:* Attacking is the only way to lower an opponent's Star count (by forcing them to repair or bankrupting them)
*   **🛑 End Turn:** Pass play to the next player.

### 🏆 Win Condition
The galaxy is won by the first player to accumulate **1,000 Stars** at the end of their turn.

### 💀 Combat Rules (No Elimination)
Armada features a "Resilience" mechanic. **Players cannot be eliminated.**
*   If a player's Planet Health reaches **0**:
    *   Their collected **Stars are reset to 0**.
    *   Their Planet Health remains at 0 until repaired.
    *   They remain in the game and can rebuild on their next turn.
*   This makes aggression a tool for economic sabotage rather than player removal.


## 🛠️ Build Instructions

### Prerequisites
*   **Compiler:** GCC or Clang (supporting C11 and C++17).
*   **Build System:** CMake (3.11+).
*   **Dependencies:**
    *   [FTXUI](https://github.com/ArthurSonzogni/FTXUI) (Fetched automatically via CMake FetchContent).
    *   Pthreads (Standard on Linux; requires setup on Windows/MinGW).
### Building
Do this only the first time.
```bash
mkdir build
cd build
cmake ..
cd ..
```

Then, use this command to build. 
```bash
cmake --build build
```

### Running
Run the executable from the build directory:
```bash
./build/armada # Linux/Mac
./build/armada.exe # Windows
```

## 🖥️ Application Usage

### The Interface
The application is split into two main tabs:
1.  **Host:** Controls for running a local server.
2.  **Play:** Controls for joining a game as a client.

### Hosting a Game
1.  Navigate to the **Host** tab.
2.  Click **Start Server**.
3.  The log will confirm the server is running on Port 8080.
4.  Wait for players to join (their names will appear in the Stats panel).
5.  *Note: The host must also join their own game via the Play tab (using `127.0.0.1`).*

### Joining a Game
1.  Navigate to the **Play** tab.
2.  **Auto-Discovery:** Wait a few seconds. Local servers will appear in the "Discovered LAN Servers" list. Select one and click **Join Selection**.
3.  **Manual Join:** Enter an IP address (e.g., `127.0.0.1`) and click **Join Manual IP**.
4.  Once connected, wait for the Host to start the match.
