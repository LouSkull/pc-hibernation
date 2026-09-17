#pragma once
#include <filesystem>
#include <vector>
#include <string>
struct MediaItem { int id=0; std::wstring name,file,source; };
class MediaLibrary {
    std::filesystem::path root;
    bool write(const MediaItem& item);
public:
    void open(const std::filesystem::path& folder);
    const std::filesystem::path& folder() const { return root; }
    std::vector<MediaItem> items() const;
    MediaItem find(int id) const;
    static bool validName(const std::wstring& name);
    static bool supported(const std::filesystem::path& path);
    int import(const std::wstring& name,const std::filesystem::path& source,int replace=0);
    bool rename(int id,const std::wstring& name);
    bool remove(int id);
};
