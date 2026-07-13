#pragma once

#include <filesystem>
#include <string>
#include <vector>

class FileDialog
{
public:
    static std::vector<std::filesystem::path> OpenDffFiles(bool multiple);
    static std::filesystem::path OpenTxdFile();
    static std::filesystem::path SelectDffFolder();
    static std::filesystem::path SelectExportFolder();
    static std::filesystem::path SaveDffFile(const std::string& suggestedName);
};
