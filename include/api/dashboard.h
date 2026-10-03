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

/**
 * @brief Telemetry chart interpolation and rendering algorithm.
 */
enum class graph_render_mode
{
    /** @brief Smooth Catmull-Rom cubic spline interpolation rendered with
       curved Unicode box-drawing glyphs. */
    modern_curve,

    /** @brief Orthogonal stepped line segments rendered with standard sharp
       Unicode box-drawing corners. */
    box_lines
};

/**
 * @brief Unicode and ASCII character palette used to draw continuous 2D
 * telemetry curves in terminal cells.
 *
 * @details Telemetry charts project continuous time-series data points onto a
 * discrete character grid. Each character cell determines which directional
 * neighbors (LEFT, RIGHT, UP, DOWN) connect through it, computing a 4-bit
 * bitmask to resolve the optimal box-drawing glyph.
 *
 * Bitmask layout:
 * - Bit 0 (0x01): Left connection
 * - Bit 1 (0x02): Right connection
 * - Bit 2 (0x04): Up connection
 * - Bit 3 (0x08): Down connection
 */
struct LineGraphGlyphs
{
    /** @brief Character string representing an empty grid cell. */
    std::string empty = " ";

    /** @brief Character string representing a horizontal line segment. */
    std::string horizontal = "─";

    /** @brief Character string representing a vertical line segment. */
    std::string vertical = "│";

    /** @brief Character string connecting bottom to right (upper-left
     * curve/corner). */
    std::string up_left = "╭";

    /** @brief Character string connecting bottom to left (upper-right
     * curve/corner). */
    std::string up_right = "╮";

    /** @brief Character string connecting top to right (lower-left
     * curve/corner). */
    std::string down_left = "╰";

    /** @brief Character string connecting top to left (lower-right
     * curve/corner). */
    std::string down_right = "╯";

    /** @brief 4-way cross intersection character string. */
    std::string cross = "┼";

    /** @brief Tee character pointing upward. */
    std::string tee_up = "┴";

    /** @brief Tee character pointing downward. */
    std::string tee_down = "┬";

    /** @brief Tee character pointing leftward. */
    std::string tee_left = "┤";

    /** @brief Tee character pointing rightward. */
    std::string tee_right = "├";

    /**
     * @brief Resolves the box-drawing glyph string for a 4-bit neighbor
     * connectivity bitmask.
     *
     * @param[in] mask 4-bit bitmask combining connection flags (LEFT=1,
     * RIGHT=2, UP=4, DOWN=8).
     * @return const std::string& Reference to the appropriate static glyph
     * string.
     */
    const std::string &for_mask(uint8_t mask) const
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

    /**
     * @brief Factory creating curved rounded Unicode box-drawing glyphs
     * (default).
     *
     * @return LineGraphGlyphs Initialized glyph set.
     */
    static LineGraphGlyphs curved()
    {
        LineGraphGlyphs glyphs;

        glyphs.empty = " ";
        glyphs.horizontal = "─";
        glyphs.vertical = "│";
        glyphs.up_left = "╭";
        glyphs.up_right = "╮";
        glyphs.down_left = "╰";
        glyphs.down_right = "╯";
        glyphs.cross = "┼";
        glyphs.tee_up = "┴";
        glyphs.tee_down = "┬";
        glyphs.tee_left = "┤";
        glyphs.tee_right = "├";

        return glyphs;
    }

    /**
     * @brief Factory creating rectangular orthogonal Unicode box-drawing
     * glyphs.
     *
     * @return LineGraphGlyphs Initialized glyph set.
     */
    static LineGraphGlyphs sharp()
    {
        LineGraphGlyphs glyphs;

        glyphs.empty = " ";
        glyphs.horizontal = "─";
        glyphs.vertical = "│";
        glyphs.up_left = "┌";
        glyphs.up_right = "┐";
        glyphs.down_left = "└";
        glyphs.down_right = "┘";
        glyphs.cross = "┼";
        glyphs.tee_up = "┴";
        glyphs.tee_down = "┬";
        glyphs.tee_left = "┤";
        glyphs.tee_right = "├";

        return glyphs;
    }

    /**
     * @brief Factory creating standard 7-bit ASCII glyphs for legacy terminals.
     *
     * @return LineGraphGlyphs Initialized glyph set.
     */
    static LineGraphGlyphs ascii()
    {
        LineGraphGlyphs glyphs;

        glyphs.empty = " ";
        glyphs.horizontal = "-";
        glyphs.vertical = "|";
        glyphs.up_left = "/";
        glyphs.up_right = "\\";
        glyphs.down_left = "\\";
        glyphs.down_right = "/";
        glyphs.cross = "+";
        glyphs.tee_up = "+";
        glyphs.tee_down = "+";
        glyphs.tee_left = "+";
        glyphs.tee_right = "+";

        return glyphs;
    }

    /**
     * @brief Factory creating solid block glyphs for high-contrast viewing.
     *
     * @return LineGraphGlyphs Initialized glyph set.
     */
    static LineGraphGlyphs blocks()
    {
        LineGraphGlyphs glyphs;

        glyphs.empty = " ";
        glyphs.horizontal = "▄";
        glyphs.vertical = "█";
        glyphs.up_left = "█";
        glyphs.up_right = "█";
        glyphs.down_left = "█";
        glyphs.down_right = "█";
        glyphs.cross = "█";
        glyphs.tee_up = "█";
        glyphs.tee_down = "█";
        glyphs.tee_left = "█";
        glyphs.tee_right = "█";

        return glyphs;
    }
};

/// Backward compatibility alias
using line_graph_glyphs = LineGraphGlyphs;

/**
 * @brief Interactive terminal user interface telemetry dashboard powered by
 * FTXUI.
 *
 * @details Renders full-screen real-time terminal graphs and telemetry monitors
 * for:
 * - CPU utilization percentage and RSS/Virtual memory consumption.
 * - Inbound and outbound network bandwidth throughput (RX/TX bytes per second).
 * - HTTP request throughput (requests per second) and active concurrent
 * connections.
 * - Thread pool worker utilization and pending queue depth.
 * - Streaming recent application logs and error buffers.
 *
 * Example C++ usage:
 * @code{.cpp}
 * api::dashboard dash(
 *     metrics_collector_instance,
 *     connection_tracker_instance,
 *     api::get_log_ring_buffer(),
 *     &lua_engine_instance,
 *     api::graph_render_mode::modern_curve,
 *     api::LineGraphGlyphs::curved()
 * );
 * dash.run(); // Blocks until user presses 'q' or sends interrupt
 * @endcode
 */
class dashboard
{
  public:
    /**
     * @brief Constructs the terminal dashboard bound to active system data
     * collectors.
     *
     * @param[in,out] metrics_collector_instance Host system metrics collector.
     * @param[in,out] connection_tracker_instance Active connection auditing
     * tracker.
     * @param[in,out] log_ring_buffer_instance In-memory logging ring buffer.
     * @param[in] lua_engine_instance Optional pointer to Lua script engine.
     * @param[in] render_mode Curve rendering algorithm (@ref
     * graph_render_mode).
     * @param[in] line_glyphs Box-drawing character set (@ref LineGraphGlyphs).
     */
    dashboard(metrics_collector &metrics_collector_instance,
              connection_tracker &connection_tracker_instance,
              log_ring_buffer &log_ring_buffer_instance,
              lua_engine *lua_engine_instance = nullptr,
              graph_render_mode render_mode = graph_render_mode::modern_curve,
              LineGraphGlyphs line_glyphs = LineGraphGlyphs::curved());

    /**
     * @brief Enters the interactive FTXUI fullscreen rendering loop.
     *
     * @details Hijacks terminal input and alternate screen buffer until
     * interrupted by user keystroke ('q', 'Ctrl-C') or programmatic invocation
     * of @ref stop.
     */
    void run();

    /**
     * @brief Signals the dashboard rendering loop to exit cleanly and restore
     * the terminal.
     */
    void stop();

    /**
     * @brief Changes the telemetry curve rendering interpolation mode at
     * runtime.
     *
     * @param[in] render_mode New rendering mode.
     */
    void set_render_mode(graph_render_mode render_mode);

    /**
     * @brief Updates the box-drawing glyph character set.
     *
     * @param[in] line_glyphs New glyph character palette.
     */
    void set_glyphs(LineGraphGlyphs line_glyphs);

  private:
    metrics_collector &metrics_;
    connection_tracker &tracker_;
    log_ring_buffer &log_buffer_;
    lua_engine *lua_;
    std::atomic<bool> running_{true};
    graph_render_mode render_mode_;
    LineGraphGlyphs glyphs_;

    std::deque<double> cpu_history_;
    std::deque<double> ram_history_;
    std::deque<double> net_rx_history_;
    std::deque<double> net_tx_history_;
    std::deque<double> request_history_;
    std::deque<double> tasks_history_;
    static constexpr size_t maximum_history_points_ = 120;
};

} // namespace api
