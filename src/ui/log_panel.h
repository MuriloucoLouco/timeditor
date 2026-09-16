#pragma once

// UI for core::Log: a full scrollable history window plus transient toast
// alerts that pop up on their own for new Warning/Error entries, so
// problems logged from anywhere in the app (failed loads/saves, GL setup
// issues...) are actually visible without a terminal attached.
namespace ui::LogPanel {

// Call exactly once per frame, unconditionally (regardless of whether the
// log window is open) - draws and ages out toast alerts for whatever's new
// in core::Log::History() since the last call.
void RenderToasts();

// Draws the full log history in a window while *open is true.
void RenderWindow(bool* open);

} // namespace ui::LogPanel
