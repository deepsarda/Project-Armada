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
    using ftxui::CatchEvent;
    using ftxui::Component;
    namespace Container = ftxui::Container;
    using ftxui::dim;
    using ftxui::Element;
    using ftxui::Event;
    using ftxui::flex;
    using ftxui::GREATER_THAN;
    using ftxui::hbox;
    using ftxui::HEIGHT;
    using ftxui::Input;
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

            auto tab_container = Container::Tab({menu_component_, join_component_, session_component_}, &view_index_);
            auto root = Renderer(tab_container, [tab_container]
                                 { return tab_container->Render() | border | size(WIDTH, GREATER_THAN, 72) | size(HEIGHT, GREATER_THAN, 24); });

            root = CatchEvent(root, [&](const Event &event)
                              {
                                  if (event == Event::Escape && view_ == View::Session)
                                  {
                                      stop_client_session();
                                      return true;
                                  }
                                  if (event == Event::Escape && view_ == View::Join)
                                  {
                                      switch_view(View::Menu);
                                      return true;
                                  }
                                  if (event == Event::Character('q'))
                                  {
                                      stop_client_session();
                                      stop_join_scan();
                                      stop_local_server();
                                      if (screen_)
                                      {
                                          screen_->Exit();
                                      }
                                      return true;
                                  }
                                  return false; });

            loop_running_.store(true, std::memory_order_release);
            screen_->Loop(root);
            loop_running_.store(false, std::memory_order_release);
            screen_ = nullptr;

            stop_client_session();
            stop_join_scan();
            stop_local_server();
            armada_ui_set_log_sink(nullptr, nullptr);
            return 0;
        }

    private:
        enum class View
        {
            Menu = 0,
            Join = 1,
            Session = 2
        };

        using ClientPtr = std::unique_ptr<ClientContext, decltype(&client_destroy)>;
        using ServerPtr = std::unique_ptr<ServerContext, decltype(&server_destroy)>;

        void build_components()
        {
            build_menu();
            build_join();
            build_session();
            refresh_lan_hosts({});
        }

        void build_menu()
        {
            auto host_button = Button("Host server", [&]
                                      { toggle_local_server(); });
            auto join_button = Button("Join server", [&]
                                      { switch_view(View::Join); });
            auto quit_button = Button("Quit", [&]
                                      {
                                          stop_client_session();
                                          stop_join_scan();
                                          stop_local_server();
                                          if (screen_)
                                          {
                                              screen_->Exit();
                                          } });

            auto buttons = Container::Vertical({host_button, join_button, quit_button});
            menu_component_ = Renderer(buttons, [this, buttons]
                                       {
                                           auto status = hosting_ ? text("Local server: running on port " + std::to_string(DEFAULT_PORT)) | bold
                                                                  : text("Local server: idle") | dim;
                                           return window(text("Project Armada Launcher"),
                                                         vbox({
                                                             text("Choose an option to get started."),
                                                             separator(),
                                                             status,
                                                             separator(),
                                                             buttons->Render(),
                                                         }) | size(WIDTH, GREATER_THAN, 48)); });
        }

        void build_join()
        {
            name_input_component_ = Input(&player_name_, "Voyager");
            manual_input_component_ = Input(&manual_ip_, "192.168.0.42");
            host_list_component_ = Radiobox(&lan_hosts_display_, &selected_host_index_);

            auto connect_discovered = Button("Join selection", [&]
                                             { connect_to_selection(); });
            auto connect_manual = Button("Join manual IP", [&]
                                         { connect_to_manual(); });
            auto back_button = Button("Back", [&]
                                      { switch_view(View::Menu); });

            auto buttons = Container::Horizontal({connect_discovered, connect_manual, back_button});

            auto join_stack = Container::Vertical({name_input_component_, manual_input_component_, host_list_component_, buttons});
            join_component_ = Renderer(join_stack, [this, buttons]
                                       { return window(text("Join a server"),
                                                       vbox({
                                                           text("Set your pilot name, pick a LAN host, or enter a manual IP."),
                                                           separator(),
                                                           hbox({text("Player name:") | size(WIDTH, GREATER_THAN, 16), name_input_component_->Render()}) | size(WIDTH, GREATER_THAN, 40),
                                                           hbox({text("Manual IP:"), manual_input_component_->Render()}),
                                                           separator(),
                                                           text("Discovered hosts:"),
                                                           host_list_component_->Render() | flex,
                                                           separator(),
                                                           hbox({text("Scanning every 30s"), separator(), text("Press JOIN to start playing") | dim}),
                                                           buttons->Render(),
                                                       }) | flex); });
        }

        void build_session()
        {
            auto end_turn = Button("End turn", [&]
                                   { send_end_turn(); });
            auto start_match = Button("Start match", [&]
                                      { send_start_request(); });
            auto disconnect = Button("Disconnect", [&]
                                     { stop_client_session(); });

            auto controls = Container::Horizontal({end_turn, start_match, disconnect});
            session_component_ = Renderer(controls, [this, controls]
                                          { return window(text("Client session"), render_session(controls)); });
        }

        Element render_session(const Component &controls)
        {
            std::string server = active_address_.empty() ? "<none>" : active_address_;
            std::string status;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                if (client_ && client_->connected)
                {
                    status = "Connected";
                }
                else
                {
                    status = "Disconnected";
                }
            }

            std::string log_text;
            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                std::ostringstream oss;
                for (const auto &line : logs_)
                {
                    oss << line << '\n';
                }
                log_text = oss.str();
            }

            return vbox({
                       text("Player: " + (player_name_.empty() ? std::string{"(unnamed)"} : player_name_)),
                       text("Server: " + server),
                       text("Status: " + status),
                       separator(),
                       paragraph(log_text.empty() ? std::string{"Waiting for events..."} : log_text) | flex,
                       separator(),
                       controls->Render(),
                   }) |
                   flex;
        }

        void switch_view(View next)
        {
            if (view_ == next)
            {
                return;
            }

            if (view_ == View::Join)
            {
                stop_join_scan();
            }

            view_ = next;
            view_index_ = static_cast<int>(view_);

            if (view_ == View::Join)
            {
                start_join_scan();
            }
            request_redraw();
        }

        void start_join_scan()
        {
            stop_join_scan();
            scanning_ = true;
            scan_thread_ = std::thread([this]()
                                       {
                                           constexpr auto kScanSleepChunk = 100ms;
                                           constexpr int kChunksPerRefresh = static_cast<int>(std::chrono::seconds(30) / kScanSleepChunk);
                                           while (scanning_)
                                           {
                                               char hosts_raw[ARMADA_DISCOVERY_MAX_RESULTS][64] = {};
                                               int found = net_discover_lan_servers(hosts_raw, ARMADA_DISCOVERY_MAX_RESULTS, DEFAULT_PORT, 200);
                                               std::vector<std::string> hosts;
                                               if (found > 0)
                                               {
                                                   hosts.reserve(found);
                                                   for (int i = 0; i < found; ++i)
                                                   {
                                                       hosts.emplace_back(hosts_raw[i]);
                                                   }
                                               }
                                               refresh_lan_hosts(hosts);
                                               for (int tick = 0; tick < kChunksPerRefresh && scanning_; ++tick)
                                               {
                                                   std::this_thread::sleep_for(kScanSleepChunk);
                                               }
                                           } });
        }

        void stop_join_scan()
        {
            scanning_ = false;
            if (scan_thread_.joinable())
            {
                scan_thread_.join();
            }
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
                {
                    lan_hosts_display_.push_back(std::wstring(host.begin(), host.end()));
                }
                if (selected_host_index_ < 0)
                {
                    selected_host_index_ = 0;
                }
                selected_host_index_ = std::min<int>(selected_host_index_, static_cast<int>(lan_hosts_.size() - 1));
            }
            request_redraw();
        }

        void connect_to_selection()
        {
            std::string address;
            {
                std::lock_guard<std::mutex> lock(host_mutex_);
                if (selected_host_index_ >= 0 && static_cast<std::size_t>(selected_host_index_) < lan_hosts_.size())
                {
                    address = lan_hosts_[selected_host_index_];
                }
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
            {
                player_name_ = "Voyager";
            }

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
            switch_view(View::Session);
        }

        void stop_client_session()
        {
            pumping_ = false;
            if (pump_thread_.joinable())
            {
                pump_thread_.join();
            }

            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_)
            {
                if (client_->connected)
                {
                    client_disconnect(client_.get());
                }
                client_.reset();
            }
            active_address_.clear();
            if (view_ == View::Session)
            {
                switch_view(View::Join);
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
                        {
                            pumping_ = false;
                        }
                    }
                }
                request_redraw();
                std::this_thread::sleep_for(50ms);
            }
            request_redraw();
        }

        void send_end_turn()
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_ && client_->connected)
            {
                client_send_action(client_.get(), USER_ACTION_END_TURN, -1, 0, 0);
            }
        }

        void send_start_request()
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            if (client_ && client_->connected)
            {
                client_request_match_start(client_.get());
            }
        }

        void start_local_server()
        {
            if (hosting_)
            {
                append_log("Local server already running.");
                return;
            }

            ServerPtr server(server_create(), &server_destroy);
            if (!server)
            {
                append_log("Unable to allocate server context.");
                return;
            }

            if (server_init(server.get(), MAX_PLAYERS) != 0)
            {
                append_log("Failed to initialize server context.");
                return;
            }

            server_start(server.get());
            if (!server->running)
            {
                append_log("Server failed to start. Is port " + std::to_string(DEFAULT_PORT) + " busy?");
                return;
            }

            host_server_ = std::move(server);
            hosting_ = true;
            append_log("Local server started on port " + std::to_string(DEFAULT_PORT) + ". Join via 127.0.0.1.");
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
            append_log("Local server stopped.");
            request_redraw();
        }

        void toggle_local_server()
        {
            if (hosting_)
            {
                stop_local_server();
            }
            else
            {
                start_local_server();
            }
        }

        static void log_thunk(const char *line, void *userdata)
        {
            if (!userdata)
            {
                return;
            }
            static_cast<ArmadaApp *>(userdata)->append_log(line ? std::string{line} : std::string{});
        }

        void append_log(const std::string &line)
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            if (!line.empty())
            {
                logs_.push_back(line);
            }
            else
            {
                logs_.push_back(std::string{});
            }
            while (logs_.size() > kMaxLogs)
            {
                logs_.pop_front();
            }
            request_redraw();
        }

    private:
        void request_redraw()
        {
            if (screen_ && loop_running_.load(std::memory_order_acquire))
            {
                screen_->PostEvent(Event::Custom);
            }
        }

        ScreenInteractive *screen_ = nullptr;
        View view_ = View::Menu;
        int view_index_ = 0;

        Component menu_component_;
        Component join_component_;
        Component session_component_;
        Component name_input_component_;
        Component manual_input_component_;
        Component host_list_component_;

        std::string player_name_ = "Voyager";
        std::string manual_ip_;
        std::vector<std::string> lan_hosts_;
        std::vector<std::wstring> lan_hosts_display_;
        int selected_host_index_ = 0;
        std::mutex host_mutex_;

        std::thread scan_thread_;
        std::atomic<bool> scanning_{false};

        ClientPtr client_{nullptr, &client_destroy};
        std::mutex client_mutex_;
        std::thread pump_thread_;
        std::atomic<bool> pumping_{false};
        std::string active_address_;
        ServerPtr host_server_{nullptr, &server_destroy};
        bool hosting_ = false;

        std::mutex log_mutex_;
        std::deque<std::string> logs_{};
        static constexpr std::size_t kMaxLogs = 200;
        std::atomic<bool> loop_running_{false};
    };
}

extern "C" int armada_tui_run(void)
{
    ArmadaApp app;
    int result = app.run();
    return result;
}