#include "capture/mf_camera.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/clock.h"
#include "core/log.h"

namespace mausely {
namespace {

template <class T>
void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::string hrText(const char* what, HRESULT hr) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return buf;
}

// Enumerates capture devices; caller releases each IMFActivate and frees the array.
HRESULT listDevices(IMFActivate*** devices, UINT32* count) {
    IMFAttributes* attr = nullptr;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr)) hr = attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attr, devices, count);
    safeRelease(attr);
    return hr;
}

std::string friendlyName(IMFActivate* dev) {
    WCHAR* name = nullptr;
    UINT32 len = 0;
    std::string s = "camera";
    if (SUCCEEDED(dev->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len))) {
        s = toUtf8(std::wstring(name, len));
        CoTaskMemFree(name);
    }
    return s;
}

}  // namespace

std::vector<CameraInfo> enumerateCameras() {
    std::vector<CameraInfo> out;
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(listDevices(&devices, &count))) return out;
    for (UINT32 i = 0; i < count; ++i) {
        out.push_back({friendlyName(devices[i])});
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return out;
}

MfCamera::~MfCamera() { close(); }

void MfCamera::close() {
    safeRelease(reader_);
    if (source_) source_->Shutdown();
    safeRelease(source_);
}

bool MfCamera::open(int index, int width, int height, int maxFps, std::string* error) {
    close();
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    HRESULT hr = listDevices(&devices, &count);
    if (FAILED(hr)) {
        if (error) *error = hrText("MFEnumDeviceSources", hr);
        return false;
    }
    std::string name;
    if (index >= 0 && static_cast<UINT32>(index) < count) {
        name = friendlyName(devices[index]);
        hr = devices[index]->ActivateObject(IID_PPV_ARGS(&source_));
    } else {
        hr = E_INVALIDARG;
    }
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    CoTaskMemFree(devices);
    if (FAILED(hr) || !source_) {
        if (error) {
            *error = count == 0 ? "No camera found" : hr == E_INVALIDARG ? "Camera #" + std::to_string(index) + " not found"
                                                                           : hrText("Opening the camera", hr);
        }
        return false;
    }

    IMFAttributes* attr = nullptr;
    hr = MFCreateAttributes(&attr, 2);
    if (SUCCEEDED(hr)) hr = attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr)) hr = attr->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source_, attr, &reader_);
    safeRelease(attr);
    if (FAILED(hr)) {
        if (error) *error = hrText("MFCreateSourceReaderFromMediaSource", hr);
        close();
        return false;
    }
    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

    // Pick the native mode: closest resolution, then the highest rate up to maxFps.
    IMFMediaType* best = nullptr;
    double bestPenalty = 1e30, bestFps = 0.0;
    for (DWORD i = 0;; ++i) {
        IMFMediaType* t = nullptr;
        if (FAILED(reader_->GetNativeMediaType(stream, i, &t))) break;
        UINT32 w = 0, h = 0, num = 0, den = 1;
        MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
        double fps = den ? static_cast<double>(num) / den : 0.0;
        double resPenalty = (w && h) ? std::fabs(std::log(static_cast<double>(w) * h / (static_cast<double>(width) * height))) * 20.0 : 1e9;
        double fpsPenalty = fps > maxFps + 0.5 ? (fps - maxFps) : (maxFps - fps) * 2.0;
        double penalty = resPenalty + fpsPenalty;
        if (penalty < bestPenalty) {
            safeRelease(best);
            best = t;
            best->AddRef();
            bestPenalty = penalty;
            bestFps = fps;
        }
        safeRelease(t);
    }
    if (best) {
        reader_->SetCurrentMediaType(stream, nullptr, best);
        safeRelease(best);
    }

    IMFMediaType* outType = nullptr;
    hr = MFCreateMediaType(&outType);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = reader_->SetCurrentMediaType(stream, nullptr, outType);
    safeRelease(outType);
    if (FAILED(hr)) {
        if (error) *error = hrText("Setting RGB32 output", hr);
        close();
        return false;
    }

    IMFMediaType* cur = nullptr;
    hr = reader_->GetCurrentMediaType(stream, &cur);
    if (SUCCEEDED(hr)) {
        UINT32 w = 0, h = 0, s = 0;
        MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
        width_ = static_cast<int>(w);
        height_ = static_cast<int>(h);
        if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &s))) {
            stride_ = static_cast<INT32>(s);
        } else {
            LONG st = 0;
            MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &st);
            stride_ = st;
        }
        safeRelease(cur);
    }
    reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader_->SetStreamSelection(stream, TRUE);

    fps_ = bestFps > 0 ? bestFps : maxFps;
    char desc[256];
    std::snprintf(desc, sizeof desc, "%s  %dx%d @ %.0f fps", name.c_str(), width_, height_, fps_);
    description_ = desc;
    logInfo("camera opened: " + description_);
    return width_ > 0 && height_ > 0;
}

bool MfCamera::read(Frame& out, std::string* error) {
    if (!reader_) {
        if (error) *error = "camera not open";
        return false;
    }
    for (;;) {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &streamIndex,
                                         &flags, &ts, &sample);
        if (FAILED(hr)) {
            if (error) *error = hrText("ReadSample", hr);
            return false;
        }
        if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) {
            safeRelease(sample);
            if (error) *error = "The camera stopped delivering frames";
            return false;
        }
        if (!sample) continue;  // stream tick / format change without data

        IMFMediaBuffer* buf = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buf);
        if (FAILED(hr)) {
            safeRelease(sample);
            continue;
        }
        out.resize(width_, height_);
        const size_t rowBytes = static_cast<size_t>(width_) * 4;
        IMF2DBuffer* buf2d = nullptr;
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch))) {
            for (int y = 0; y < height_; ++y)
                std::memcpy(out.bgra.data() + y * rowBytes, scan0 + static_cast<ptrdiff_t>(y) * pitch, rowBytes);
            buf2d->Unlock2D();
        } else {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            if (SUCCEEDED(buf->Lock(&data, &maxLen, &curLen))) {
                LONG p = stride_ ? stride_ : static_cast<LONG>(rowBytes);
                BYTE* first = p < 0 ? data + static_cast<size_t>(height_ - 1) * static_cast<size_t>(-p) : data;
                if (curLen >= static_cast<DWORD>(std::labs(p)) * static_cast<DWORD>(height_))
                    for (int y = 0; y < height_; ++y)
                        std::memcpy(out.bgra.data() + y * rowBytes, first + static_cast<ptrdiff_t>(y) * p, rowBytes);
                buf->Unlock();
            }
        }
        safeRelease(buf2d);
        safeRelease(buf);
        safeRelease(sample);
        out.arrivalUs = Clock::nowUs();
        out.index = counter_++;
        return true;
    }
}

}  // namespace mausely
