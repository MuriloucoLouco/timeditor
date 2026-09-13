#include "file_dialog.h"
#include "nfd.h"

namespace ui::FileDialog {

namespace {

std::vector<nfdu8filteritem_t> ToNfdFilters(const std::vector<Filter>& filters) {
    std::vector<nfdu8filteritem_t> out;
    out.reserve(filters.size());
    for (const auto& f : filters) out.push_back({ f.name, f.spec });
    return out;
}

void SplitDirAndName(const std::string& path, std::string& dir, std::string& name) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        dir.clear();
        name = path;
    } else {
        dir = path.substr(0, slash);
        name = path.substr(slash + 1);
    }
}

} // namespace

std::string OpenFile(const char* title, const std::vector<Filter>& filters, const std::string& default_path) {
    std::vector<nfdu8filteritem_t> nfd_filters = ToNfdFilters(filters);

    nfdopendialogu8args_t args = {};
    args.filterList = nfd_filters.empty() ? nullptr : nfd_filters.data();
    args.filterCount = static_cast<nfdfiltersize_t>(nfd_filters.size());
    args.defaultPath = default_path.empty() ? nullptr : default_path.c_str();
    args.title = title;

    nfdu8char_t* out_path = nullptr;
    std::string result;
    if (NFD_OpenDialogU8_With(&out_path, &args) == NFD_OKAY) {
        result = out_path;
        NFD_FreePathU8(out_path);
    }
    return result;
}

std::vector<std::string> OpenFiles(const char* title, const std::vector<Filter>& filters,
                                    const std::string& default_path) {
    std::vector<nfdu8filteritem_t> nfd_filters = ToNfdFilters(filters);

    nfdopendialogu8args_t args = {};
    args.filterList = nfd_filters.empty() ? nullptr : nfd_filters.data();
    args.filterCount = static_cast<nfdfiltersize_t>(nfd_filters.size());
    args.defaultPath = default_path.empty() ? nullptr : default_path.c_str();
    args.title = title;

    std::vector<std::string> results;
    const nfdpathset_t* paths = nullptr;
    if (NFD_OpenDialogMultipleU8_With(&paths, &args) != NFD_OKAY) return results;

    nfdpathsetsize_t count = 0;
    NFD_PathSet_GetCount(paths, &count);
    for (nfdpathsetsize_t i = 0; i < count; i++) {
        nfdu8char_t* path = nullptr;
        if (NFD_PathSet_GetPathU8(paths, i, &path) == NFD_OKAY) {
            results.emplace_back(path);
            NFD_PathSet_FreePathU8(path);
        }
    }
    NFD_PathSet_Free(paths);
    return results;
}

std::string SaveFile(const char* title, const std::vector<Filter>& filters, const std::string& existing_path) {
    std::vector<nfdu8filteritem_t> nfd_filters = ToNfdFilters(filters);

    std::string dir, name;
    SplitDirAndName(existing_path, dir, name);

    nfdsavedialogu8args_t args = {};
    args.filterList = nfd_filters.empty() ? nullptr : nfd_filters.data();
    args.filterCount = static_cast<nfdfiltersize_t>(nfd_filters.size());
    args.defaultPath = dir.empty() ? nullptr : dir.c_str();
    args.defaultName = name.empty() ? nullptr : name.c_str();
    args.title = title;

    nfdu8char_t* out_path = nullptr;
    std::string result;
    if (NFD_SaveDialogU8_With(&out_path, &args) == NFD_OKAY) {
        result = out_path;
        NFD_FreePathU8(out_path);
    }
    return result;
}

std::string PickFolder(const char* title, const std::string& default_path) {
    nfdpickfolderu8args_t args = {};
    args.defaultPath = default_path.empty() ? nullptr : default_path.c_str();
    args.title = title;

    nfdu8char_t* out_path = nullptr;
    std::string result;
    if (NFD_PickFolderU8_With(&out_path, &args) == NFD_OKAY) {
        result = out_path;
        NFD_FreePathU8(out_path);
    }
    return result;
}

} // namespace ui::FileDialog
