#include "core/log.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <ctime>
#include <mutex>

namespace mausely {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

void write(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::time_t t = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::fprintf(stderr, "[%s] %s %s\n", stamp, level, msg.c_str());
    if (g_file) {
        std::fprintf(g_file, "[%s] %s %s\n", stamp, level, msg.c_str());
        std::fflush(g_file);
    }
}

}  // namespace

void logInit(const std::wstring& filePath) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = _wfopen(filePath.c_str(), L"a");
}

void logInfo(const std::string& msg) { write("INFO ", msg); }
void logError(const std::string& msg) { write("ERROR", msg); }

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring exeDirectory() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : p.substr(0, slash + 1);
}

std::wstring appDataDirectory() {
    PWSTR path = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
        dir = std::wstring(path) + L"\\Mausely\\";
        CoTaskMemFree(path);
    } else {
        dir = exeDirectory();
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

}  // namespace mausely
