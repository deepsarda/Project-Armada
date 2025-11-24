#include "../../include/client/tui_bridge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <ox/ox.hpp>

namespace
{
    using ox::Application;
    using ox::Canvas;
    using ox::Key;

    constexpr int kDefaultScanTimeoutMs = 400;
    constexpr int kMinScanTimeoutMs = 100;
    constexpr int kMaxScanTimeoutMs = 5000;
    constexpr int kDefaultScanLimit = 8;
    constexpr int kMinScanLimit = 1;

    enum FieldId : std::size_t
    {
        FieldPlayerName = 0,
        FieldManualAddress,
        FieldScanTimeout,
        FieldScanLimit,
        FieldCount
    };

    enum ActionId : std::size_t
    {
        ActionHost = 0,
        ActionScan,
        ActionManualJoin,
        ActionQuit,
        ActionCount
    };

    struct FieldDefinition
    {
        std::string label;
        std::string value;
        std::string hint;
        std::size_t max_length;
        bool numeric_only;
        int min_value;
        int max_value;
        int step;
        int fallback;
    };

    enum class FocusType
    {
        Field,
        Action
    };

    struct FocusItem
    {
        FocusType type;
        std::size_t index;
    };

    static void copy_into_buffer(const std::string &text, char *buffer, std::size_t buffer_size)
    {
        if (!buffer || buffer_size == 0)
        {
            return;
        }

        if (text.empty())
        {
            buffer[0] = '\0';
            return;
        }

        std::snprintf(buffer, buffer_size, "%s", text.c_str());
    }

    class LauncherWidget : public ox::Widget
    {
    public:
        explicit LauncherWidget(ArmadaTuiSelection &selection)
            : Widget{ox::FocusPolicy::Strong, ox::SizePolicy::flex()}, selection_{selection}
        {
            init_fields();
            focus_items_ = {
                FocusItem{FocusType::Field, FieldPlayerName},
                FocusItem{FocusType::Field, FieldManualAddress},
                FocusItem{FocusType::Field, FieldScanTimeout},
                FocusItem{FocusType::Field, FieldScanLimit},
                FocusItem{FocusType::Action, ActionHost},
                FocusItem{FocusType::Action, ActionScan},
                FocusItem{FocusType::Action, ActionManualJoin},
                FocusItem{FocusType::Action, ActionQuit},
            };
        }

        void paint(Canvas c) override
        {
            ox::fill(c, U' ');
            int row = 0;
            draw_header(c, row);
            row += 2;
            draw_fields(c, row);
            row += 1;
            draw_actions(c, row);
            row = std::max(row + 1, c.size.height - 3);
            draw_instructions(c, row);
        }

        void key_press(Key key) override
        {
            if (editing_field_ >= 0)
            {
                handle_edit_key(key);
                return;
            }

            switch (key)
            {
            case Key::ArrowDown:
            case Key::Tab:
                move_focus(+1);
                break;
            case Key::ArrowUp:
            case Key::BackTab:
                move_focus(-1);
                break;
            case Key::ArrowLeft:
                adjust_numeric_current(-1);
                break;
            case Key::ArrowRight:
                adjust_numeric_current(+1);
                break;
            case Key::Enter:
                activate_current();
                break;
            case Key::Escape:
            case Key::Cancel:
                selection_.action = ARMADA_TUI_ACTION_QUIT;
                Application::quit(ARMADA_TUI_STATUS_EXIT_REQUESTED);
                break;
            default:
                break;
            }
        }

    private:
        void init_fields()
        {
            fields_.fill(FieldDefinition{});

            auto bootstrap_or_default = [](const char *value, const char *fallback)
            {
                if (value && value[0] != '\0')
                {
                    return std::string{value};
                }
                return std::string{fallback};
            };

            fields_[FieldPlayerName] = FieldDefinition{
                .label = "Player name",
                .value = bootstrap_or_default(selection_.player_name, "Voyager"),
                .hint = "Voyager",
                .max_length = MAX_NAME_LEN - 1,
                .numeric_only = false,
                .min_value = 0,
                .max_value = 0,
                .step = 0,
                .fallback = 0};

            fields_[FieldManualAddress] = FieldDefinition{
                .label = "Manual server IP",
                .value = selection_.manual_address,
                .hint = "e.g. 192.168.1.50",
                .max_length = sizeof(selection_.manual_address) - 1,
                .numeric_only = false,
                .min_value = 0,
                .max_value = 0,
                .step = 0,
                .fallback = 0};

            int timeout_seed = selection_.scan_timeout_ms > 0 ? selection_.scan_timeout_ms : kDefaultScanTimeoutMs;
            fields_[FieldScanTimeout] = FieldDefinition{
                .label = "LAN scan timeout (ms)",
                .value = std::to_string(std::clamp(timeout_seed, kMinScanTimeoutMs, kMaxScanTimeoutMs)),
                .hint = std::to_string(kDefaultScanTimeoutMs),
                .max_length = 5,
                .numeric_only = true,
                .min_value = kMinScanTimeoutMs,
                .max_value = kMaxScanTimeoutMs,
                .step = 50,
                .fallback = kDefaultScanTimeoutMs};

            int limit_seed = selection_.scan_result_limit > 0 ? selection_.scan_result_limit : kDefaultScanLimit;
            fields_[FieldScanLimit] = FieldDefinition{
                .label = "Max discovered servers",
                .value = std::to_string(std::clamp(limit_seed, kMinScanLimit, ARMADA_DISCOVERY_MAX_RESULTS)),
                .hint = std::to_string(kDefaultScanLimit),
                .max_length = 2,
                .numeric_only = true,
                .min_value = kMinScanLimit,
                .max_value = ARMADA_DISCOVERY_MAX_RESULTS,
                .step = 1,
                .fallback = kDefaultScanLimit};
        }

        void draw_header(Canvas c, int &row) const
        {
            static constexpr std::string_view title = "Project Armada";
            static constexpr std::string_view subtitle = "LAN Launcher";
            draw_centered(c, row++, title);
            draw_centered(c, row++, subtitle);
        }

        void draw_centered(Canvas c, int row, std::string_view text) const
        {
            int start_x = std::max(0, (c.size.width - static_cast<int>(text.size())) / 2);
            ox::put(c, {.x = start_x, .y = row}, text);
        }

        [[nodiscard]] std::string resolved_value(std::size_t field_index) const
        {
            const auto &field = fields_[field_index];
            if (field.value.empty())
            {
                return field.hint;
            }
            return field.value;
        }

        void draw_fields(Canvas c, int &row) const
        {
            constexpr int margin = 2;
            for (std::size_t i = 0; i < fields_.size(); ++i)
            {
                const auto &field = fields_[i];
                bool focus = is_focus(FocusType::Field, i);
                bool editing = (editing_field_ == static_cast<int>(i));
                std::string value = resolved_value(i);
                if (editing)
                {
                    value.push_back('_');
                }
                std::string line = (focus ? "> " : "  ");
                line += field.label + ": " + value;
                ox::put(c, {.x = margin, .y = row++}, line);
            }
        }

        void draw_actions(Canvas c, int &row) const
        {
            constexpr int margin = 2;
            static constexpr std::array<std::string_view, ActionCount> labels = {
                "Host a new server",
                "Join via LAN scan",
                "Join via manual IP",
                "Quit"};

            for (std::size_t i = 0; i < labels.size(); ++i)
            {
                bool focus = is_focus(FocusType::Action, i);
                std::string detail;
                if (i == ActionScan)
                {
                    detail = " (" + resolved_value(FieldScanTimeout) + " ms, up to " + resolved_value(FieldScanLimit) + ")";
                }
                else if (i == ActionManualJoin && !fields_[FieldManualAddress].value.empty())
                {
                    detail = " [" + fields_[FieldManualAddress].value + "]";
                }
                std::string line = (focus ? "> " : "  ");
                line += std::string{labels[i]} + detail;
                ox::put(c, {.x = margin, .y = row++}, line);
            }
        }

        void draw_instructions(Canvas c, int row) const
        {
            constexpr int margin = 2;
            ox::put(c, {.x = margin, .y = row++}, "↑/↓ or Tab to move • Enter to edit or launch");
            ox::put(c, {.x = margin, .y = row++}, "Esc to quit • ←/→ tweak numeric fields • Backspace edits");
        }

        bool is_focus(FocusType type, std::size_t index) const
        {
            if (focus_items_.empty())
            {
                return false;
            }

            const auto &current = focus_items_[focus_index_];
            return current.type == type && current.index == index;
        }

        void move_focus(int delta)
        {
            if (focus_items_.empty())
            {
                return;
            }

            if (editing_field_ >= 0)
            {
                editing_field_ = -1;
            }

            int count = static_cast<int>(focus_items_.size());
            focus_index_ = (focus_index_ + delta) % count;
            if (focus_index_ < 0)
            {
                focus_index_ += count;
            }
        }

        void activate_current()
        {
            if (focus_items_.empty())
            {
                return;
            }

            const auto &item = focus_items_[focus_index_];
            if (item.type == FocusType::Field)
            {
                editing_field_ = static_cast<int>(item.index);
                return;
            }

            static constexpr std::array<ArmadaTuiAction, ActionCount> action_map = {
                ARMADA_TUI_ACTION_HOST,
                ARMADA_TUI_ACTION_SCAN,
                ARMADA_TUI_ACTION_MANUAL_JOIN,
                ARMADA_TUI_ACTION_QUIT};

            sync_selection_from_state();
            selection_.action = action_map[item.index];
            int code = selection_.action == ARMADA_TUI_ACTION_QUIT ? ARMADA_TUI_STATUS_EXIT_REQUESTED : ARMADA_TUI_STATUS_OK;
            Application::quit(code);
        }

        void adjust_numeric_current(int delta)
        {
            if (focus_items_.empty())
            {
                return;
            }

            const auto &item = focus_items_[focus_index_];
            if (item.type != FocusType::Field)
            {
                return;
            }

            FieldDefinition &field = fields_[item.index];
            if (!field.numeric_only)
            {
                return;
            }

            int step = field.step > 0 ? field.step : 1;
            int value = parse_numeric(item.index);
            value = std::clamp(value + delta * step, field.min_value, field.max_value);
            field.value = std::to_string(value);
        }

        int parse_numeric(std::size_t index) const
        {
            const FieldDefinition &field = fields_[index];
            if (field.value.empty())
            {
                return field.fallback;
            }

            char *end = nullptr;
            long parsed = std::strtol(field.value.c_str(), &end, 10);
            if (end == field.value.c_str())
            {
                return field.fallback;
            }

            parsed = std::clamp(parsed, static_cast<long>(field.min_value), static_cast<long>(field.max_value));
            return static_cast<int>(parsed);
        }

        void handle_edit_key(Key key)
        {
            if (editing_field_ < 0)
            {
                return;
            }

            FieldDefinition &field = fields_[editing_field_];

            auto finish_edit = [this]()
            {
                editing_field_ = -1;
            };

            switch (key)
            {
            case Key::Enter:
                finish_edit();
                return;
            case Key::Tab:
                finish_edit();
                move_focus(+1);
                return;
            case Key::BackTab:
                finish_edit();
                move_focus(-1);
                return;
            case Key::Escape:
                finish_edit();
                return;
            case Key::Backspace:
                if (!field.value.empty())
                {
                    field.value.pop_back();
                }
                return;
            default:
                break;
            }

            char ch = 0;
            if (!key_to_char(key, ch))
            {
                return;
            }
            if (field.numeric_only && !std::isdigit(static_cast<unsigned char>(ch)))
            {
                return;
            }
            if (field.value.size() >= field.max_length)
            {
                return;
            }
            field.value.push_back(ch);
        }

        static bool key_to_char(Key key, char &out)
        {
            auto code = static_cast<char32_t>(key);
            if (code >= 32 && code <= 126)
            {
                out = static_cast<char>(code);
                return true;
            }
            return false;
        }

        void sync_selection_from_state()
        {
            copy_into_buffer(fields_[FieldPlayerName].value, selection_.player_name, sizeof(selection_.player_name));
            copy_into_buffer(fields_[FieldManualAddress].value, selection_.manual_address, sizeof(selection_.manual_address));
            selection_.scan_timeout_ms = parse_numeric(FieldScanTimeout);
            selection_.scan_result_limit = parse_numeric(FieldScanLimit);
        }

    private:
        ArmadaTuiSelection &selection_;
        std::array<FieldDefinition, FieldCount> fields_{};
        std::vector<FocusItem> focus_items_{};
        int focus_index_ = 0;
        int editing_field_ = -1;
    };

} // namespace

extern "C" int armada_tui_launch(ArmadaTuiSelection *selection)
{
    if (!selection)
    {
        return ARMADA_TUI_STATUS_ERROR;
    }

    LauncherWidget launcher{*selection};

    Application app{launcher, ox::Terminal{ox::MouseMode::Move, ox::KeyMode::Raw}};

    // Explicitly set focus to the launcher widget to ensure it receives key events.
    ox::Focus::set(launcher);

    return app.run();
}