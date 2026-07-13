#include "FileDialog.h"

#ifdef _WIN32

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <array>

namespace
{
    std::vector<std::filesystem::path> ParseMultiSelectBuffer(const wchar_t* buffer)
    {
        std::vector<std::filesystem::path> paths;

        const std::filesystem::path first(buffer);
        const wchar_t* cursor = buffer + first.native().size() + 1;

        if (*cursor == L'\0')
        {
            paths.push_back(first);
            return paths;
        }

        while (*cursor != L'\0')
        {
            const std::filesystem::path name(cursor);
            paths.push_back(first / name);
            cursor += name.native().size() + 1;
        }

        return paths;
    }

    std::vector<std::filesystem::path> OpenFiles(
        const wchar_t* filter,
        bool multiple)
    {
        std::vector<wchar_t> buffer(65536, L'\0');

        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (multiple)
        {
            dialog.Flags |= OFN_ALLOWMULTISELECT;
        }

        if (!GetOpenFileNameW(&dialog))
        {
            return {};
        }

        return ParseMultiSelectBuffer(buffer.data());
    }

    std::filesystem::path SaveFile(
        const wchar_t* filter,
        const std::wstring& suggestedName)
    {
        std::array<wchar_t, 4096> buffer{};
        wcsncpy_s(buffer.data(), buffer.size(), suggestedName.c_str(), _TRUNCATE);

        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        dialog.lpstrDefExt = L"dff";

        if (!GetSaveFileNameW(&dialog))
        {
            return {};
        }

        return std::filesystem::path(buffer.data());
    }
}

std::vector<std::filesystem::path> FileDialog::OpenDffFiles(bool multiple)
{
    return OpenFiles(
        L"RenderWare DFF (*.dff)\0*.dff\0All files (*.*)\0*.*\0",
        multiple);
}

std::filesystem::path FileDialog::OpenTxdFile()
{
    const auto files = OpenFiles(
        L"RenderWare TXD (*.txd)\0*.txd\0All files (*.*)\0*.*\0",
        false);

    return files.empty() ? std::filesystem::path{} : files.front();
}

std::filesystem::path SelectFolderWithTitle(const wchar_t* title)
{
    BROWSEINFOW browse{};
    browse.lpszTitle = title;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
    if (item == nullptr)
    {
        return {};
    }

    std::array<wchar_t, MAX_PATH> path{};
    const bool success = SHGetPathFromIDListW(item, path.data()) != FALSE;
    CoTaskMemFree(item);

    return success
        ? std::filesystem::path(path.data())
        : std::filesystem::path{};
}

std::filesystem::path FileDialog::SelectDffFolder()
{
    return SelectFolderWithTitle(
        L"Select a folder containing DFF files");
}

std::filesystem::path FileDialog::SelectExportFolder()
{
    return SelectFolderWithTitle(
        L"Select an ODFF DFF export folder");
}

std::filesystem::path FileDialog::SaveDffFile(
    const std::string& suggestedName)
{
    const std::wstring wideName(
        suggestedName.begin(),
        suggestedName.end());

    return SaveFile(
        L"RenderWare DFF (*.dff)\0*.dff\0All files (*.*)\0*.*\0",
        wideName);
}

#else

std::vector<std::filesystem::path> FileDialog::OpenDffFiles(bool)
{
    return {};
}

std::filesystem::path FileDialog::OpenTxdFile()
{
    return {};
}

std::filesystem::path FileDialog::SelectDffFolder()
{
    return {};
}

std::filesystem::path FileDialog::SelectExportFolder()
{
    return {};
}

std::filesystem::path FileDialog::SaveDffFile(const std::string&)
{
    return {};
}

#endif
