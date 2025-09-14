#include <windows.h>
#include <iostream>
#include <string>
#include <linux/limits.h>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <queue>
#include <dirent.h>

#define WIN_DLL_API(ret) extern "C" ret WINAPI

void SplitPath(const std::string& path, std::string& parent, std::string& leaf)
{
    parent.clear();
    leaf.clear();

    if (path.empty())
    {
        return;
    }

    // Find the last separator
    auto it = std::find_if(path.rbegin(), path.rend(), [](char c) {
        return c == '/' || c == '\\';
    });

    if (it == path.rend()) {
        // No separator found → entire path is leaf
        leaf = path;
        return;
    }

    // Index of last separator
    size_t sepIndex = std::distance(it, path.rend()) - 1;

    // Extract leaf
    leaf = path.substr(sepIndex + 1);

    // Extract parent, remove trailing separators
    parent = path.substr(0, sepIndex);
    while (!parent.empty() && (parent.back() == '/' || parent.back() == '\\'))
    {
        parent.pop_back();
    }
}

std::string CanonizePath(const std::string& path)
{
    if (path.empty())
    {
        return path;
    }

    char buffer[PATH_MAX];
    char *res = realpath(path.c_str(), buffer);
    if (!res)
    {
        return path;
    }

    return std::string(buffer);
}

std::string CanonizeParentPath(const std::string& path)
{
    std::string parent, leaf;

    SplitPath(path, parent, leaf);

    if (parent.empty() || leaf.empty())
    {
        return path;
    }

    return CanonizePath(parent) + "/" + leaf;
}

std::string GetUnixFileName(std::string path)
{
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (sizeNeeded <= 0)
    {
        return "";
    }

    WCHAR* widePathC = new WCHAR[sizeNeeded];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, widePathC, sizeNeeded);

    auto unixPathC = wine_get_unix_file_name(widePathC);
    if (!unixPathC) {
        return "";
    }

    return std::string(unixPathC);
}

std::string LoopGetUnixFileName(std::string path)
{
    std::string parent;
    std::string leaf;
    std::string unixPath;
    std::string result;

    SplitPath(path, parent, leaf);
    result = leaf;
    unixPath = GetUnixFileName(parent);

    while (unixPath.empty() && !parent.empty())
    {
        SplitPath(std::string(parent), parent, leaf);
        result = leaf + "/" + result;
        unixPath = GetUnixFileName(parent);
    }

    if (!unixPath.empty())
    {
        unixPath = CanonizeParentPath(unixPath);
        unixPath += "/";
    }

    return unixPath + result;
}

std::string ConvertWindowsPathToStr(const char* pathC)
{
    if (!pathC)
    {
        return "";
    }

    auto path = std::string(pathC);

    while (path.back() == '/' || path.back() == '\\')
    {
        path.pop_back();
    }

    return LoopGetUnixFileName(path);
}

WIN_DLL_API(bool) ConvertWindowsPath(const char* pathC, char* buffer, int bufferSize)
{
    auto unixPath = ConvertWindowsPathToStr(pathC);

    if (unixPath.empty() || bufferSize <= 0)
    {
        return false;
    }

    if (unixPath.size() < bufferSize)
    {
        strcpy(buffer, unixPath.c_str());

        return true;
    }

    strncpy(buffer, unixPath.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';

    return false;
}

int GetPathSymlinkStatus(std:: string path)
{
    struct stat buf;
    int res = lstat(path.c_str(), &buf);

    if (res != 0)
    {
        return -1;
    }
    
    if (S_ISLNK(buf.st_mode))
    {
        return 1;
    }

    return 0;
}

WIN_DLL_API(bool) IsWindowsPathSymlink(const char* pathC)
{
    auto unixPath = ConvertWindowsPathToStr(pathC);

    return GetPathSymlinkStatus(unixPath) > 0;
}

void RecursivelyRemoveDirectory(std::string path)
{
    struct stat buf;
    std::queue<std::string> dirsToCheck;
    std::deque<std::string> dirsToRemove;

    dirsToCheck.push(path);
    dirsToRemove.push_front(path);

    while (!dirsToCheck.empty())
    {
        auto dirPath = dirsToCheck.front();
        dirsToCheck.pop();

        struct dirent *ent;
        DIR *dir = opendir (dirPath.c_str());
        if (!dir)
        {
            // Could not open dir
            continue;
        }

        while ((ent = readdir (dir)) != NULL)
        {
            auto entPath = std::string(ent->d_name);
            if (entPath == "." || entPath == "..")
            {
                continue;
            }

            entPath = dirPath + "/" + entPath;
            int res = lstat (entPath.c_str(), &buf);
            if (res != 0)
            {
                continue;
            }
            else if (S_ISLNK(buf.st_mode) || !S_ISDIR(buf.st_mode))
            {
                unlink(entPath.c_str());
            }
            else if (S_ISDIR(buf.st_mode))
            {
                dirsToCheck.push(entPath);
                dirsToRemove.push_front(entPath);
            }
        }

        closedir (dir);
    }

    while (!dirsToRemove.empty())
    {
        auto dirPath = dirsToRemove.front();
        dirsToRemove.pop_front();

        rmdir(dirPath.c_str());
    }
}

WIN_DLL_API(bool) DeleteWindowsPath(const char* pathC)
{
    auto unixPath = ConvertWindowsPathToStr(pathC);
    struct stat buf;
    int res = lstat(unixPath.c_str(), &buf);

    if (res != 0)
    {
        return false;
    }
    else if (S_ISLNK(buf.st_mode) || !S_ISDIR(buf.st_mode))
    {
        unlink(unixPath.c_str());
        return true;
    }

    RecursivelyRemoveDirectory(unixPath);

    return true;
}

WIN_DLL_API(bool) CreateWindowsSymlink(const char* pathSourceC, const char* pathDestinationC)
{
    auto unixSourcePath = ConvertWindowsPathToStr(pathSourceC);
    auto unixDestinationPath = ConvertWindowsPathToStr(pathDestinationC);

    if (unixSourcePath.empty() || unixDestinationPath.empty())
    {
        return false;
    }

    auto res = symlink(unixSourcePath.c_str(), unixDestinationPath.c_str());

    return res == 0;
}

WIN_DLL_API(void) Test()
{
    auto windowsPath = "";
    auto convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 1: " << convRes << std::endl;

    windowsPath = "c:\\dupa\\test";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 2: " << convRes << std::endl;

    windowsPath = "z:\\home\\maniman303\\dupaLink\\test";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 3: " << convRes << std::endl;

    windowsPath = "z:\\home\\maniman303\\dupaLink\\testLink";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 4: " << convRes << std::endl;

    windowsPath = "z:\\home\\maniman303\\dupaLink";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 5: " << convRes << std::endl;

    windowsPath = "z:\\home\\maniman303\\dupaLink\\dupaFake\\testFake\\\\";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 6: " << convRes << std::endl;

    windowsPath = "z:\\home\\maniman303\\dupaLink\\dupa\\dupaFake\\testFake";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 7: " << convRes << std::endl;

    windowsPath = "./";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 8: " << convRes << std::endl;

    windowsPath = "./main.cpp";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 9: " << convRes << std::endl;

    windowsPath = "main.cpp";
    convRes = ConvertWindowsPathToStr(windowsPath);
    std::cout << "Convert result 10: " << convRes << std::endl;
}