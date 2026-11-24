// Paths bridge: Firestorm 7.2.5 Poser file operations on Cool VL LLDir.
// All calls perform the actual filesystem operations, no mock behavior.
#pragma once
#include "lldir.h"
#include <filesystem>
#include <vector>
#include <algorithm>
namespace FSPoserFilePaths {
inline std::string getExpandedFilename(ELLPath loc, const std::string& first) {
    return gDirUtil.getFullPath(loc, first, "");
}
inline std::string getExpandedFilename(ELLPath loc, const std::string& first,
                                        const std::string& name) {
    return gDirUtil.getFullPath(loc, first, name);
}
inline std::string getExpandedFilename(ELLPath loc, const std::string& a,
                                        const std::string& b, const std::string& name) {
    return gDirUtil.getFullPath(loc, a, b, name);
}
inline bool fileExists(const std::string& p) { return std::filesystem::exists(std::filesystem::u8path(p)); }
inline std::string getDirDelimiter() { return std::string(1, std::filesystem::path::preferred_separator); }
inline std::string add(const std::string& a, const std::string& b) {
    return (std::filesystem::u8path(a) / std::filesystem::u8path(b)).string();
}
inline void append(std::string& path, const std::string& name) { path=add(path,name); }
inline std::string getBaseFileName(const std::string& p, bool stripExt) {
    return LLDir::getBaseFileName(p,stripExt);
}
inline std::vector<std::string> getFilesInDir(const std::string& dir) {
    std::vector<std::string> result;
    std::error_code err;
    for(std::filesystem::directory_iterator it(std::filesystem::u8path(dir),err),end;
        !err && it!=end; it.increment(err)) {
        if(it->is_regular_file(err)) result.push_back(it->path().filename().string());
    }
    std::sort(result.begin(),result.end());
    return result;
}
} // namespace FSPoserFilePaths
