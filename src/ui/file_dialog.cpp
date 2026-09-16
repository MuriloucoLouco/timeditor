#include "file_dialog.h"
#include "tinyfiledialogs.h"

namespace ui::FileDialog {

namespace {

// Flattens this app's `{name, "ext1,ext2"}` filters into tinyfiledialogs'
// shape - see the comment on Filter in file_dialog.h for why they collapse
// into one description/pattern list.
struct TinyFdFilters {
    std::vector<std::string> pattern_storage;
    std::vector<const char*> patterns;
    std::string description;
};

TinyFdFilters ToTinyFdFilters(const std::vector<Filter>& filters) {
    TinyFdFilters out;
    for (const auto& f : filters) {
        if (!out.description.empty()) out.description += "/";
        out.description += f.name;

        std::string spec = f.spec;
        size_t start = 0;
        while (start <= spec.size()) {
            size_t comma = spec.find(',', start);
            size_t len = comma == std::string::npos ? std::string::npos : comma - start;
            std::string ext = spec.substr(start, len);
            if (!ext.empty()) out.pattern_storage.push_back("*." + ext);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    // pattern_storage is done growing by this point, so these c_str()
    // pointers stay valid for the rest of this struct's lifetime.
    for (const auto& p : out.pattern_storage) out.patterns.push_back(p.c_str());
    return out;
}

std::string SafeString(const char* s) { return s ? s : std::string(); }

} // namespace

std::string OpenFile(const char* title, const std::vector<Filter>& filters, const std::string& default_path) {
    TinyFdFilters f = ToTinyFdFilters(filters);
    const char* result =
        tinyfd_openFileDialog(title, default_path.c_str(), static_cast<int>(f.patterns.size()),
                               f.patterns.empty() ? nullptr : f.patterns.data(),
                               f.description.empty() ? nullptr : f.description.c_str(), 0);
    return SafeString(result);
}

std::vector<std::string> OpenFiles(const char* title, const std::vector<Filter>& filters,
                                    const std::string& default_path) {
    TinyFdFilters f = ToTinyFdFilters(filters);
    const char* result =
        tinyfd_openFileDialog(title, default_path.c_str(), static_cast<int>(f.patterns.size()),
                               f.patterns.empty() ? nullptr : f.patterns.data(),
                               f.description.empty() ? nullptr : f.description.c_str(), 1);
    std::vector<std::string> out;
    if (!result) return out;

    // Multiple selections come back as one '|'-separated string.
    std::string joined = result;
    size_t start = 0;
    while (start <= joined.size()) {
        size_t bar = joined.find('|', start);
        size_t len = bar == std::string::npos ? std::string::npos : bar - start;
        out.push_back(joined.substr(start, len));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return out;
}

std::string SaveFile(const char* title, const std::vector<Filter>& filters, const std::string& existing_path) {
    TinyFdFilters f = ToTinyFdFilters(filters);
    const char* result =
        tinyfd_saveFileDialog(title, existing_path.c_str(), static_cast<int>(f.patterns.size()),
                               f.patterns.empty() ? nullptr : f.patterns.data(),
                               f.description.empty() ? nullptr : f.description.c_str());
    return SafeString(result);
}

std::string PickFolder(const char* title, const std::string& default_path) {
    const char* result = tinyfd_selectFolderDialog(title, default_path.c_str());
    return SafeString(result);
}

} // namespace ui::FileDialog
