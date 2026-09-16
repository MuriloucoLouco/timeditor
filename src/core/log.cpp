#include "log.h"
#include <cstdarg>
#include <cstdio>

namespace core::Log {

namespace {

constexpr size_t kMaxHistory = 2000; // oldest entries drop once exceeded - a debugging aid, not an audit log

std::vector<Entry> g_history;
int g_next_id = 0;
FILE* g_file = nullptr;

const char* LevelTag(Level level) {
    switch (level) {
        case Level::Info: return "INFO";
        case Level::Warning: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

void Log(Level level, const char* fmt, va_list args) {
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);

    g_history.push_back({ level, buf, g_next_id++ });
    if (g_history.size() > kMaxHistory) {
        g_history.erase(g_history.begin(), g_history.begin() + (g_history.size() - kMaxHistory));
    }

    std::fprintf(stderr, "[%s] %s\n", LevelTag(level), buf);
    if (g_file) {
        std::fprintf(g_file, "[%s] %s\n", LevelTag(level), buf);
        std::fflush(g_file); // crash-safe - this is exactly what gets read after something went wrong
    }
}

} // namespace

void Init(const std::string& path) {
    if (g_file) std::fclose(g_file);
    g_file = std::fopen(path.c_str(), "w");
}

void Info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Log(Level::Info, fmt, args);
    va_end(args);
}

void Warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Log(Level::Warning, fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Log(Level::Error, fmt, args);
    va_end(args);
}

const std::vector<Entry>& History() { return g_history; }

void Clear() { g_history.clear(); }

} // namespace core::Log
