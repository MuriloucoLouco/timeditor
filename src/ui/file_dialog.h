#pragma once
#include <string>
#include <vector>

// Thin synchronous wrapper around nativefiledialog-extended, matching the
// small subset of dialog shapes this app needs (single/multi file open,
// save, folder pick). NFD_Init()/NFD_Quit() bracket the whole process (see
// main.cpp) - nothing here needs to call them.
namespace ui::FileDialog {

// name: label shown next to the filter in the dialog's type dropdown (e.g.
// "Image Files"). spec: comma-separated extensions with no dots or
// wildcards (e.g. "png,jpg,jpeg").
struct Filter {
    const char* name;
    const char* spec;
};

// Returns the chosen path, or empty if the dialog was cancelled/failed.
std::string OpenFile(const char* title, const std::vector<Filter>& filters,
                      const std::string& default_path = "");

// Returns the chosen paths, or empty if the dialog was cancelled/failed.
std::vector<std::string> OpenFiles(const char* title, const std::vector<Filter>& filters,
                                    const std::string& default_path = "");

// `existing_path`, if non-empty, seeds both the starting folder and the
// suggested file name (its directory and basename are split apart, since
// NFD takes them as separate fields).
std::string SaveFile(const char* title, const std::vector<Filter>& filters,
                      const std::string& existing_path = "");

std::string PickFolder(const char* title, const std::string& default_path = "");

} // namespace ui::FileDialog
