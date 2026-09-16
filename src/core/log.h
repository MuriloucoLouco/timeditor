#pragma once
#include <string>
#include <vector>

// A small process-wide log used across all three layers (core/gfx/ui) for
// anything worth surfacing to whoever's running the app - failed loads/
// saves, GL setup problems, etc. Every call appends to an in-memory history
// (capped, oldest entries dropped) that ui::LogPanel displays as a
// scrollable window and as transient toast alerts for new
// Warning/Error entries, and always echoes to stderr; Init() additionally
// tees it to a file on disk, since a double-clicked release build has no
// visible terminal to redirect.
//
// Not thread-safe - fine as long as it's only ever called from the single
// main/render thread, which is true everywhere in this app today.
namespace core::Log {

enum class Level { Info, Warning, Error };

struct Entry {
    Level level;
    std::string message;
    // A monotonically increasing id (not wall-clock time) - just enough to
    // let ui::LogPanel tell "have I already shown a toast for this one"
    // apart from "is this new since last frame".
    int id;
};

// Opens `path` (truncated) as an additional log sink. Optional - entries
// still go to stderr and the in-memory history without ever calling this.
void Init(const std::string& path);

void Info(const char* fmt, ...);
void Warning(const char* fmt, ...);
void Error(const char* fmt, ...);

// Every entry logged so far this run, oldest first.
const std::vector<Entry>& History();

void Clear();

} // namespace core::Log
