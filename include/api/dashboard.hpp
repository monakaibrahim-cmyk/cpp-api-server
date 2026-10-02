#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace api
{

class metrics_collector;
class connection_tracker;
class log_ring_buffer;
class lua_engine;

enum class graph_render_mode
{
    modern_curve,
    box_lines,
};

struct line_graph_glyphs
{
    std::string empty = " ";
    std::string horizontal = "─";
    std::string vertical = "│";
    std::string up_left = "╭";
    std::string up_right = "╮";
    std::string down_left = "╰";
    std::string down_right = "╯";
    std::string cross = "┼";
    std::string tee_up = "┴";
    std::string tee_down = "┬";
    std::string tee_left = "┤";
    std::string tee_right = "├";

    const std::string& for_mask(uint8_t mask) const
    {
        switch (mask & 0x0F)
        {
        case 1:
        case 2:
        case 3:
            return horizontal;
        case 4:
        case 8:
        case 12:
            return vertical;
        case 5:
            return down_right;
        case 6:
            return down_left;
        case 7:
            return tee_up;
        case 9:
            return up_right;
        case 10:
            return up_left;
        case 11:
            return tee_down;
        case 13:
            return tee_left;
        case 14:
            return tee_right;
        case 15:
            return cross;
        default:
            return empty;
        }
    }

    static line_graph_glyphs curved()
    {
        line_graph_glyphs g;

        g.empty = " ";
        g.horizontal = "─";
        g.vertical = "│";
        g.up_left = "╭";
        g.up_right = "╮";
        g.down_left = "╰";
        g.down_right = "╯";
        g.cross = "┼";
        g.tee_up = "┴";
        g.tee_down = "┬";
        g.tee_left = "┤";
        g.tee_right = "├";

        return g;
    }

    static line_graph_glyphs sharp()
    {
        line_graph_glyphs g;

        g.empty = " ";
        g.horizontal = "─";
        g.vertical = "│";
        g.up_left = "┌";
        g.up_right = "┐";
        g.down_left = "└";
        g.down_right = "┘";
        g.cross = "┼";
        g.tee_up = "┴";
        g.tee_down = "┬";
        g.tee_left = "┤";
        g.tee_right = "├";

        return g;
    }

    static line_graph_glyphs ascii()
    {
        line_graph_glyphs g;

        g.empty = " ";
        g.horizontal = "-";
        g.vertical = "|";
        g.up_left = "/";
        g.up_right = "\\";
        g.down_left = "\\";
        g.down_right = "/";
        g.cross = "+";
        g.tee_up = "+";
        g.tee_down = "+";
        g.tee_left = "+";
        g.tee_right = "+";

        return g;
    }

    static line_graph_glyphs blocks()
    {
        line_graph_glyphs g;

        g.empty = " ";
        g.horizontal = "▄";
        g.vertical = "█";
        g.up_left = "█";
        g.up_right = "█";
        g.down_left = "█";
        g.down_right = "█";
        g.cross = "█";
        g.tee_up = "█";
        g.tee_down = "█";
        g.tee_left = "█";
        g.tee_right = "█";

        return g;
    }
};

class dashboard
{
public:
    dashboard(
        metrics_collector& metrics,
        connection_tracker& tracker,
        log_ring_buffer& log_buffer,
        lua_engine* lua = nullptr,
        graph_render_mode mode = graph_render_mode::modern_curve,
        line_graph_glyphs glyphs = line_graph_glyphs::curved()
    );

    void run();
    void stop();

    void set_render_mode(graph_render_mode mode);
    void set_glyphs(line_graph_glyphs glyphs);

private:
    metrics_collector& metrics_;
    connection_tracker& tracker_;
    log_ring_buffer& log_buffer_;
    lua_engine* lua_;
    std::atomic<bool> running_{true};
    graph_render_mode render_mode_;
    line_graph_glyphs glyphs_;

    std::deque<double> cpu_history_;
    std::deque<double> ram_history_;
    std::deque<double> net_rx_history_;
    std::deque<double> net_tx_history_;
    std::deque<double> req_history_;
    std::deque<double> tasks_history_;
    static constexpr size_t max_history_points_ = 120;
};

} // namespace api
