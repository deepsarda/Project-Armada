#include "../../include/client/tui_bridge.h"
#include "../../include/client/client_api.h"
#include "../../include/client/ui_notifications.h"
#include "../../include/common/events.h"
#include "../../include/networking/network.h"
#include "../../include/server/server_api.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;
    using ftxui::bold;
    using ftxui::border;
    using ftxui::Button;
    using ftxui::ButtonOption;
    using ftxui::CatchEvent;
    using ftxui::color;
    using ftxui::Color;
    using ftxui::Component;
    namespace Container = ftxui::Container;
    using ftxui::dim;
    using ftxui::Element;
    using ftxui::Event;
    using ftxui::flex;
    using ftxui::flex_shrink;
    using ftxui::GREATER_THAN;
    using ftxui::hbox;
    using ftxui::HEIGHT;
    using ftxui::Input;
    using ftxui::Maybe;
    using ftxui::Menu;
    using ftxui::paragraph;
    using ftxui::Radiobox;
    using ftxui::Renderer;
    using ftxui::ScreenInteractive;
    using ftxui::separator;
    using ftxui::size;
    using ftxui::text;
    using ftxui::vbox;
    using ftxui::WIDTH;
    using ftxui::window;

    // Parse ANSI color codes and convert to FTXUI elements
    Element parse_ansi_line(const std::string &line)
    {
        std::vector<Element> elements;
        std::string current_text;
        Color current_color = Color::Default;
        bool is_bold = false;

        for (size_t i = 0; i < line.size(); ++i)
        {
            // Check for ANSI escape sequence
            if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[')
            {
                // Flush current text with current style
                if (!current_text.empty())
                {
                    Element elem = text(current_text);
                    if (current_color != Color::Default)
                        elem = elem | color(current_color);
                    if (is_bold)
                        elem = elem | bold;
                    elements.push_back(elem);
                    current_text.clear();
                }

                // Find the end of the escape sequence
                size_t seq_start = i + 2;
                size_t seq_end = seq_start;
                while (seq_end < line.size() && line[seq_end] != 'm')
                    ++seq_end;

                if (seq_end < line.size())
                {
                    std::string code_str = line.substr(seq_start, seq_end - seq_start);
                    int code = std::atoi(code_str.c_str());

                    switch (code)
                    {
                    case 0: // Reset
                        current_color = Color::Default;
                        is_bold = false;
                        break;
                    case 1: // Bold
                        is_bold = true;
                        break;
                    case 31: // Red
                        current_color = Color::Red;
                        break;
                    case 32: // Green
                        current_color = Color::Green;
                        break;
                    case 33: // Yellow
                        current_color = Color::Yellow;
                        break;
                    case 34: // Blue
                        current_color = Color::Blue;
                        break;
                    case 35: // Magenta
                        current_color = Color::Magenta;
                        break;
                    case 36: // Cyan
                        current_color = Color::Cyan;
                        break;
                    default:
                        break;
                    }

                    i = seq_end; // Move past the 'm'
                }
            }
            else
            {
                current_text += line[i];
            }
        }

        // Flush remaining text
        if (!current_text.empty())
        {
            Element elem = text(current_text);
            if (current_color != Color::Default)
                elem = elem | color(current_color);
            if (is_bold)
                elem = elem | bold;
            elements.push_back(elem);
        }

        if (elements.empty())
            return text("");

        return hbox(elements);
    }

    // HELPER FUNCTIONS
    // Create a styled button that appears disabled when condition is false
    Component StyledButton(const std::string &label, std::function<void()> on_click, std::function<bool()> is_enabled)
    {
        auto option = ButtonOption::Simple();
        return Button(
                   label,
                   [on_click, is_enabled]()
                   {
                       if (is_enabled())
                       {
                           on_click();
                       }
                   },
                   option) |
               Renderer([is_enabled](Element inner)
                        {
                            if (!is_enabled())
                            {
                                return inner | dim | color(Color::GrayDark);
                            }
                            return inner; });
    }

    // Simple button without disable logic
    Component SimpleButton(const std::string &label, std::function<void()> on_click)
    {
        return Button(label, on_click, ButtonOption::Simple());
    }

    // MAIN APP CLASS
    class ArmadaApp
    {
    public:
        ArmadaApp() = default;

        int run()
        {
            ScreenInteractive screen = ScreenInteractive::Fullscreen();
            screen_ = &screen;

            build_components();
            armada_ui_set_log_sink(&ArmadaApp::log_thunk, this);
            armada_server_set_log_sink(&ArmadaApp::server_log_thunk, this);

            // Main tabs: Host and Play
            std::vector<std::string> tab_names = {"Host", "Play"};
            auto tab_menu = Menu(&tab_names, &main_tab_index_);

            auto tab_content = Container::Tab({host_component_, play_component_}, &main_tab_index_);

            auto main_container = Container::Horizontal({tab_menu, tab_content});

            auto root = Renderer(main_container, [&]
                                 {
                auto tab_element = tab_menu->Render() | border;
                auto content_element = tab_content->Render() | flex | border;
                return hbox({
                    tab_element | size(WIDTH, ftxui::EQUAL, 12),
                    content_element,
                }) | size(WIDTH, GREATER_THAN, 80) | size(HEIGHT, GREATER_THAN, 24); });

            root = CatchEvent(root, [&](const Event &event)
                              {
                if (event == Event::Character('q'))
                {
                    stop_client_session();
                    stop_join_scan();
                    stop_local_server();
                    if (screen_) screen_->Exit();
                    return true;
                }
                if (event == Event::Escape && play_view_ == PlayView::Session)
                {
                    stop_client_session();
                    return true;
                }
                return false; });

            start_join_scan();

            loop_running_.store(true, std::memory_order_release);
            screen_->Loop(root);
            loop_running_.store(false, std::memory_order_release);
            screen_ = nullptr;

            stop_client_session();
            stop_join_scan();
            stop_local_server();
            armada_ui_set_log_sink(nullptr, nullptr);
            armada_server_set_log_sink(nullptr, nullptr);
            return 0;
        }

    private:
        enum class PlayView
        {
            JoinServer = 0,
            Session = 1,
        };

        enum class DialogMode
        {
            None = 0,
            Attack,
        };

        using ClientPtr = std::unique_ptr<ClientContext, decltype(&client_destroy)>;
        using ServerPtr = std::unique_ptr<ServerContext, decltype(&server_destroy)>;

        // COMPONENT BUILDERS

        void build_components()
        {
            build_host_tab();
            build_play_tab();
            refresh_lan_hosts({});
        }

        // HOST TAB

        Component build_server_controls()
        {
            auto start_btn = SimpleButton("Start Server", [&]
                                          { start_local_server(); });
            auto stop_btn = SimpleButton("Stop Server", [&]
                                         { stop_local_server(); });

            // Use Maybe to show/hide buttons based on server state
            auto start_visible = start_btn | Maybe([&]
                                                   { return !hosting_; });
            auto stop_visible = stop_btn | Maybe([&]
                                                 { return hosting_; });

            return Container::Horizontal({start_visible, stop_visible});
        }

        Element render_server_stats()
        {
            std::vector<Element> stats_elements;
            stats_elements.push_back(text("Server Statistics") | bold);
            stats_elements.push_back(separator());

            if (hosting_ && host_server_)
            {
                pthread_mutex_lock(&host_server_->state_mutex);
                GameState &gs = host_server_->game_state;

                stats_elements.push_back(text("Players: " + std::to_string(gs.player_count) + "/" + std::to_string(host_server_->max_players)));
                stats_elements.push_back(text("Match Started: " + std::string(gs.match_started ? "Yes" : "No")));

                if (gs.match_started)
                {
                    stats_elements.push_back(text("Turn: " + std::to_string(gs.turn.turn_number)));
                    if (gs.turn.current_player_id >= 0 && gs.turn.current_player_id < MAX_PLAYERS)
                    {
                        stats_elements.push_back(text("Current Player: " + std::string(gs.players[gs.turn.current_player_id].name)));
                    }
                }

                stats_elements.push_back(separator());
                stats_elements.push_back(text("Player List:") | bold);

                for (int i = 0; i < MAX_PLAYERS; ++i)
                {
                    if (gs.players[i].is_active)
                    {
                        std::string status_str;
                        if (gs.host_player_id == i)
                            status_str = " [HOST]";
                        if (gs.match_started && gs.turn.current_player_id == i)
                            status_str += " <- Turn";
                        std::string player_line = "  " + std::to_string(i) + ": " + gs.players[i].name + status_str;
                        if (gs.match_started)
                        {
                            player_line += " | Stars: " + std::to_string(gs.players[i].stars);
                            player_line += " | HP: " + std::to_string(gs.players[i].planet.current_health) + "/" + std::to_string(gs.players[i].planet.max_health);
                        }
                        stats_elements.push_back(text(player_line));
                    }
                }

                pthread_mutex_unlock(&host_server_->state_mutex);
            }
            else
            {
                stats_elements.push_back(text("(Server not running)") | dim);
            }

            return vbox(stats_elements) | flex;
        }

        Element render_server_logs()
        {
            std::vector<Element> log_elements;
            {
                std::lock_guard<std::mutex> lock(server_log_mutex_);
                for (const auto &line : server_logs_)
                    log_elements.push_back(parse_ansi_line(line));
            }
            if (log_elements.empty())
                return text("(No logs yet)") | flex;
            return vbox(log_elements) | flex;
        }

        void build_host_tab()
        {
            auto server_controls = build_server_controls();

            host_component_ = Renderer(server_controls, [this, server_controls]
                                       {
                Element server_status;
                if (hosting_)
                    server_status = text("Server: RUNNING on port " + std::to_string(DEFAULT_PORT)) | bold | color(Color::Green);
                else
                    server_status = text("Server: STOPPED") | dim;

                auto stats_panel = window(text("Stats"), render_server_stats());
                auto logs_panel = window(text("Server Logs"), render_server_logs());

                return vbox({
                    text("HOST SERVER") | bold,
                    separator(),
                    server_status,
                    separator(),
                    hbox({
                        stats_panel | size(WIDTH, GREATER_THAN, 40) | flex_shrink,
                        logs_panel | flex,
                    }) | flex,
                    separator(),
                    server_controls->Render(),
                }) | flex; });
        }

        // PLAY TAB

        void build_play_tab()
        {
            build_join_view();
            build_session_view();

            auto play_views = Container::Tab({join_component_, session_component_}, &play_view_index_);
            play_component_ = Renderer(play_views, [play_views]
                                       { return play_views->Render() | flex; });
        }

        // JOIN VIEW

        Component build_join_controls()
        {
            auto connect_selection = SimpleButton("Join Selection", [&]
                                                  { connect_to_selection(); });
            auto connect_manual = SimpleButton("Join Manual IP", [&]
                                               { connect_to_manual(); });
            auto search_now = SimpleButton("Search Now", [&]
                                           { trigger_scan_now(); });

            return Container::Horizontal({connect_selection, connect_manual, search_now});
        }

        void build_join_view()
        {
            name_input_component_ = Input(&player_name_, "Voyager");
            manual_input_component_ = Input(&manual_ip_, "192.168.0.42");
            host_list_component_ = Radiobox(&lan_hosts_display_, &selected_host_index_);

            auto join_controls = build_join_controls();
            auto join_stack = Container::Vertical({name_input_component_, manual_input_component_, host_list_component_, join_controls});

            join_component_ = Renderer(join_stack, [this, join_controls]
                                       { return vbox({
                                                    text("JOIN SERVER") | bold,
                                                    separator(),
                                                    hbox({text("Player Name: ") | size(WIDTH, ftxui::EQUAL, 14), name_input_component_->Render() | flex}),
                                                    hbox({text("Manual IP: ") | size(WIDTH, ftxui::EQUAL, 14), manual_input_component_->Render() | flex}),
                                                    separator(),
                                                    text("Discovered LAN Servers:") | bold,
                                                    host_list_component_->Render() | flex | border,
                                                    text("Auto-scanning every 10s. Press 'Search Now' to refresh.") | dim,
                                                    separator(),
                                                    join_controls->Render(),
                                                }) |
                                                flex; });
        }

        // SESSION VIEW

        Component build_prematch_controls()
        {
            auto disconnect_btn = SimpleButton("Disconnect", [&]
                                               { stop_client_session(); });

            auto start_match_btn = StyledButton(
                "Start Match",
                [&]
                { send_start_request(); },
                [&]
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    return client_ && client_->is_host;
                });

            return Container::Horizontal({disconnect_btn, start_match_btn});
        }

        Component build_game_action_buttons()
        {
            // TODO: Implement game action buttons
            return Container::Horizontal({});
        }

        Component build_attack_dialog()
        {
            // TODO: Implement attack dialog
            return Renderer([]
                            { return vbox(); });
        }

        Element render_other_players()
        {
            // TODO: Implement detailed player list
            return vbox();
        }

        Element render_self_info()
        {
            // TODO: Implement self info display
            return vbox();
        }

        Element render_game_logs()
        {
            std::vector<Element> log_elements;
            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                for (const auto &line : logs_)
                    log_elements.push_back(parse_ansi_line(line));
            }
            if (log_elements.empty())
                return text("Waiting for events...") | flex;
            return vbox(log_elements) | flex;
        }

        Element render_turn_indicator()
        {
            bool match_started = false;
            bool is_my_turn = false;
            int current_turn_id = -1;

            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                if (client_)
                {
                    match_started = client_->match_started;
                    is_my_turn = (client_->current_turn_player_id == client_->player_id);
                    current_turn_id = client_->current_turn_player_id;
                }
            }

            if (match_started)
            {
                if (is_my_turn)
                    return text(">>> YOUR TURN <<<") | bold | color(Color::Green);
                else
                    return text("Waiting for Player " + std::to_string(current_turn_id) + "...") | dim;
            }
            return text("Waiting for match to start...") | dim;
        }

        void build_session_view()
        {
            auto prematch_controls = build_prematch_controls();
            // TODO: Add game action buttons here
            auto attack_dialog = build_attack_dialog();

            // Use Maybe to show prematch or game controls based on match state
            auto prematch_visible = prematch_controls | Maybe([&]
                                                              {
                std::lock_guard<std::mutex> lock(client_mutex_);
                return !client_ || !client_->match_started; });
            // TODO: Wrap game action buttons similarly
            auto all_controls = Container::Vertical({prematch_visible, attack_dialog});

            session_component_ = Renderer(all_controls, [this, all_controls]
                                          {
                std::string server = active_address_.empty() ? "<none>" : active_address_;
                std::string status;
                bool is_host = false;

                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    status = (client_ && client_->connected) ? "Connected" : "Disconnected";
                    is_host = client_ && client_->is_host;
                }

                auto other_players_panel = window(text("Opponents"), render_other_players());
                auto self_panel = window(text("You"), render_self_info());
                auto logs_panel = window(text("Game Log"), render_game_logs());

                std::vector<Element> info_line;
                info_line.push_back(text("Server: " + server + " | Status: " + status));
                if (is_host)
                    info_line.push_back(text(" | You are HOST") | color(Color::Yellow));

                return vbox({
                    text("GAME SESSION") | bold,
                    hbox(info_line),
                    separator(),
                    other_players_panel,
                    separator(),
                    render_turn_indicator(),
                    separator(),
                    hbox({
                        self_panel | size(WIDTH, GREATER_THAN, 30),
                        logs_panel | flex,
                    }) | flex,
                    separator(),
                    all_controls->Render(),
                }) | flex; });
        }

        // SCANNING

        void start_join_scan()
        {
            stop_join_scan();
            scanning_ = true;
            scan_thread_ = std::thread([this]()
                                       {
                constexpr auto kScanSleepChunk = 100ms;
                constexpr int kChunksPerRefresh = static_cast<int>(std::chrono::seconds(10) / kScanSleepChunk);
                while (scanning_)
                {
                    perform_lan_scan();
                    for (int tick = 0; tick < kChunksPerRefresh && scanning_ && !scan_now_requested_; ++tick)
                    {
                        std::this_thread::sleep_for(kScanSleepChunk);
                    }
                    scan_now_requested_ = false;
                } });
        }

        void stop_join_scan()
        {
            scanning_ = false;
            scan_now_requested_ = true; // Break out of sleep early
            if (scan_thread_.joinable())
                scan_thread_.join();
        }

        void trigger_scan_now()
        {
            scan_now_requested_ = true;
        }

        void perform_lan_scan()
        {
            char hosts_raw[ARMADA_DISCOVERY_MAX_RESULTS][64] = {};
            int found = net_discover_lan_servers(hosts_raw, ARMADA_DISCOVERY_MAX_RESULTS, DEFAULT_PORT, 200);
            std::vector<std::string> hosts;
            if (found > 0)
            {
                hosts.reserve(found);
                for (int i = 0; i < found; ++i)
                    hosts.emplace_back(hosts_raw[i]);
            }
            refresh_lan_hosts(hosts);
        }

        void refresh_lan_hosts(const std::vector<std::string> &hosts)
        {
            std::lock_guard<std::mutex> lock(host_mutex_);
            lan_hosts_ = hosts;
            lan_hosts_display_.clear();
            if (lan_hosts_.empty())
            {
                lan_hosts_display_.push_back(L"(No LAN servers detected)");
                selected_host_index_ = 0;
            }
            else
            {
                for (const auto &host : lan_hosts_)
                    lan_hosts_display_.push_back(std::wstring(host.begin(), host.end()));
                if (selected_host_index_ < 0)
                    selected_host_index_ = 0;
                selected_host_index_ = std::min<int>(selected_host_index_, static_cast<int>(lan_hosts_.size() - 1));
            }
            request_redraw();
        }

        // CONNECTION

        void connect_to_selection()
        {
            std::string address;
            {
                std::lock_guard<std::mutex> lock(host_mutex_);
                if (selected_host_index_ >= 0 && static_cast<std::size_t>(selected_host_index_) < lan_hosts_.size())
                    address = lan_hosts_[selected_host_index_];
            }

            if (address.empty())
            {
                append_log("Select a discovered host before joining.");
                return;
            }
            begin_client_session(address);
        }

        void connect_to_manual()
        {
            if (manual_ip_.empty())
            {
                append_log("Enter a manual IP first.");
                return;
            }
            begin_client_session(manual_ip_);
        }

        void begin_client_session(const std::string &address)
        {
            if (player_name_.empty())
                player_name_ = "Voyager";

            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                if (client_ && client_->connected)
                {
                    append_log("Already connected. Disconnect first.");
                    return;
                }
            }

            ClientContext *raw = client_create(player_name_.c_str());
            if (!raw)
            {
                append_log("Failed to allocate client context.");
                return;
            }

            client_.reset(raw);
            active_address_ = address;

            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                if (client_connect(client_.get(), address.c_str()) != 0)
                {
                    append_log("Unable to connect to " + address + ".");
                    client_.reset();
                    active_address_.clear();
                    return;
                }
            }

            pumping_ = true;
            pump_thread_ = std::thread(&ArmadaApp::pump_loop, this);
            switch_play_view(PlayView::Session);
            stop_join_scan();
        }

        void stop_client_session()
        {
            pumping_ = false;
            if (pump_thread_.joinable())
                pump_thread_.join();

            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_)
            {
                if (client_->connected)
                    client_disconnect(client_.get());
                client_.reset();
            }
            active_address_.clear();
            dialog_mode_ = DialogMode::None;
            if (play_view_ == PlayView::Session)
            {
                switch_play_view(PlayView::JoinServer);
                start_join_scan();
            }
        }

        void pump_loop()
        {
            while (pumping_)
            {
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    if (client_)
                    {
                        client_pump(client_.get());
                        if (!client_->connected)
                            pumping_ = false;
                    }
                }
                request_redraw();
                std::this_thread::sleep_for(50ms);
            }
            request_redraw();
        }

        void switch_play_view(PlayView view)
        {
            play_view_ = view;
            play_view_index_ = static_cast<int>(view);
            request_redraw();
        }

        // GAME ACTIONS

        void send_start_request()
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_ && client_->connected && client_->is_host)
                client_request_match_start(client_.get());
        }

        void show_attack_dialog()
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (!client_ || !client_->connected || !(client_->valid_actions & VALID_ACTION_ATTACK_PLANET))
                return;

            target_players_display_.clear();
            target_player_ids_.clear();
            for (int i = 0; i < MAX_PLAYERS; ++i)
            {
                const PlayerPublicInfo &info = client_->player_game_state.entries[i];
                if (info.player_id != client_->player_id && info.planet_level > 0)
                {
                    target_player_ids_.push_back(i);
                    target_players_display_.push_back(L"Player " + std::to_wstring(i));
                }
            }

            if (target_players_display_.empty())
                return;

            selected_target_index_ = 0;
            dialog_mode_ = DialogMode::Attack;
            request_redraw();
        }

        void confirm_attack()
        {
            if (dialog_mode_ != DialogMode::Attack)
                return;

            std::lock_guard<std::mutex> lock(client_mutex_);
            if (!client_ || !client_->connected || !(client_->valid_actions & VALID_ACTION_ATTACK_PLANET))
            {
                dialog_mode_ = DialogMode::None;
                return;
            }

            if (selected_target_index_ >= 0 && static_cast<std::size_t>(selected_target_index_) < target_player_ids_.size())
            {
                int target_id = target_player_ids_[selected_target_index_];
                int damage = client_->player_game_state.self.ship.base_damage;
                client_send_action(client_.get(), USER_ACTION_ATTACK_PLANET, target_id, damage, 0);
            }

            dialog_mode_ = DialogMode::None;
            request_redraw();
        }

        void send_repair()
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_ && client_->connected && (client_->valid_actions & VALID_ACTION_REPAIR_PLANET))
                client_send_action(client_.get(), USER_ACTION_REPAIR_PLANET, -1, 20, 0);
        }

        void send_upgrade_planet()
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_ && client_->connected && (client_->valid_actions & VALID_ACTION_UPGRADE_PLANET))
                client_send_action(client_.get(), USER_ACTION_UPGRADE_PLANET, -1, 0, 0);
        }

        void send_upgrade_ship()
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_ && client_->connected && (client_->valid_actions & VALID_ACTION_UPGRADE_SHIP))
                client_send_action(client_.get(), USER_ACTION_UPGRADE_SHIP, -1, 0, 0);
        }

        // SERVER HOSTING

        void start_local_server()
        {
            if (hosting_)
            {
                append_server_log("Local server already running.");
                return;
            }

            ServerPtr server(server_create(), &server_destroy);
            if (!server)
            {
                append_server_log("Unable to allocate server context.");
                return;
            }

            if (server_init(server.get(), MAX_PLAYERS) != 0)
            {
                append_server_log("Failed to initialize server context.");
                return;
            }

            server_start(server.get());
            if (!server->running)
            {
                append_server_log("Server failed to start. Is port " + std::to_string(DEFAULT_PORT) + " busy?");
                return;
            }

            host_server_ = std::move(server);
            hosting_ = true;
            append_server_log("Local server started on port " + std::to_string(DEFAULT_PORT) + ".");
            request_redraw();
        }

        void stop_local_server()
        {
            if (!host_server_)
            {
                hosting_ = false;
                return;
            }

            server_stop(host_server_.get());
            host_server_.reset();
            hosting_ = false;
            append_server_log("Local server stopped.");
            request_redraw();
        }

        // LOGGING

        static void log_thunk(const char *line, void *userdata)
        {
            if (!userdata)
                return;
            static_cast<ArmadaApp *>(userdata)->append_log(line ? std::string{line} : std::string{});
        }

        static void server_log_thunk(const char *line, void *userdata)
        {
            if (!userdata)
                return;
            static_cast<ArmadaApp *>(userdata)->append_server_log(line ? std::string{line} : std::string{});
        }

        void append_log(const std::string &line)
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            logs_.push_back(line.empty() ? std::string{} : line);
            while (logs_.size() > kMaxLogs)
                logs_.pop_front();
            request_redraw();
        }

        void append_server_log(const std::string &line)
        {
            std::lock_guard<std::mutex> lock(server_log_mutex_);
            if (!line.empty())
                server_logs_.push_back(line);
            while (server_logs_.size() > kMaxLogs)
                server_logs_.pop_front();
            request_redraw();
        }

        void request_redraw()
        {
            if (screen_ && loop_running_.load(std::memory_order_acquire))
                screen_->PostEvent(Event::Custom);
        }

        // MEMBER VARIABLES

        ScreenInteractive *screen_ = nullptr;

        // Main tab state
        int main_tab_index_ = 0;

        // Play tab sub-view state
        PlayView play_view_ = PlayView::JoinServer;
        int play_view_index_ = 0;

        // Components
        Component host_component_;
        Component play_component_;
        Component join_component_;
        Component session_component_;
        Component name_input_component_;
        Component manual_input_component_;
        Component host_list_component_;
        Component target_list_component_;

        // Join state
        std::string player_name_ = "Voyager";
        std::string manual_ip_;
        std::vector<std::string> lan_hosts_;
        std::vector<std::wstring> lan_hosts_display_;
        int selected_host_index_ = 0;
        std::mutex host_mutex_;

        // Scanning
        std::thread scan_thread_;
        std::atomic<bool> scanning_{false};
        std::atomic<bool> scan_now_requested_{false};

        // Client connection
        ClientPtr client_{nullptr, &client_destroy};
        std::mutex client_mutex_;
        std::thread pump_thread_;
        std::atomic<bool> pumping_{false};
        std::string active_address_;

        // Server hosting
        ServerPtr host_server_{nullptr, &server_destroy};
        bool hosting_ = false;

        // Dialog state
        DialogMode dialog_mode_ = DialogMode::None;
        std::vector<std::wstring> target_players_display_;
        std::vector<int> target_player_ids_;
        int selected_target_index_ = 0;

        // Logging
        std::mutex log_mutex_;
        std::deque<std::string> logs_{};
        std::mutex server_log_mutex_;
        std::deque<std::string> server_logs_{};
        static constexpr std::size_t kMaxLogs = 200;
        std::atomic<bool> loop_running_{false};
    };
}

extern "C" int armada_tui_run(void)
{
    ArmadaApp app;
    return app.run();
}

// CLIENT CALLBACKS
// These functions are called from the C networking layer (client.c)

// ANSI color codes for client logging
#define CLR_RESET "\033[0m"
#define CLR_GREEN "\033[32m"
#define CLR_YELLOW "\033[33m"
#define CLR_RED "\033[31m"
#define CLR_CYAN "\033[36m"
#define CLR_MAGENTA "\033[35m"
#define CLR_BLUE "\033[34m"
#define CLR_BOLD "\033[1m"

// Helper function to get action name
static const char *get_action_name(UserActionType type)
{
    switch (type)
    {
    case USER_ACTION_NONE:
        return "None";
    case USER_ACTION_END_TURN:
        return "End Turn";
    case USER_ACTION_ATTACK_PLANET:
        return "Attack Planet";
    case USER_ACTION_REPAIR_PLANET:
        return "Repair Planet";
    case USER_ACTION_UPGRADE_PLANET:
        return "Upgrade Planet";
    case USER_ACTION_UPGRADE_SHIP:
        return "Upgrade Ship";
    default:
        return "Unknown";
    }
}

extern "C" int client_on_init(ClientContext *ctx, const char *player_name)
{
    (void)player_name;
    ctx->has_state_snapshot = 0;
    memset(&ctx->player_game_state, 0, sizeof(PlayerGameState));
    return 0;
}

extern "C" void client_on_connected(ClientContext *ctx)
{
    armada_ui_logf(CLR_GREEN "[%s]" CLR_RESET " Connected to server.", ctx->player_name);
}

extern "C" void client_on_connecting(ClientContext *ctx, const char *server_addr, int port)
{
    armada_ui_logf(CLR_CYAN "[%s]" CLR_RESET " Connecting to " CLR_BOLD "%s:%d" CLR_RESET "...", ctx->player_name, server_addr, port);
}

extern "C" void client_on_connection_failed(ClientContext *ctx, const char *server_addr, int port)
{
    armada_ui_logf(CLR_RED "[%s] ERROR:" CLR_RESET " Connection to %s:%d failed.", ctx->player_name, server_addr, port);
}

extern "C" void client_on_disconnected(ClientContext *ctx)
{
    armada_ui_logf(CLR_YELLOW "[%s]" CLR_RESET " Disconnected from server.", ctx->player_name);
}

extern "C" void client_on_join_request(ClientContext *ctx)
{
    armada_ui_logf(CLR_CYAN "[%s]" CLR_RESET " Sending join request...", ctx->player_name);
}

extern "C" void client_on_join_ack(ClientContext *ctx, const EventPayload_JoinAck *payload)
{
    if (!payload)
        return;
    if (payload->success)
    {
        armada_ui_logf(CLR_GREEN "[%s]" CLR_RESET " Joined successfully! Assigned ID " CLR_BOLD "%d" CLR_RESET ".", ctx->player_name, payload->player_id);
        if (payload->is_host)
        {
            armada_ui_logf(CLR_MAGENTA "[%s]" CLR_RESET " " CLR_BOLD "You are the lobby host." CLR_RESET " Use 'start' to begin once ready.", ctx->player_name);
        }
        else if (payload->host_player_id >= 0)
        {
            armada_ui_logf(CLR_CYAN "[%s]" CLR_RESET " Waiting for host (player %d) to start the match.", ctx->player_name, payload->host_player_id);
        }
    }
    else
    {
        armada_ui_logf(CLR_RED "[%s] REJECTED:" CLR_RESET " %s", ctx->player_name, payload->message);
    }
}

extern "C" void client_on_player_joined(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload)
{
    if (!payload)
        return;
    armada_ui_logf(CLR_GREEN "[%s]" CLR_RESET " Player " CLR_BOLD "%s" CLR_RESET " (ID %d) joined the game.",
                   ctx->player_name,
                   payload->player_name,
                   payload->player_id);
}

extern "C" void client_on_host_update(ClientContext *ctx, const EventPayload_HostUpdate *payload)
{
    if (!payload)
        return;

    if (payload->host_player_id >= 0)
    {
        armada_ui_logf(CLR_MAGENTA "[%s]" CLR_RESET " " CLR_BOLD "%s" CLR_RESET " (ID %d) is now the lobby host.",
                       ctx->player_name,
                       payload->host_player_name,
                       payload->host_player_id);
        if (ctx && ctx->player_id == payload->host_player_id)
        {
            armada_ui_logf(CLR_MAGENTA "[%s]" CLR_RESET " " CLR_BOLD "You are now the host!" CLR_RESET);
        }
    }
    else
    {
        armada_ui_logf(CLR_YELLOW "[%s]" CLR_RESET " Lobby host cleared. Waiting for a new host...", ctx->player_name);
    }
}

extern "C" void client_on_player_left(ClientContext *ctx, const EventPayload_PlayerLifecycle *payload)
{
    if (!payload)
        return;
    armada_ui_logf(CLR_YELLOW "[%s]" CLR_RESET " Player " CLR_BOLD "%s" CLR_RESET " (ID %d) left the game.",
                   ctx->player_name,
                   payload->player_name,
                   payload->player_id);
}

extern "C" void client_on_match_start(ClientContext *ctx, const EventPayload_MatchStart *payload)
{
    if (!payload || !ctx)
        return;

    ctx->host_player_id = payload->state.host_player_id;
    ctx->is_host = (ctx->player_id >= 0 && ctx->player_id == ctx->host_player_id);
    ctx->has_state_snapshot = 0;
    memset(&ctx->player_game_state, 0, sizeof(PlayerGameState));

    armada_ui_logf(CLR_GREEN CLR_BOLD "=== MATCH STARTED ===" CLR_RESET);
    armada_ui_logf(CLR_CYAN "[%s]" CLR_RESET " %d players in match. First turn: Player %d.",
                   ctx->player_name,
                   payload->state.player_count,
                   payload->state.turn.current_player_id);
}

extern "C" void client_on_match_stop(ClientContext *ctx, const EventPayload_Error *payload)
{
    const char *reason = (payload) ? payload->message : "Unknown";
    armada_ui_logf(CLR_RED "[%s] SERVER:" CLR_RESET " %s", ctx->player_name, reason);
}

extern "C" void client_on_turn_event(ClientContext *ctx, EventType type, const EventPayload_TurnInfo *payload)
{
    if (!payload)
        return;

    (void)type;

    // Log whose turn it is
    if (payload->current_player_id == ctx->player_id)
    {
        armada_ui_logf(CLR_GREEN CLR_BOLD "[%s] >>> YOUR TURN (Turn #%d) <<<" CLR_RESET, ctx->player_name, payload->turn_number);
    }
    else
    {
        armada_ui_logf(CLR_CYAN "[%s]" CLR_RESET " Turn #%d: Player %d's turn.", ctx->player_name, payload->turn_number, payload->current_player_id);
    }

    if (payload->is_match_start)
    {
        armada_ui_logf(CLR_MAGENTA "[%s]" CLR_RESET " Match phase starting!", ctx->player_name);
    }

    // Log last action with readable description
    if (payload->last_action.action_type != USER_ACTION_NONE)
    {
        const char *action_name = get_action_name(payload->last_action.action_type);
        if (payload->last_action.action_type == USER_ACTION_ATTACK_PLANET)
        {
            armada_ui_logf(CLR_RED "[%s]" CLR_RESET " Player %d used " CLR_BOLD "%s" CLR_RESET " on Player %d.",
                           ctx->player_name,
                           payload->last_action.player_id,
                           action_name,
                           payload->last_action.target_player_id);
        }
        else
        {
            armada_ui_logf(CLR_BLUE "[%s]" CLR_RESET " Player %d used " CLR_BOLD "%s" CLR_RESET ".",
                           ctx->player_name,
                           payload->last_action.player_id,
                           action_name);
        }
    }

    // Log threshold crossing
    if (payload->threshold_player_id >= 0)
    {
        armada_ui_logf(CLR_YELLOW CLR_BOLD "[%s] ⚠ WARNING:" CLR_RESET " Player %d has crossed 900 stars!",
                       ctx->player_name,
                       payload->threshold_player_id);
    }
}

extern "C" void client_on_threshold(ClientContext *ctx, const EventPayload_Threshold *payload)
{
    if (!payload)
        return;
    armada_ui_logf(CLR_YELLOW CLR_BOLD "[%s] ⚠ ALERT:" CLR_RESET " Player %d crossed " CLR_BOLD "%d" CLR_RESET " stars!",
                   ctx->player_name,
                   payload->player_id,
                   payload->threshold);
}

extern "C" void client_on_action_sent(ClientContext *ctx, UserActionType type, int target_player_id, int value, int metadata)
{
    (void)value;
    (void)metadata;
    const char *action_name = get_action_name(type);
    if (type == USER_ACTION_ATTACK_PLANET)
    {
        armada_ui_logf(CLR_CYAN "[%s]" CLR_RESET " Sending action: " CLR_BOLD "%s" CLR_RESET " → Player %d",
                       ctx->player_name, action_name, target_player_id);
    }
    else
    {
        armada_ui_logf(CLR_CYAN "[%s]" CLR_RESET " Sending action: " CLR_BOLD "%s" CLR_RESET,
                       ctx->player_name, action_name);
    }
}

extern "C" void client_on_game_over(ClientContext *ctx, int winner_id)
{
    armada_ui_logf(CLR_MAGENTA CLR_BOLD "=== GAME OVER ===" CLR_RESET);
    if (winner_id == ctx->player_id)
    {
        armada_ui_logf(CLR_GREEN CLR_BOLD "[%s] 🎉 YOU WIN! 🎉" CLR_RESET, ctx->player_name);
    }
    else
    {
        armada_ui_logf(CLR_RED "[%s]" CLR_RESET " Player %d wins!", ctx->player_name, winner_id);
    }
}
