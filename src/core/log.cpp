#include "core/log.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#endif
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <cstdint>
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
#ifdef __ANDROID__
    // stderr goes nowhere on Android.
    __android_log_print(level[0] == 'E' ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "Mausely", "%s", msg.c_str());
#else
    std::fprintf(stderr, "[%s] %s %s\n", stamp, level, msg.c_str());
#endif
    if (g_file) {
        std::fprintf(g_file, "[%s] %s %s\n", stamp, level, msg.c_str());
        std::fflush(g_file);
    }
}

}  // namespace

void logInit(const std::wstring& filePath) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = openFile(filePath, "a");
}

void logInfo(const std::string& msg) { write("INFO ", msg); }
void logError(const std::string& msg) { write("ERROR", msg); }

#ifdef _WIN32

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

FILE* openFile(const std::wstring& path, const char* mode) {
    return _wfopen(path.c_str(), toWide(mode).c_str());
}

bool makeDirectory(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
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

#else  // POSIX: wchar_t holds whole code points (UTF-32)

std::string toUtf8(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t wc : w) {
        auto c = static_cast<uint32_t>(wc);
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else if (c < 0x800) {
            s += static_cast<char>(0xC0 | (c >> 6));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            s += static_cast<char>(0xE0 | (c >> 12));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (c >> 18));
            s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return s;
}

std::wstring toWide(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        auto b = static_cast<unsigned char>(s[i]);
        int extra = b < 0x80 ? 0 : (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : -1;
        uint32_t c = extra == 0 ? b : extra == 1 ? (b & 0x1F) : extra == 2 ? (b & 0x0F) : (b & 0x07);
        bool ok = extra >= 0 && i + static_cast<size_t>(extra) < s.size();
        for (int k = 1; ok && k <= extra; ++k) {
            auto cont = static_cast<unsigned char>(s[i + k]);
            ok = (cont & 0xC0) == 0x80;
            c = (c << 6) | (cont & 0x3F);
        }
        w += static_cast<wchar_t>(ok ? c : 0xFFFD);
        i += ok ? static_cast<size_t>(extra) + 1 : 1;
    }
    return w;
}

FILE* openFile(const std::wstring& path, const char* mode) { return std::fopen(toUtf8(path).c_str(), mode); }

bool makeDirectory(const std::wstring& path) {
    return mkdir(toUtf8(path).c_str(), 0755) == 0 || errno == EEXIST;
}

std::wstring exeDirectory() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return L"./";
    std::string p(buf, static_cast<size_t>(n));
    size_t slash = p.find_last_of('/');
    return toWide(slash == std::string::npos ? "./" : p.substr(0, slash + 1));
}

std::wstring appDataDirectory() {
    std::string base;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home) base = std::string(home) + "/.config";
    else return exeDirectory();
    makeDirectory(toWide(base));
    std::wstring dir = toWide(base + "/Mausely/");
    makeDirectory(dir);
    return dir;
}

#endif

}  // namespace mausely
