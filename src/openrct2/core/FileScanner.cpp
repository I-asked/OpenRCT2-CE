#pragma region Copyright (c) 2014-2017 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#include "../common.h"

#ifdef _WIN3kk2
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)) || defined(__WIIU__)
    #include <dirent.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
#elif defined(__psp2__)
    #include <psp2/io/stat.h> 
    #include <psp2/io/dirent.h> 
#elif defined(_WIN32)
    // Windows needs this for widechar <-> utf8 conversion utils
    #include "../localisation/Language.h"
#endif

#include <memory>
#include <stack>
#include <string>
#include <vector>
#include "FileScanner.h"
#include "Memory.hpp"
#include "Path.hpp"
#include "String.hpp"

#include "../platform/platform.h"

enum class DIRECTORY_CHILD_TYPE
{
    DC_DIRECTORY,
    DC_FILE,
};

struct DirectoryChild
{
    DIRECTORY_CHILD_TYPE Type;
    std::string Name;

    // Files only
    uint64 Size         = 0;
    uint64 LastModified = 0;
};

static uint32 GetPathChecksum(const utf8 * path);
static bool MatchWildcard(const utf8 * fileName, const utf8 * pattern);

class FileScannerBase : public IFileScanner
{
private:
    struct DirectoryState
    {
        std::string                 Path;
        std::vector<DirectoryChild> Listing;
        sint32                      Index;
    };

    // Options
    std::string                 _rootPath;
    std::vector<std::string>    _patterns;
    bool                        _recurse;

    // State
    bool                        _started;
    std::stack<DirectoryState>  _directoryStack;

    // Current
    FileInfo    * _currentFileInfo;
    utf8        * _currentPath;

public:
    FileScannerBase(const std::string &pattern, bool recurse)
    {
        _rootPath = Path::GetDirectory(pattern);
        _recurse = recurse;
        _patterns = GetPatterns(Path::GetFileName(pattern));

        _currentPath = Memory::Allocate<utf8>(MAX_PATH);
        _currentFileInfo = Memory::Allocate<FileInfo>();

        Reset();
    }

    ~FileScannerBase() override
    {
        Memory::Free(_currentPath);
        Memory::Free(_currentFileInfo);
    }

    const FileInfo * GetFileInfo() const override
    {
        return _currentFileInfo;
    }

    const utf8 * GetPath() const override
    {
        return _currentPath;
    }

    const utf8 * GetPathRelative() const override
    {
        // +1 to remove the path separator
        return _currentPath + _rootPath.size() + 1;
    }

    void Reset() override
    {
        _started = false;
        _directoryStack = std::stack<DirectoryState>();
        _currentPath[0] = 0;
    }

    bool Next() override
    {
        if (!_started)
        {
            _started = true;
            PushState(_rootPath);
        }

        while (_directoryStack.size() != 0)
        {
            DirectoryState * state = &_directoryStack.top();
            state->Index++;
            if (state->Index >= (sint32)state->Listing.size())
            {
                _directoryStack.pop();
            }
            else
            {
                const DirectoryChild * child = &state->Listing[state->Index];
                if (child->Type == DIRECTORY_CHILD_TYPE::DC_DIRECTORY)
                {
                    if (_recurse)
                    {
                        utf8 childPath[MAX_PATH];
                        String::Set(childPath, sizeof(childPath), state->Path.c_str());
                        Path::Append(childPath, sizeof(childPath), child->Name.c_str());

                        PushState(childPath);
                    }
                }
                else if (PatternMatch(child->Name))
                {
                    String::Set(_currentPath, MAX_PATH, state->Path.c_str());
                    Path::Append(_currentPath, MAX_PATH, child->Name.c_str());

                    _currentFileInfo->Name = child->Name.c_str();
                    _currentFileInfo->Size = child->Size;
                    _currentFileInfo->LastModified = child->LastModified;
                    return true;
                }
            }
        }
        return false;
    }

    virtual void GetDirectoryChildren(std::vector<DirectoryChild> &children, const std::string &path) abstract;

private:
    void PushState(const std::string &directory)
    {
        DirectoryState newState;
        newState.Path = directory;
        newState.Index = -1;
        GetDirectoryChildren(newState.Listing, directory);
        _directoryStack.push(newState);
    }

    bool PatternMatch(const std::string &fileName)
    {
        for (const auto &pattern : _patterns)
        {
            if (MatchWildcard(fileName.c_str(), pattern.c_str()))
            {
                return true;
            }
        }
        return false;
    }

    static std::vector<std::string> GetPatterns(const std::string &delimitedPatterns)
    {
        std::vector<std::string> patterns;

        const utf8 * start = delimitedPatterns.c_str();
        const utf8 * ch = start;
        utf8 c;
        do
        {
            c = *ch;
            if (c == '\0' || c == ';')
            {
                size_t length = (size_t)(ch - start);
                if (length > 0)
                {
                    std::string newPattern = std::string(start, length);
                    patterns.push_back(newPattern);
                }
                start = ch + 1;
            }
            ch++;
        }
        while (c != '\0');

        patterns.shrink_to_fit();
        return patterns;
    }
};

#ifdef _WIN32

class FileScannerWindows final : public FileScannerBase
{
public:
    FileScannerWindows(const std::string &pattern, bool recurse)
        : FileScannerBase(pattern, recurse)
    {
    }

    void GetDirectoryChildren(std::vector<DirectoryChild> &children, const std::string &path) override
    {
        std::string pattern = path + "\\*";
        wchar_t * wPattern = utf8_to_widechar(pattern.c_str());

        WIN32_FIND_DATAW findData;
        HANDLE hFile = FindFirstFileW(wPattern, &findData);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (lstrcmpW(findData.cFileName, L".") != 0 &&
                    lstrcmpW(findData.cFileName, L"..") != 0)
                {
                    DirectoryChild child = CreateChild(&findData);
                    children.push_back(child);
                }
            }
            while (FindNextFileW(hFile, &findData));
            FindClose(hFile);
        }

        Memory::Free(wPattern);
    }

private:
    static DirectoryChild CreateChild(const WIN32_FIND_DATAW * child)
    {
        DirectoryChild result;

        utf8 * name = widechar_to_utf8(child->cFileName);
        result.Name = std::string(name);
        Memory::Free(name);

        if (child->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            result.Type = DIRECTORY_CHILD_TYPE::DC_DIRECTORY;
        }
        else
        {
            result.Type = DIRECTORY_CHILD_TYPE::DC_FILE;
            result.Size = ((uint64)child->nFileSizeHigh << 32ULL) | (uint64)child->nFileSizeLow;
            result.LastModified = ((uint64)child->ftLastWriteTime.dwHighDateTime << 32ULL) | (uint64)child->ftLastWriteTime.dwLowDateTime;
        }
        return result;
    }
};

#endif // _WIN32

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)) || defined(__WIIU__)

class FileScannerUnix final : public FileScannerBase
{
public:
    FileScannerUnix(const std::string &pattern, bool recurse)
        : FileScannerBase(pattern, recurse)
    {
    }

    void GetDirectoryChildren(std::vector<DirectoryChild> &children, const std::string &path) override
    {
        struct dirent * * namelist;
        sint32 count = scandir(path.c_str(), &namelist, FilterFunc, alphasort);
        if (count > 0)
        {
            for (sint32 i = 0; i < count; i++)
            {
                const struct dirent * node = namelist[i];
                if (!String::Equals(node->d_name, ".") &&
                    !String::Equals(node->d_name, ".."))
                {
                    DirectoryChild child = CreateChild(path.c_str(), node);
                    children.push_back(child);
                }
                free(namelist[i]);
            }
            free(namelist);
        }
    }

private:
    static sint32 FilterFunc(const struct dirent * d)
    {
        return 1;
    }

    static DirectoryChild CreateChild(const utf8 * directory, const struct dirent * node)
    {
        DirectoryChild result;
        result.Name = std::string(node->d_name);
#ifndef __psp2__
        if (node->d_type == DT_DIR)
        {
            result.Type = DIRECTORY_CHILD_TYPE::DC_DIRECTORY;
        }
        else
        {
#else   
        if (node->d_stat.st_attr == SCE_SO_IFDIR)
        {
            result.Type = DIRECTORY_CHILD_TYPE::DC_DIRECTORY;
        }
        else
        {
#endif
            result.Type = DIRECTORY_CHILD_TYPE::DC_FILE;

            // Get the full path of the file
            size_t pathSize = String::SizeOf(directory) + 1 + String::SizeOf(node->d_name) + 1;
            utf8 * path = Memory::Allocate<utf8>(pathSize);
            String::Set(path, pathSize, directory);
            Path::Append(path, pathSize, node->d_name);
#ifndef __psp2__ 
            struct stat statInfo;
            sint32 statRes = stat(path, &statInfo);
            if (statRes != -1)
            {
                result.Size = statInfo.st_size;
                result.LastModified = statInfo.st_mtime;

                if (S_ISDIR(statInfo.st_mode))
                {
                    result.Type = DIRECTORY_CHILD_TYPE::DC_DIRECTORY;
                }
            }
#endif

            Memory::Free(path);
// #ifndef __psp2__
        }
// #endif
        return result;
    }
};

#endif // defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))

#ifdef __psp2__
class FileScannerPSP2 final : public FileScannerBase
{
public:
    FileScannerPSP2(const std::string &pattern, bool recurse)
        : FileScannerBase(pattern, recurse)
    {
    }

    void GetDirectoryChildren(std::vector<DirectoryChild> &children, const std::string &path) override
    {
        // struct dirent * * namelist;
        
        SceUID dir = sceIoDopen(path.c_str());
        if (dir > (SceUID)-1)
        {
            SceIoDirent node;
            while(sceIoDread(dir, &node))
            {
                // const struct dirent * node = namelist[i];
                if (!String::Equals(node.d_name, ".") &&
                    !String::Equals(node.d_name, ".."))
                {
                    DirectoryChild child = CreateChild(path.c_str(), &node);
                    children.push_back(child);
                }
            }
            sceIoDclose(dir);
        }
    }

private:
    static sint32 FilterFunc(const struct dirent * d)
    {
        return 1;
    }

    static DirectoryChild CreateChild(const utf8 * directory, const SceIoDirent * node)
    {
        DirectoryChild result;
        result.Name = std::string(node->d_name);

        if (node->d_stat.st_attr == SCE_SO_IFDIR)
        {
            result.Type = DIRECTORY_CHILD_TYPE::DC_DIRECTORY;
        }
        else
        {
            result.Type = DIRECTORY_CHILD_TYPE::DC_FILE;

            // Get the full path of the file
            size_t pathSize = String::SizeOf(directory) + 1 + String::SizeOf(node->d_name) + 1;
            utf8 * path = Memory::Allocate<utf8>(pathSize);
            String::Set(path, pathSize, directory);
            Path::Append(path, pathSize, node->d_name);

            Memory::Free(path);
        }
        return result;
    }
};
#endif

IFileScanner * Path::ScanDirectory(const std::string &pattern, bool recurse)
{
#ifdef _WIN32
    return new FileScannerWindows(pattern, recurse);
#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)) || defined(__WIIU__)
    return new FileScannerUnix(pattern, recurse);
#elif defined(__psp2__)
    return new FileScannerPSP2(pattern, recurse);
#endif
}

void Path::QueryDirectory(QueryDirectoryResult * result, const std::string &pattern)
{
    IFileScanner * scanner = Path::ScanDirectory(pattern, true);
    while (scanner->Next())
    {
        const FileInfo * fileInfo = scanner->GetFileInfo();
        const utf8 * path = scanner->GetPath();

        result->TotalFiles++;
        result->TotalFileSize += fileInfo->Size;
        result->FileDateModifiedChecksum ^=
            (uint32)(fileInfo->LastModified >> 32) ^
            (uint32)(fileInfo->LastModified & 0xFFFFFFFF);
        result->FileDateModifiedChecksum = ror32(result->FileDateModifiedChecksum, 5);
        result->PathChecksum += GetPathChecksum(path);
    }
    delete scanner;
}

std::vector<std::string> Path::GetDirectories(const std::string &path)
{
    auto scanner = std::unique_ptr<IFileScanner>(ScanDirectory(path, false));
    auto baseScanner = static_cast<FileScannerBase *>(scanner.get());

    std::vector<DirectoryChild> children;
    baseScanner->GetDirectoryChildren(children, path);

    std::vector<std::string> subDirectories;
    for (const auto &c : children)
    {
        if (c.Type == DIRECTORY_CHILD_TYPE::DC_DIRECTORY)
        {
            subDirectories.push_back(c.Name);
        }
    }
    return subDirectories;
}

static uint32 GetPathChecksum(const utf8 * path)
{
    uint32 hash = 0xD8430DED;
    for (const utf8 * ch = path; *ch != '\0'; ch++)
    {
        hash += (*ch);
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

/**
 * Due to FindFirstFile / FindNextFile searching for DOS names as well, *.doc also matches *.docx which isn't what the pattern
 * specified. This will verify if a filename does indeed match the pattern we asked for.
 * @remarks Based on algorithm (http://xoomer.virgilio.it/acantato/dev/wildcard/wildmatch.html)
 */
static bool MatchWildcard(const utf8 * fileName, const utf8 * pattern)
{
    while (*fileName != '\0')
    {
        switch (*pattern) {
        case '?':
            if (*fileName == '.')
            {
                return false;
            }
            break;
        case '*':
            do
            {
                pattern++;
            }
            while (*pattern == '*');
            if (*pattern == '\0')
            {
                return false;
            }
            while (*fileName != '\0')
            {
                if (MatchWildcard(fileName++, pattern))
                {
                    return true;
                }
            }
            return false;
        default:
            if (toupper(*fileName) != toupper(*pattern))
            {
                return false;
            }
            break;
        }
        pattern++;
        fileName++;
    }
    while (*pattern == '*')
    {
        ++fileName;
    }
    return *pattern == '\0';
}
