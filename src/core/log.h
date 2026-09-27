#pragma once

#include <cstdio>
#include <string>

namespace mausely {

// Minimal thread-safe logger: stderr (logcat on Android) + optional file (%APPDATA%\Mausely\mausely.log).
void logInit(const std::wstring& filePath);
void logInfo(const std::string& msg);
void logError(const std::string& msg);

// UTF-8 <-> UTF-16 (UTF-32 where wchar_t is 32-bit) helpers; paths are wide strings everywhere.
std::string toUtf8(const std::wstring& w);
std::wstring toWide(const std::string& s);

// fopen() for a wide path (`mode` as in fopen). Returns nullptr on failure.
FILE* openFile(const std::wstring& path, const char* mode);
// Creates one directory level; succeeds if it already exists.
bool makeDirectory(const std::wstring& path);

// Directory that contains the running executable (with trailing separator).
std::wstring exeDirectory();
// %APPDATA%\Mausely\ on Windows, ~/.config/Mausely/ elsewhere (created on demand, trailing separator).
std::wstring appDataDirectory();

}  // namespace mausely
