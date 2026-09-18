/** --------------------------------------------------------------------------------------------------------- Internal logging implementation
 * @file logging.cpp
 * @brief Internal implementation of logging utilities and global logging context.
 */
#include <cstdint>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <iomanip>
#include <cmath>
#include <functional>
#include <chrono>
#include <vector>
#include <algorithm>
#include <random>
#include <cstdlib>
#include <deque>
#include <queue>
#include <unordered_map>
#include <logging.hpp>
#if defined(_WIN32) || defined(_WIN64)
    #define LOGGER_PLATFORM_WINDOWS 1
    #define LOGGER_PLATFORM_POSIX 0
    #include <io.h>
    #include <windows.h>
    #define LOGGER_ISATTY(fd) _isatty(fd)
    #define LOGGER_FILENO(f) _fileno(f)
#else
    #define LOGGER_PLATFORM_WINDOWS 0
    #define LOGGER_PLATFORM_POSIX 1
    #include <sys/ioctl.h>
    #include <unistd.h>
    #include <termios.h>
    #ifdef B0
    #undef B0
    #endif
    #define LOGGER_ISATTY(fd) isatty(fd)
    #define LOGGER_FILENO(f) fileno(f)
#endif
#if defined(__APPLE__) || defined(__linux__)
    #include <execinfo.h>  // For backtrace support
#endif
#include <ctime>
#include <cstdlib>
#include <csignal>
#include <unordered_set>
#include <loggingutils.hpp>

namespace threadsafe_logger {
using namespace logging;
class InternalLogger;
Logger& Logger::instance() {
    static Logger logger_instance;
    return logger_instance;
}
class Logger::Impl {
private:
    static Logger& instance() {
        static Logger logger_instance;
        return logger_instance;
    }
    static LogLevel& level() {
        static LogLevel current_level = LogLevel::INFO;
        return current_level;
    }
    static uint64_t getHighPrecisionTimestamp();
    static uint64_t getMicrosecondTimestamp();
    static void RedrawAllProgressBars();
    static std::atomic<uint32_t>& progress_creation_counter();
    static std::function<void(float)> log_progress(const std::string& header);
    static void install_uncaught_logger();
    static void UpdateProgressBar(const ProgressBar& bar);
    friend class Logger;
    friend class ::threadsafe_logger::InternalLogger;
    friend class ModelPicker;
};
uint64_t Logger::get_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
uint64_t Logger::Impl::getHighPrecisionTimestamp() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count(); }
uint64_t Logger::Impl::getMicrosecondTimestamp() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
void Logger::Impl::UpdateProgressBar(const ProgressBar& bar) {
    auto& bars = GlobalLoggingContext::instance().progress_bars();
    int pos_from_bottom = 0;
    for (const auto& [id, other] : bars) {
        if (other.creation_order < bar.creation_order) {
            pos_from_bottom++;
        }
    }
    int lines_up = pos_from_bottom + 1;
    std::string content = RenderProgressBarLine(bar);
    std::ostringstream oss;
    if (lines_up > 0) {
        oss << "\033[" << lines_up << "A";
    }
    oss << "\033[2K\r" << content;
    if (lines_up > 0) {
        oss << "\033[" << lines_up << "B";
    }
    oss << "\r";
    GlobalLoggingContext::stdout_lock();
    std::cout << oss.str();
    std::cout.flush();
    GlobalLoggingContext::stdout_unlock();
}
void Logger::Impl::RedrawAllProgressBars() {
    std::unique_lock lock(GlobalLoggingContext::instance().progress_mutex());
    std::vector<ProgressBar*> sorted;
    for (auto& [id, bar] : GlobalLoggingContext::instance().progress_bars()) {
        sorted.push_back(&bar);
    }
    std::sort(sorted.begin(), sorted.end(), [](const ProgressBar* a, const ProgressBar* b) {
        return a->creation_order > b->creation_order;
    });
    for (auto* bar : sorted) {
        UpdateProgressBar(*bar);
    }
}
std::atomic<uint32_t>& Logger::Impl::progress_creation_counter() {
    static std::atomic<uint32_t> counter{0};
    return counter;
}
std::function<void(float)> Logger::Impl::log_progress(const std::string& header) {
    std::string random_string = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string random_id;
    for (int i = 0; i < 32; i++) {
        random_id += random_string[rand() % random_string.length()];
    }
    uint64_t order = progress_creation_counter().fetch_add(1, std::memory_order_relaxed);
    {
        int term_width = get_terminal_width();
        int bar_width = term_width - static_cast<int>(header.length()) - 55;
        if (bar_width < 10) {
            bar_width = 10;
        }
        std::string timestamp = GlobalLoggingContext::get_timestamp();
        std::unique_lock lock(GlobalLoggingContext::instance().progress_mutex());
        ProgressBar bar = {
            random_id,
            header,
            timestamp,
            0.0f,
            bar_width,
            0,
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()),
            order
        };
        GlobalLoggingContext::instance().progress_bars()[random_id] = bar;
        GlobalLoggingContext::stdout_lock();
        std::cout << "\n";
        std::cout.flush();
        GlobalLoggingContext::stdout_unlock();
    }
    RedrawAllProgressBars();
    return [random_id](float progress) {
        std::unique_lock lock(GlobalLoggingContext::instance().progress_mutex());
        auto it = GlobalLoggingContext::instance().progress_bars().find(random_id);
        if (it == GlobalLoggingContext::instance().progress_bars().end()) {
            return;
        }
        it->second.progress = progress;
        if (progress >= 1.0f) {
            int pos_from_bottom = 0;
            auto& bars = GlobalLoggingContext::instance().progress_bars();
            for (const auto& [id, other] : bars) {
                if (other.creation_order < it->second.creation_order) {
                    pos_from_bottom++;
                }
            }
            int lines_up = pos_from_bottom + 1;
            std::string completed = RenderProgressBarLine(it->second);
            bars.erase(it);
            int remaining = static_cast<int>(bars.size());
            lock.unlock();
            GlobalLoggingContext::stdout_lock();
            std::ostringstream oss;
            if (lines_up > 0) {
                oss << "\033[" << lines_up << "A";
            }
            oss << "\033[2K" << completed << "\n";
            oss << "\033[M";
            if (lines_up - 1 > 0) {
                oss << "\033[" << (lines_up - 1) << "B";
            }
            oss << "\r";
            std::cout << oss.str();
            std::cout.flush();
            GlobalLoggingContext::stdout_unlock();
            if (remaining > 0) {
                RedrawAllProgressBars();
            }
        }
        else {
            UpdateProgressBar(it->second);
        }
    };
}
/** --------------------------------------------------------------------------------------------------------- log_message Method
 * @brief Logs a message with the specified log level. This method handles formatting, coloring, and
 * routing of the log message to the appropriate output streams (stdout, stderr, additional log stream).
 * It also supports JSON formatting for structured logging if enabled.
 * @param level The log level of the message (e.g., INFO, WARN, ERROR)
 * @param message The message to log
 */
void GlobalLoggingContext::log_message(Logger::LogLevel level, const std::string& message) {
    const bool raw_output = level == Logger::LogLevel::RAW;
    if (!raw_output && GlobalLoggingContext::log_format_json().load(std::memory_order_acquire)) {
        std::string line;
        line.reserve(160 + message.size() + GlobalLoggingContext::component().size() +
            GlobalLoggingContext::instance_name().size());
        line += "{\"schema_version\":1,\"timestamp\":\"";
        line += GlobalLoggingContext::get_iso8601_timestamp();
        line += "\",\"level\":\"";
        line += LogLevelName(level);
        line += "\",\"component\":\"";
        GlobalLoggingContext::json_escape_into(GlobalLoggingContext::component(), line);
        line += "\",\"instance\":\"";
        GlobalLoggingContext::json_escape_into(GlobalLoggingContext::instance_name(), line);
        line += "\",\"message\":\"";
        GlobalLoggingContext::json_escape_into(message, line);
        line += "\"}\n";
        std::ostream* json_out = GlobalLoggingContext::stdout_stream().load(std::memory_order_acquire);
        std::ostream* json_additional_out = GlobalLoggingContext::addtl_log_stream().load(
            std::memory_order_acquire);
        if (json_out != nullptr) {
            GlobalLoggingContext::stdout_lock();
            (*json_out) << line;
            json_out->flush();
            GlobalLoggingContext::stdout_unlock();
        }
        if (json_additional_out != nullptr) {
            (*json_additional_out) << line;
            json_additional_out->flush();
        }
        return;
    }
    if (!raw_output && GlobalLoggingContext::is_claude()) {
        std::stringstream oss;
        oss << "threadsafe_logger:" << (level == Logger::LogLevel::ERROR ? "error" :
            (level == Logger::LogLevel::WARN ? "warn" : (level == Logger::LogLevel::INFO ? "info" :
            (level == Logger::LogLevel::DEBUG ? "debug" : "trace")))) << ": " << message << "\n";
        oss << GlobalLoggingContext::get_timestamp() << " ";
        GlobalLoggingContext::stdout_lock();
        std::cout << oss.str();
        std::cout.flush();
        GlobalLoggingContext::stdout_unlock();
        return;
    }
    if (GlobalLoggingContext::global_log_level() > level) {
        return;
    }
    std::stringstream oss;
    std::stringstream oss_plain;
    const bool stdout_tty = stdout_is_tty();
    const bool colored_output = GlobalLoggingContext::colored_output().load(
        std::memory_order_acquire);
    if (stdout_tty && colored_output && !raw_output) {
        oss << TTYGREY << GlobalLoggingContext::get_timestamp();
        switch (level) {
            case Logger::LogLevel::INFO:    oss << INFO_LOG_STANZA; break;
            case Logger::LogLevel::WARN:    oss << WARN_LOG_STANZA; break;
            case Logger::LogLevel::ERROR:   oss << ERROR_LOG_STANZA; break;
            case Logger::LogLevel::DEBUG:   oss << DEBUG_LOG_STANZA; break;
            case Logger::LogLevel::TRACE:   oss << TRACE_LOG_STANZA; break;
            case Logger::LogLevel::RAW:     break;
            default:                oss << " [UNKWN] "; break;
        }
        oss << " " << message << TTYRESET << "\n";
    }
    if (raw_output) {
        oss_plain << message << "\n";
    } else {
        oss_plain << GlobalLoggingContext::get_timestamp();
        switch (level) {
            case Logger::LogLevel::INFO:    oss_plain << " [INFO] "; break;
            case Logger::LogLevel::WARN:    oss_plain << " [WARN] "; break;
            case Logger::LogLevel::ERROR:   oss_plain << " [ERROR]"; break;
            case Logger::LogLevel::DEBUG:   oss_plain << " [DEBUG]"; break;
            case Logger::LogLevel::TRACE:   oss_plain << " [TRACE]"; break;
            case Logger::LogLevel::RAW:     break;
            default:                oss_plain << " [UNKWN]"; break;
        }
        oss_plain << " " << message << "\n";
    }
    const std::string plain_line = oss_plain.str();
    const std::string console_line = stdout_tty && colored_output && !raw_output
        ? oss.str() : plain_line;
    int lines = 0;
    int char_count = 0;
    int term_width = get_terminal_width();
    for (char c : console_line) {
        char_count++;
        if (char_count % term_width == 0) {
            lines++;
        }
        if (c == '\n') {
            lines++;
            char_count = 0;
        }
    }
    std::vector<std::string> line_buffers;
    std::string line_buffer;
    line_buffer.resize(term_width, ' ');
    line_buffer[term_width - 1] = 'E';    line_buffer.push_back('\n');
    line_buffers.resize(lines + 1, line_buffer);
    std::ostream* out_ptr_ = GlobalLoggingContext::stdout_stream().load(std::memory_order_acquire);
    std::ostream* additional_out_ptr = GlobalLoggingContext::addtl_log_stream().load(
        std::memory_order_acquire);
    if (additional_out_ptr != nullptr) {
        (*additional_out_ptr) << plain_line;
        additional_out_ptr->flush();
    }
    if (GlobalLoggingContext::log_message_route_to_compositor(console_line)) {
        return;
    }
    std::stringstream linebumps;
    if (out_ptr_ != nullptr) {
        std::lock_guard<std::mutex> plock(GlobalLoggingContext::instance().progress_mutex());
        auto& bars = GlobalLoggingContext::instance().progress_bars();
        size_t n_bars = bars.size();
        if (n_bars > 0) {
            std::ostringstream out;
            if (n_bars > 0) {
                for (size_t i = 0; i < n_bars; ++i) {
                    linebumps << line_buffer;
                }
            }
            for (size_t i = 0; i < n_bars + static_cast<size_t>(lines) + 1; ++i) {
                out << "\033[1A"; }
            for (size_t i = 0; i < n_bars + static_cast<size_t>(lines); ++i) {
                out << line_buffers[i];
            }
            for (size_t i = 0; i < n_bars + static_cast<size_t>(lines); ++i) {
                out << "\033[1A";
            }
            out << "\033[2K" << console_line;
            std::vector<ProgressBar*> sorted;
            for (auto& [id, bar] : bars) {
                sorted.push_back(&bar);
            }
            std::sort(sorted.begin(), sorted.end(), [](const ProgressBar* a, const ProgressBar* b) {
                return a->creation_order > b->creation_order;
            });
            for (auto* bar : sorted) {
                out << "\033[2K" << RenderProgressBarLine(*bar) << "\n";
            }
            GlobalLoggingContext::stdout_lock();
            (*out_ptr_) << linebumps.str();
            (*out_ptr_) << out.str();
            out_ptr_->flush();
            GlobalLoggingContext::stdout_unlock();
            GlobalLoggingContext::stdout_current_line() += lines;
        }
        else {
            GlobalLoggingContext::stdout_lock();
            (*out_ptr_) << console_line;
            out_ptr_->flush();
            GlobalLoggingContext::stdout_unlock();
            GlobalLoggingContext::stdout_current_line() += lines;
        }
    }
}
void Logger::install_uncaught_logger() {
    Impl::install_uncaught_logger();
}
void Logger::Impl::install_uncaught_logger() {
    std::set_terminate([]() noexcept {
        try {
            if (const std::exception_ptr current = std::current_exception()) std::rethrow_exception(current);
            GlobalLoggingContext::log_message(Logger::LogLevel::ERROR, "terminate called without an active exception");
        } catch (const Exception& error) {
            GlobalLoggingContext::log_message(Logger::LogLevel::ERROR, std::string("uncaught exception: ") + error.what());
#if ENABLE_STACKTRACE
            error.dump_stacktrace();
#endif
        } catch (const std::exception& error) {
            GlobalLoggingContext::log_message(Logger::LogLevel::ERROR, std::string("uncaught standard exception: ") + error.what());
        } catch (...) {
            GlobalLoggingContext::log_message(Logger::LogLevel::ERROR, "uncaught non-standard exception");
        }
        std::abort();
    });
}
Logger::Logger() : impl_(nullptr), level_(LogLevel::INFO) {}
Logger::LogLevel& Logger::level() {
    return Logger::Impl::level();
}
void Logger::log_message(LogLevel level, const std::string& message) {
    GlobalLoggingContext::log_message(level, message);
}
std::string& Logger::thread_context() {
    return GlobalLoggingContext::thread_context();
}
std::string& Logger::component() {
    return GlobalLoggingContext::component();
}
std::string& Logger::instance_name() {
    return GlobalLoggingContext::instance_name();
}
std::atomic<std::ostream*>& Logger::stdout_stream() {
    return GlobalLoggingContext::stdout_stream();
}
std::atomic<std::ostream*>& Logger::stderr_stream() {
    return GlobalLoggingContext::stderr_stream();
}
std::atomic<std::ostream*>& Logger::additional_stream() {
    return GlobalLoggingContext::addtl_log_stream();
}
std::atomic<bool>& Logger::log_format_json() {
    return GlobalLoggingContext::log_format_json();
}
std::string Logger::get_iso8601_timestamp(uint64_t now_ms) {
    return GlobalLoggingContext::get_iso8601_timestamp(now_ms);
}
std::string Logger::format_duration(uint64_t duration_ms) {
    return GlobalLoggingContext::format_duration(duration_ms);
}
bool Logger::stdout_is_tty() {
    return logging::stdout_is_tty();
}
std::function<void(float)> Logger::progress(const std::string& header) {
    if (!stdout_is_tty()) return [](float) {};
    return Impl::log_progress(header);
}
/** --------------------------------------------------------------------------------------------------------- Terminal Screen Draw
 * @brief Compose every sprite owned by this screen into its `rows`/`colors`
 * backing grid. Called by the Terminal singleton before flipping the
 * rendered screen to the front buffer.
 */
bool TerminalScreen::draw() {
    std::vector<SpriteBase*> sprites;
    {
        std::shared_lock<std::shared_mutex> lock(sprites_mutex_);
        std::shared_lock<std::shared_mutex> log_lock(log_messages_mutex_);
        std::shared_lock<std::shared_mutex> bar_lock(progress_bars_mutex_);
        sprites.reserve(owned_sprites_.size() + log_messages_.size() + progress_bars_.size());
        for (const auto& log_msg : log_messages_) {
            sprites.push_back(static_cast<SpriteBase*>(log_msg.get()));
        }
        for (const auto& sprite : owned_sprites_) {
            sprites.push_back(sprite.get());
        }
        for (const auto& bar : progress_bars_) {
            sprites.push_back(static_cast<SpriteBase*>(bar.get()));
        }
    }
    for (int r = 0; r < size.height; ++r) {
        for (int c = 0; c < size.width; ++c) {
            rows[r][c] = 0x20u;
            colors[r][c] = TerminalColors::TRANSPARENT;
        }
    }
    bool still_animating = false;
    const int inset = has_border_ ? 1 : 0;
    for (SpriteBase* sprite : sprites) {
        const bool active = sprite->advance_frame(epoch_ms);
        still_animating = still_animating || active;
        Terminal::draw_sprite(sprite, this, inset, inset);
    }
    if (has_border_) {
        paint_border();
    }
    return still_animating;
}
bool Terminal::LogMessageQueueSprite::advance_frame(uint64_t epoch_ms) {
    bool resized = false;
    if (parent_ != nullptr) {
        const TerminalSize interior = parent_->interior_size();
        const TerminalSize want(
            std::max(0, static_cast<int>(interior.width)
                - parent_margin_right_),
            std::max(0, static_cast<int>(interior.height)
                - parent_margin_bottom_));
        if (want.width != size_.width || want.height != size_.height) {
            size_ = want;
            delete[] chars_;
            delete[] colors_;
            const size_t cells = cell_count();
            chars_ = new uint32_t[cells]();
            colors_ = new TerminalColors[cells];
            for (size_t i = 0; i < cells; ++i) {
                colors_[i] = TerminalColors::TRANSPARENT;
            }
            resized = true;
        }
    }
    bool flip_active_now = false;
    bool flip_horizontal_now = false;
    bool flip_expired = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (flip_end_ms_ != 0) {
            if (epoch_ms >= flip_end_ms_) {
                flip_end_ms_ = 0;
                flip_expired = true;
            } else {
                flip_active_now = true;
                flip_horizontal_now = flip_horizontal_;
            }
        }
    }
    if (!dirty_.exchange(false, std::memory_order_acq_rel)
        && !resized
        && !flip_expired) {
        return false;
    }
    const int w = static_cast<int>(size_.width);
    const int h = static_cast<int>(size_.height);
    if (w <= 0 || h <= 0) {
        return flip_active_now;
    }
    const size_t width_sz = static_cast<size_t>(w);
    const size_t cells = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<uint32_t> stage_chars(cells, 0x20u);
    std::vector<TerminalColors> stage_colors(
        cells, TerminalColors::TRANSPARENT);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!messages_.empty()) {
            /// First pass - newest → oldest, accumulate row counts.
            const size_t row_cap = static_cast<size_t>(h);
            size_t total_rows = 0;
            size_t oldest_visible_skip = 0;
            auto oldest_it = messages_.rend();
            for (auto it = messages_.rbegin();
                    it != messages_.rend(); ++it) {
                const size_t msg_rows = std::max<size_t>(
                    1, (it->count + width_sz - 1) / width_sz);
                if (total_rows + msg_rows >= row_cap) {
                    oldest_visible_skip =
                        (total_rows + msg_rows) - row_cap;
                    total_rows = row_cap;
                    oldest_it = it;
                    ++oldest_it;
                    break;
                }
                total_rows += msg_rows;
                oldest_it = it;
                ++oldest_it;
            }
            /// Second pass - oldest → newest, top-down.
            int render_row = 0;
            auto fwd_end = messages_.end();
            auto fwd_it = oldest_it.base();
            bool first = true;
            for (; fwd_it != fwd_end && render_row < h; ++fwd_it) {
                const Message& m = *fwd_it;
                const size_t msg_rows = std::max<size_t>(
                    1, (m.count + width_sz - 1) / width_sz);
                const size_t skip_rows = first ? oldest_visible_skip : 0;
                first = false;
                size_t cp_idx = skip_rows * width_sz;
                for (size_t r_in_msg = skip_rows;
                        r_in_msg < msg_rows && render_row < h;
                        ++r_in_msg) {
                    for (int c = 0; c < w && cp_idx < m.count; ++c) {
                        const size_t dst = static_cast<size_t>(render_row)
                            * width_sz + static_cast<size_t>(c);
                        stage_chars[dst] = m.cps[cp_idx];
                        stage_colors[dst] = m.per_cell_colors
                            ? m.per_cell_colors[cp_idx]
                            : m.color;
                        ++cp_idx;
                    }
                    ++render_row;
                }
            }
        }
    }
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            const size_t src = static_cast<size_t>(r) * width_sz
                + static_cast<size_t>(c);
            size_t dst;
            if (!flip_active_now) {
                dst = src;
            } else if (flip_horizontal_now) {
                dst = static_cast<size_t>(r) * width_sz
                    + static_cast<size_t>(w - 1 - c);
            } else {
                dst = static_cast<size_t>(h - 1 - r) * width_sz
                    + static_cast<size_t>(c);
            }
            chars_[dst] = stage_chars[src];
            colors_[dst] = stage_colors[src];
        }
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        bands_.erase(
            std::remove_if(bands_.begin(), bands_.end(),
                [&](const ColorBand& b) {
                    return b.end_ms <= epoch_ms;
                }),
            bands_.end());
        for (const auto& b : bands_) {
            if (b.horizontal) {
                if (b.index < 0 || b.index >= h) {
                    continue;
                }
                for (int c = 0; c < w; ++c) {
                    const size_t idx = static_cast<size_t>(b.index)
                        * width_sz + static_cast<size_t>(c);
                    if (colors_[idx] != TerminalColors::TRANSPARENT) {
                        colors_[idx] = b.color;
                    }
                }
            } else {
                if (b.index < 0 || b.index >= w) {
                    continue;
                }
                for (int r = 0; r < h; ++r) {
                    const size_t idx = static_cast<size_t>(r)
                        * width_sz + static_cast<size_t>(b.index);
                    if (colors_[idx] != TerminalColors::TRANSPARENT) {
                        colors_[idx] = b.color;
                    }
                }
            }
        }
    }
    return false;
}
} // namespace threadsafe_logger
