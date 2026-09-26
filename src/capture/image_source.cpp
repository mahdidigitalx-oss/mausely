#include "capture/image_source.h"

#include <windows.h>

#include <cstdio>
#include <memory>

#include "core/clock.h"
#include "core/log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_WINDOWS_UTF8
#include <stb_image.h>

namespace mausely {

bool loadImageFile(const std::wstring& path, Frame& out, std::string* error) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) {
        if (error) *error = "Cannot open " + toUtf8(path);
        return false;
    }
    int w = 0, h = 0, n = 0;
    std::unique_ptr<stbi_uc, void (*)(void*)> px(stbi_load_from_file(f, &w, &h, &n, 4), stbi_image_free);
    std::fclose(f);
    if (!px) {
        if (error) *error = "Cannot decode " + toUtf8(path) + ": " + stbi_failure_reason();
        return false;
    }
    out.resize(w, h);
    const stbi_uc* src = px.get();
    for (size_t i = 0, n4 = static_cast<size_t>(w) * h; i < n4; ++i) {
        out.bgra[i * 4 + 0] = src[i * 4 + 2];
        out.bgra[i * 4 + 1] = src[i * 4 + 1];
        out.bgra[i * 4 + 2] = src[i * 4 + 0];
        out.bgra[i * 4 + 3] = 255;
    }
    out.arrivalUs = Clock::nowUs();
    out.index = 0;
    return true;
}

bool ImageSource::open(const std::wstring& path, double fps, std::string* error) {
    if (!loadImageFile(path, image_, error)) return false;
    fps_ = fps > 0 ? fps : 30.0;
    nextUs_ = Clock::nowUs();
    description_ = "Image " + toUtf8(path) + " (" + std::to_string(image_.width) + "x" +
                   std::to_string(image_.height) + ")";
    return true;
}

bool ImageSource::read(Frame& out, std::string*) {
    // Pace like a camera so the pipeline sees realistic timing.
    int64_t now = Clock::nowUs();
    if (now < nextUs_) Sleep(static_cast<DWORD>((nextUs_ - now) / 1000));
    nextUs_ += static_cast<int64_t>(1e6 / fps_);
    if (Clock::nowUs() > nextUs_ + 1'000'000) nextUs_ = Clock::nowUs();

    out.width = image_.width;
    out.height = image_.height;
    out.bgra = image_.bgra;
    out.arrivalUs = Clock::nowUs();
    out.index = counter_++;
    return true;
}

}  // namespace mausely
