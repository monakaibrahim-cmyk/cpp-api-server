#include <dashboard/dashboard.h>
#include <core/cache.h>
#include <server/connection_tracker.h>
#include <core/logger.h>
#include <core/metrics.h>
#include <core/thread_pool.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace api
{

enum LineBits : uint8_t
{
    LINE_NONE = 0,
    LINE_LEFT = 1 << 0,
    LINE_RIGHT = 1 << 1,
    LINE_UP = 1 << 2,
    LINE_DOWN = 1 << 3,
};

static double catmull_rom_spline(double p0, double p1, double p2, double p3,
                                 double t)
{
    double t2 = t * t;
    double t3 = t2 * t;

    return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
                  (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

static std::vector<double> sample_history_smooth(const std::deque<double> &hist,
                                                 int width)
{
    if (width <= 0)
    {
        return {};
    }

    std::vector<double> out(width, 0.0);

    if (hist.empty())
    {
        return out;
    }

    if (hist.size() == 1)
    {
        std::fill(out.begin(), out.end(), hist.front());

        return out;
    }

    int n = static_cast<int>(hist.size());

    for (int col = 0; col < width; ++col)
    {
        double norm_x = static_cast<double>(col) /
                        static_cast<double>(std::max(1, width - 1));
        double real_idx = norm_x * (n - 1);

        int i1 = static_cast<int>(std::floor(real_idx));
        int i2 = std::min(i1 + 1, n - 1);
        int i0 = std::max(0, i1 - 1);
        int i3 = std::min(n - 1, i2 + 1);

        double t = real_idx - i1;
        double val =
            catmull_rom_spline(hist[i0], hist[i1], hist[i2], hist[i3], t);

        out[col] = std::max(0.0, val);
    }

    return out;
}

static std::vector<std::vector<uint8_t>>
build_line_grid(const std::vector<double> &vals, double max_val, int rows,
                int cols)
{
    std::vector<std::vector<uint8_t>> grid(rows, std::vector<uint8_t>(cols, 0));
    std::vector<int> R(cols, rows - 1);

    for (int x = 0; x < cols; ++x)
    {
        double v = (x < static_cast<int>(vals.size())) ? vals[x] : 0.0;
        double norm = max_val > 0.0 ? (v / max_val) : 0.0;

        norm = std::clamp(norm, 0.0, 1.0);

        int r = static_cast<int>(std::round((1.0 - norm) * (rows - 1)));

        R[x] = std::clamp(r, 0, rows - 1);
    }

    for (int x = 0; x < cols; ++x)
    {
        int y = R[x];

        if (x == 0)
        {
            grid[y][x] |= LINE_LEFT;
        }

        if (x < cols - 1)
        {
            int y_next = R[x + 1];

            if (y == y_next)
            {
                grid[y][x] |= LINE_RIGHT;
                grid[y][x + 1] |= LINE_LEFT;
            }
            else if (y > y_next)
            {
                grid[y][x] |= LINE_UP;

                for (int r = y_next + 1; r < y; ++r)
                {
                    grid[r][x] |= (LINE_UP | LINE_DOWN);
                }

                grid[y_next][x] |= (LINE_DOWN | LINE_RIGHT);
                grid[y_next][x + 1] |= LINE_LEFT;
            }
            else
            {
                grid[y][x] |= LINE_DOWN;

                for (int r = y + 1; r < y_next; ++r)
                {
                    grid[r][x] |= (LINE_UP | LINE_DOWN);
                }

                grid[y_next][x] |= (LINE_UP | LINE_RIGHT);
                grid[y_next][x + 1] |= LINE_LEFT;
            }
        }
        else
        {
            grid[y][x] |= LINE_RIGHT;
        }
    }

    return grid;
}

static ftxui::Element
telemetry_dual_lines(std::function<std::vector<double>(int)> data_fn1,
                     double max_val1, ftxui::Color color1,
                     std::function<std::vector<double>(int)> data_fn2,
                     double max_val2, ftxui::Color color2,
                     graph_render_mode mode, const LineGraphGlyphs &glyphs)
{
    return ftxui::canvas(
               20, 16,
               [data_fn1 = std::move(data_fn1), max_val1, color1,
                data_fn2 = std::move(data_fn2), max_val2, color2, mode,
                glyphs](ftxui::Canvas &c)
               {
                   int cw = c.width();
                   int ch = c.height();

                   if (cw <= 0 || ch <= 0)
                   {
                       return;
                   }

                   if (mode == graph_render_mode::modern_curve)
                   {
                       auto vals1 =
                           data_fn1 ? data_fn1(cw) : std::vector<double>{};
                       auto vals2 =
                           data_fn2 ? data_fn2(cw) : std::vector<double>{};

                       for (int x = 0; x < cw; x += 4)
                       {
                           c.DrawPoint(x, ch - 1, true,
                                       ftxui::Color::RGB(45, 55, 65));
                       }

                       if (!vals1.empty())
                       {
                           for (int x = 0; x < cw - 1; ++x)
                           {
                               double v1 = (x < static_cast<int>(vals1.size()))
                                               ? vals1[x]
                                               : 0.0;
                               double v2 =
                                   (x + 1 < static_cast<int>(vals1.size()))
                                       ? vals1[x + 1]
                                       : 0.0;

                               double n1 = std::clamp(
                                   max_val1 > 0.0 ? (v1 / max_val1) : 0.0, 0.0,
                                   1.0);
                               double n2 = std::clamp(
                                   max_val1 > 0.0 ? (v2 / max_val1) : 0.0, 0.0,
                                   1.0);

                               int y1 = std::clamp(
                                   (ch - 2) - static_cast<int>(
                                                  std::round(n1 * (ch - 3))),
                                   0, ch - 1);
                               int y2 = std::clamp(
                                   (ch - 2) - static_cast<int>(
                                                  std::round(n2 * (ch - 3))),
                                   0, ch - 1);

                               c.DrawPointLine(x, y1, x + 1, y2, color1);
                           }
                       }

                       if (!vals2.empty())
                       {
                           for (int x = 0; x < cw - 1; ++x)
                           {
                               double v1 = (x < static_cast<int>(vals2.size()))
                                               ? vals2[x]
                                               : 0.0;
                               double v2 =
                                   (x + 1 < static_cast<int>(vals2.size()))
                                       ? vals2[x + 1]
                                       : 0.0;

                               double n1 = std::clamp(
                                   max_val2 > 0.0 ? (v1 / max_val2) : 0.0, 0.0,
                                   1.0);
                               double n2 = std::clamp(
                                   max_val2 > 0.0 ? (v2 / max_val2) : 0.0, 0.0,
                                   1.0);

                               int y1 = std::clamp(
                                   (ch - 2) - static_cast<int>(
                                                  std::round(n1 * (ch - 3))),
                                   0, ch - 1);
                               int y2 = std::clamp(
                                   (ch - 2) - static_cast<int>(
                                                  std::round(n2 * (ch - 3))),
                                   0, ch - 1);

                               c.DrawPointLine(x, y1, x + 1, y2, color2);
                           }
                       }
                   }
                   else
                   {
                       int cols = cw / 2;
                       int rows = ch / 4;

                       if (cols <= 0 || rows <= 0)
                       {
                           return;
                       }

                       auto vals1 =
                           data_fn1 ? data_fn1(cols) : std::vector<double>{};
                       auto grid1 =
                           build_line_grid(vals1, max_val1, rows, cols);

                       std::vector<std::vector<uint8_t>> grid2;
                       bool has_series2 = (data_fn2 != nullptr);

                       if (has_series2)
                       {
                           auto vals2 = data_fn2(cols);

                           grid2 = build_line_grid(vals2, max_val2, rows, cols);
                       }

                       for (int r = 0; r < rows; ++r)
                       {
                           for (int col = 0; col < cols; ++col)
                           {
                               uint8_t m1 = grid1[r][col];
                               uint8_t m2 = has_series2 ? grid2[r][col] : 0;

                               if (m1 > 0 && m2 > 0)
                               {
                                   const auto &ch_str =
                                       glyphs.for_mask(m1 | m2);

                                   c.DrawText(col * 2, r * 4, ch_str,
                                              ftxui::Color::White);
                               }
                               else if (m1 > 0)
                               {
                                   const auto &ch_str = glyphs.for_mask(m1);

                                   c.DrawText(col * 2, r * 4, ch_str, color1);
                               }
                               else if (m2 > 0)
                               {
                                   const auto &ch_str = glyphs.for_mask(m2);

                                   c.DrawText(col * 2, r * 4, ch_str, color2);
                               }
                           }
                       }
                   }
               }) |
           ftxui::flex;
}

static std::string format_bytes(size_t bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB"};
    double val = static_cast<double>(bytes);
    int u = 0;

    while (val >= 1024.0 && u < 3)
    {
        val /= 1024.0;
        ++u;
    }

    std::ostringstream oss;

    oss.precision(1);
    oss << std::fixed << val << " " << units[u];

    return oss.str();
}

static ftxui::Color severity_color(severity_level lvl)
{
    switch (lvl)
    {
    case severity_level::trace:
    case severity_level::debug:
        return ftxui::Color::GrayDark;
    case severity_level::info:
        return ftxui::Color::Green;
    case severity_level::warning:
        return ftxui::Color::Yellow;
    case severity_level::error:
    case severity_level::fatal:
        return ftxui::Color::Red;
    default:
        return ftxui::Color::White;
    }
}

static ftxui::Color gauge_color(double pct)
{
    if (pct < 60.0)
    {
        return ftxui::Color::Green;
    }

    if (pct < 85.0)
    {
        return ftxui::Color::Yellow;
    }

    return ftxui::Color::Red;
}

static ftxui::Color direction_color(connection_direction dir)
{
    if (dir == connection_direction::incoming)
    {
        return ftxui::Color::Cyan;
    }

    return ftxui::Color::MagentaLight;
}

static ftxui::Color method_color(const std::string &method)
{
    if (method == "GET")
    {
        return ftxui::Color::Green;
    }

    if (method == "POST")
    {
        return ftxui::Color::Cyan;
    }

    if (method == "PUT")
    {
        return ftxui::Color::Yellow;
    }

    if (method == "DELETE")
    {
        return ftxui::Color::Red;
    }

    return ftxui::Color::White;
}

static ftxui::Color status_color(int code, bool in_flight)
{
    if (in_flight)
    {
        return ftxui::Color::Yellow;
    }

    if (code >= 200 && code < 300)
    {
        return ftxui::Color::Green;
    }

    if (code >= 300 && code < 400)
    {
        return ftxui::Color::Cyan;
    }

    if (code >= 400 && code < 500)
    {
        return ftxui::Color::YellowLight;
    }

    if (code >= 500)
    {
        return ftxui::Color::Red;
    }

    return ftxui::Color::White;
}

static std::string status_text(int code, bool in_flight)
{
    if (in_flight)
    {
        return "ACTIVE";
    }

    switch (code)
    {
    case 200:
        return "200 OK";
    case 201:
        return "201 CR";
    case 204:
        return "204 NC";
    case 400:
        return "400 BAD";
    case 401:
        return "401 UNA";
    case 403:
        return "403 FOR";
    case 404:
        return "404 NF";
    case 405:
        return "405 MET";
    case 500:
        return "500 ERR";
    case 502:
        return "502 BAD";
    case 503:
        return "503 UNA";
    default:
        return std::to_string(code);
    }
}

static std::string format_duration_ms(double ms)
{
    std::ostringstream oss;

    oss.precision(1);
    oss << std::fixed << ms << "ms";

    return oss.str();
}

dashboard::dashboard(metrics_collector &metrics, connection_tracker &tracker,
                     log_ring_buffer &log_buffer, lua_engine *lua,
                     graph_render_mode mode, LineGraphGlyphs glyphs)
    : metrics_(metrics), tracker_(tracker), log_buffer_(log_buffer), lua_(lua),
      render_mode_(mode), glyphs_(std::move(glyphs))
{
}

void dashboard::stop() { running_ = false; }

void dashboard::set_render_mode(graph_render_mode mode) { render_mode_ = mode; }

void dashboard::set_glyphs(LineGraphGlyphs glyphs)
{
    glyphs_ = std::move(glyphs);
}

void dashboard::run()
{
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();

    auto renderer = Renderer(
        [&]() -> Element
        {
            auto snapshot = metrics_.get_snapshot();
            auto tracker_stats = tracker_.get_stats();
            auto logs = log_buffer_.snapshot();
            auto errors = log_buffer_.snapshot_errors();

            cpu_history_.push_back(snapshot.cpu_usage_percent);

            while (cpu_history_.size() > maximum_history_points_)
            {
                cpu_history_.pop_front();
            }

            double ram_mb = static_cast<double>(snapshot.memory_rss_bytes) /
                            (1024.0 * 1024.0);

            ram_history_.push_back(ram_mb);

            while (ram_history_.size() > maximum_history_points_)
            {
                ram_history_.pop_front();
            }

            net_rx_history_.push_back(
                static_cast<double>(snapshot.net_rx_bytes_per_sec));

            while (net_rx_history_.size() > maximum_history_points_)
            {
                net_rx_history_.pop_front();
            }

            net_tx_history_.push_back(
                static_cast<double>(snapshot.net_tx_bytes_per_sec));

            while (net_tx_history_.size() > maximum_history_points_)
            {
                net_tx_history_.pop_front();
            }

            request_history_.push_back(tracker_stats.requests_per_second);

            while (request_history_.size() > maximum_history_points_)
            {
                request_history_.pop_front();
            }

            tasks_history_.push_back(
                static_cast<double>(s_thread_pool().active_tasks()));

            while (tasks_history_.size() > maximum_history_points_)
            {
                tasks_history_.pop_front();
            }

            double peak_cpu = 100.0;

            for (double value : cpu_history_)
            {
                if (value > peak_cpu)
                {
                    peak_cpu = value;
                }
            }

            double peak_ram = 64.0;

            for (double value : ram_history_)
            {
                if (value > peak_ram)
                {
                    peak_ram = value;
                }
            }

            double peak_net = 1024.0;

            for (double value : net_rx_history_)
            {
                if (value > peak_net)
                {
                    peak_net = value;
                }
            }

            for (double value : net_tx_history_)
            {
                if (value > peak_net)
                {
                    peak_net = value;
                }
            }

            double peak_load = 5.0;

            for (double value : request_history_)
            {
                if (value > peak_load)
                {
                    peak_load = value;
                }
            }

            double peak_tasks = 5.0;

            for (double value : tasks_history_)
            {
                if (value > peak_tasks)
                {
                    peak_tasks = value;
                }
            }

            auto cpu_percent = snapshot.cpu_usage_percent;

            auto cpu_ram_graph =
                telemetry_dual_lines(
                    [this](int width)
                    { return sample_history_smooth(cpu_history_, width); },
                    peak_cpu, gauge_color(cpu_percent), [this](int width)
                    { return sample_history_smooth(ram_history_, width); },
                    peak_ram, Color::YellowLight, render_mode_, glyphs_) |
                size(HEIGHT, EQUAL, 4);

            auto net_graph =
                telemetry_dual_lines(
                    [this](int width)
                    { return sample_history_smooth(net_rx_history_, width); },
                    peak_net, Color::GreenLight, [this](int width)
                    { return sample_history_smooth(net_tx_history_, width); },
                    peak_net, Color::CyanLight, render_mode_, glyphs_) |
                size(HEIGHT, EQUAL, 4);

            auto tasks_graph =
                telemetry_dual_lines(
                    [this](int width)
                    { return sample_history_smooth(request_history_, width); },
                    peak_load, Color::YellowLight, [this](int width)
                    { return sample_history_smooth(tasks_history_, width); },
                    peak_tasks, Color::MagentaLight, render_mode_, glyphs_) |
                size(HEIGHT, EQUAL, 4);

            auto header = hbox({
                text(" API-cli Telemetry & System Monitor ") | bold |
                    color(Color::Cyan),
                filler(),
                text(" Workers: " +
                     std::to_string(s_thread_pool().thread_count()) + " ") |
                    bold | color(Color::YellowLight),
            });

            auto cpu_ram_panel = vbox({
                hbox({
                    text(" " + glyphs_.horizontal + " CPU ") |
                        bgcolor(Color::RGB(25, 45, 30)) |
                        color(gauge_color(cpu_percent)) | bold,
                    text(" " + std::to_string(static_cast<int>(cpu_percent)) +
                         "% ") |
                        bold,
                    filler(),
                    text(" " + glyphs_.horizontal + " RAM ") |
                        bgcolor(Color::RGB(45, 40, 20)) |
                        color(Color::YellowLight) | bold,
                    text(" " + format_bytes(snapshot.memory_rss_bytes) + " ") |
                        bold,
                }),
                cpu_ram_graph,
                separatorLight(),
                hbox({
                    text("RSS: ") | bold,
                    text(format_bytes(snapshot.memory_rss_bytes)),
                    filler(),
                    text("VSize: ") | bold,
                    text(format_bytes(snapshot.memory_vsize_bytes)),
                }),
                hbox({
                    text("Threads: ") | bold,
                    text(std::to_string(snapshot.thread_count)),
                    filler(),
                    text("FDs: ") | bold,
                    text(std::to_string(snapshot.open_fds)),
                }),
            });

            auto bandwidth_panel = vbox({
                hbox({
                    text(" " + glyphs_.horizontal + " In (RX) ") |
                        bgcolor(Color::RGB(20, 45, 30)) |
                        color(Color::GreenLight) | bold,
                    text(" " + format_bytes(snapshot.net_rx_bytes_per_sec) +
                         "/s ") |
                        bold,
                    filler(),
                    text(" " + glyphs_.horizontal + " Out (TX) ") |
                        bgcolor(Color::RGB(20, 35, 45)) |
                        color(Color::CyanLight) | bold,
                    text(" " + format_bytes(snapshot.net_tx_bytes_per_sec) +
                         "/s ") |
                        bold,
                }),
                net_graph,
                separatorLight(),
                hbox({
                    text("Total Served: ") | bold,
                    text(std::to_string(tracker_stats.total_connections)),
                    filler(),
                    text("State: ") | bold,
                    text("online") | color(Color::GreenLight),
                }),
                hbox({
                    text("Peak RX: ") | dim,
                    text(format_bytes(static_cast<size_t>(peak_net)) + "/s") |
                        dim,
                    filler(),
                    text("Active: ") | bold,
                    text(std::to_string(tracker_stats.active_connections)) |
                        color(Color::Yellow),
                }),
                hbox({
                    text("Total RX: ") | dim,
                    text(format_bytes(snapshot.net_rx_total_bytes)) | dim,
                    filler(),
                    text("Total TX: ") | dim,
                    text(format_bytes(snapshot.net_tx_total_bytes)) | dim,
                }),
            });

            auto cache_statistics = s_cache_engine().get_stats();
            std::ostringstream ratio_stream;

            ratio_stream.precision(1);
            ratio_stream << std::fixed << cache_statistics.hit_ratio_percent
                         << "%";

            auto tasks_panel = vbox({
                hbox({
                    text(" " + glyphs_.horizontal + " HTTP Load ") |
                        bgcolor(Color::RGB(45, 40, 20)) |
                        color(Color::YellowLight) | bold,
                    text(" " +
                         std::to_string(static_cast<int>(
                             tracker_stats.requests_per_second)) +
                         " r/s ") |
                        bold,
                    filler(),
                    text(" " + glyphs_.horizontal + " Tasks ") |
                        bgcolor(Color::RGB(45, 20, 45)) |
                        color(Color::MagentaLight) | bold,
                    text(" " + std::to_string(s_thread_pool().active_tasks()) +
                         " active ") |
                        bold,
                }),
                tasks_graph,
                separatorLight(),
                hbox({
                    text("Workers: ") | bold,
                    text(std::to_string(s_thread_pool().thread_count())),
                    filler(),
                    text("Pending: ") | bold,
                    text(std::to_string(s_thread_pool().pending_tasks())) |
                        color(Color::YellowLight),
                }),
                hbox({
                    text("Done: ") | bold,
                    text(std::to_string(s_thread_pool().completed_tasks())) |
                        color(Color::Green),
                    filler(),
                    text("Cache: ") | bold,
                    text(std::to_string(cache_statistics.items) + " (" +
                         ratio_stream.str() + ")") |
                        color(Color::Cyan),
                }),
            });

            auto top_row = hbox({
                window(text(" CPU & RAM Metrics ") | bold, cpu_ram_panel) |
                    flex,
                window(text(" Bandwidth & Network ") | bold, bandwidth_panel) |
                    flex,
                window(text(" Tasks & Workers Monitor ") | bold, tasks_panel) |
                    flex,
            });

            auto recent_conns = tracker_.get_recent_connections(8);
            Elements conn_elements;

            conn_elements.push_back(hbox({
                text("TIMESTAMP      ") | bold | dim,
                text("DIR   ") | bold,
                text("IP USED                ") | bold,
                text("METHOD  ") | bold,
                text("FULL URL / ENDPOINT") | bold | flex,
                text("STATUS    ") | bold,
                text("LATENCY   ") | bold,
            }));
            conn_elements.push_back(separatorLight());

            if (recent_conns.empty())
            {
                conn_elements.push_back(text("  (listening - no incoming or "
                                             "outgoing connections recorded)") |
                                        color(Color::GrayDark));
            }
            else
            {
                for (const auto &c : recent_conns)
                {
                    std::string ip_used = c.remote_ip;

                    if (c.remote_port > 0)
                    {
                        ip_used += ":" + std::to_string(c.remote_port);
                    }

                    std::string target_url =
                        c.full_url.empty() ? c.url : c.full_url;
                    std::string st = status_text(c.status_code, c.in_flight);
                    std::string dur =
                        c.in_flight ? "-" : format_duration_ms(c.duration_ms);
                    std::string transfer_bytes =
                        c.in_flight ? "-"
                                    : format_bytes(c.received_bytes +
                                                   c.transmitted_bytes);

                    conn_elements.push_back(hbox({
                        text("[" + c.timestamp + "] ") | dim,
                        text(c.direction == connection_direction::incoming
                                 ? "[IN ] "
                                 : "[OUT] ") |
                            color(direction_color(c.direction)) | bold,
                        text(ip_used) | size(WIDTH, EQUAL, 23),
                        text(c.method) | color(method_color(c.method)) | bold |
                            size(WIDTH, EQUAL, 8),
                        text(target_url) | flex,
                        text(st) |
                            color(status_color(c.status_code, c.in_flight)) |
                            bold | size(WIDTH, EQUAL, 10),
                        text(dur) | dim | size(WIDTH, EQUAL, 10),
                        text(transfer_bytes) | dim | size(WIDTH, EQUAL, 10),
                    }));
                }
            }

            auto connections_panel =
                window(text(" Incoming & Outgoing Connections Monitor ") | bold,
                       vbox(conn_elements) | frame | size(HEIGHT, EQUAL, 8));

            Elements log_elements;
            size_t log_display_count = 14;
            size_t log_start = logs.size() > log_display_count
                                   ? logs.size() - log_display_count
                                   : 0;

            for (size_t i = log_start; i < logs.size(); ++i)
            {
                const auto &e = logs[i];
                std::string loc = e.file.empty()
                                      ? ""
                                      : (e.file + ":" + std::to_string(e.line));
                std::string fn = e.function.empty() ? "" : (e.function + "()");

                log_elements.push_back(hbox({
                    text("[" + e.timestamp + "] ") | dim,
                    text("[" +
                         std::string(
                             e.level == severity_level::info      ? "INFO "
                             : e.level == severity_level::warning ? "WARN "
                             : e.level == severity_level::error   ? "ERROR"
                             : e.level == severity_level::fatal   ? "FATAL"
                                                                  : "DEBUG") +
                         "] ") |
                        color(severity_color(e.level)) | bold,
                    text("[" + e.channel + "] ") | color(Color::Cyan) | bold,
                    text("[" + loc + " " + fn + "] ") | dim,
                    text(e.message) | color(Color::White),
                }));
            }

            if (log_elements.empty())
            {
                log_elements.push_back(text("  (no logs yet)") |
                                       color(Color::GrayDark));
            }

            auto logs_panel =
                window(text(" System Logs (All Channels) ") | bold,
                       vbox(log_elements) | frame | flex) |
                flex;

            Elements err_elements;
            size_t err_display_count = 5;
            size_t err_start = errors.size() > err_display_count
                                   ? errors.size() - err_display_count
                                   : 0;

            for (size_t i = err_start; i < errors.size(); ++i)
            {
                const auto &e = errors[i];
                std::string loc = e.file.empty()
                                      ? ""
                                      : (e.file + ":" + std::to_string(e.line));
                std::string fn = e.function.empty() ? "" : (e.function + "()");

                err_elements.push_back(hbox({
                    text("[" + e.timestamp + "] ") | dim,
                    text("[" +
                         std::string(e.level == severity_level::warning
                                         ? "WARN "
                                         : "ERROR") +
                         "] ") |
                        color(severity_color(e.level)) | bold,
                    text("[" + e.channel + "] ") | color(Color::YellowLight) |
                        bold,
                    text("[" + loc + " " + fn + "] ") | dim,
                    text(e.message) | color(Color::RedLight) | bold,
                }));
            }

            if (err_elements.empty())
            {
                err_elements.push_back(hbox({
                    text("  ✔ ") | color(Color::GreenLight) | bold,
                    text("No errors or warnings detected") |
                        color(Color::GrayLight),
                }));
            }

            auto errors_panel =
                window(text(" Errors & Alerts Monitor ") | bold,
                       vbox(err_elements) | frame | size(HEIGHT, EQUAL, 5));

            auto footer = hbox({
                text(" [q / Esc] Exit Monitor") | bold |
                    color(Color::GrayLight),
                filler(),
                text(" Live Telemetry Active ") | color(Color::Green),
            });

            return vbox({
                       header,
                       separatorLight(),
                       top_row,
                       connections_panel,
                       logs_panel,
                       errors_panel,
                       separatorLight(),
                       footer,
                   }) |
                   border;
        });

    auto with_quit = CatchEvent(renderer,
                                [&](Event event) -> bool
                                {
                                    if (event == Event::Character('q') ||
                                        event == Event::Character('Q') ||
                                        event == Event::Escape)
                                    {
                                        screen.Exit();

                                        return true;
                                    }

                                    return false;
                                });

    std::thread refresher(
        [&]
        {
            while (running_.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                screen.PostEvent(Event::Custom);
            }
        });

    screen.Loop(with_quit);
    running_ = false;

    refresher.join();
}

} // namespace api
