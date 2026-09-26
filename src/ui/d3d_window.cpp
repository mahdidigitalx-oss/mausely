#include "ui/d3d_window.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace mausely {
namespace {

template <class T>
void safeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void applyTheme(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 6.f;
    s.ChildRounding = 6.f;
    s.FrameRounding = 4.f;
    s.GrabRounding = 4.f;
    s.PopupRounding = 4.f;
    s.WindowBorderSize = 0.f;
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = s.Colors;
    const ImVec4 bg(0.075f, 0.082f, 0.098f, 1.f), panel(0.105f, 0.115f, 0.135f, 1.f);
    const ImVec4 accent(0.24f, 0.62f, 0.96f, 1.f), accentDim(0.20f, 0.45f, 0.72f, 1.f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.26f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.25f, 0.30f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.17f, 0.19f, 0.23f, 1.f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.21f, 0.24f, 0.29f, 1.f);
    c[ImGuiCol_HeaderActive] = accentDim;
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.24f, 1.f);
    c[ImGuiCol_ButtonHovered] = accentDim;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.28f, 1.f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.15f, 0.18f, 1.f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.f, 1.f, 1.f, 0.025f);
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

}  // namespace

D3dWindow::~D3dWindow() {
    if (imguiReady_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
    releaseRenderTarget();
    safeRelease(swapChain_);
    safeRelease(context_);
    safeRelease(device_);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool D3dWindow::create(const wchar_t* title, int width, int height, std::string* error) {
    ImGui_ImplWin32_EnableDpiAwareness();
    dpiScale_ = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = &D3dWindow::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(wc.hInstance, L"IDI_APP");
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MauselyWindow";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            static_cast<int>(width * dpiScale_), static_cast<int>(height * dpiScale_), nullptr,
                            nullptr, wc.hInstance, this);
    if (!hwnd_) {
        if (error) *error = "CreateWindow failed";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &got, &context_);
    if (hr == DXGI_ERROR_UNSUPPORTED)  // no usable GPU: software rasteriser
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &swapChain_, &device_, &got, &context_);
    if (FAILED(hr)) {
        if (error) *error = "Direct3D 11 is not available";
        return false;
    }
    createRenderTarget();
    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // layout is fixed; settings live in settings.ini
    applyTheme(dpiScale_);
    ImGui::GetStyle().FontSizeBase = 16.f;
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf")) io.Fonts->AddFontDefault();
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);
    imguiReady_ = true;
    return true;
}

void D3dWindow::createRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        device_->CreateRenderTargetView(back, nullptr, &rtv_);
        back->Release();
    }
}

void D3dWindow::releaseRenderTarget() { safeRelease(rtv_); }

bool D3dWindow::pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) return false;
    }
    return true;
}

bool D3dWindow::beginFrame() {
    if (occluded_ && swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        Sleep(15);
        return false;
    }
    occluded_ = false;
    if (IsIconic(hwnd_)) {
        Sleep(30);
        return false;
    }
    if (resizeW_ && resizeH_) {
        releaseRenderTarget();
        swapChain_->ResizeBuffers(0, resizeW_, resizeH_, DXGI_FORMAT_UNKNOWN, 0);
        resizeW_ = resizeH_ = 0;
        createRenderTarget();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void D3dWindow::endFrame(const std::wstring& screenshotPath) {
    ImGui::Render();
    const float clear[4] = {0.075f, 0.082f, 0.098f, 1.f};
    context_->OMSetRenderTargets(1, &rtv_, nullptr);
    context_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (!screenshotPath.empty()) saveBackBuffer(screenshotPath);
    HRESULT hr = swapChain_->Present(1, 0);  // vsync
    occluded_ = hr == DXGI_STATUS_OCCLUDED;
}

bool D3dWindow::saveBackBuffer(const std::wstring& path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    bool ok = SUCCEEDED(device_->CreateTexture2D(&d, nullptr, &staging));
    if (ok) {
        context_->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE m;
        ok = SUCCEEDED(context_->Map(staging, 0, D3D11_MAP_READ, 0, &m));
        if (ok) {
            // 32-bit top-down BMP (BGRA); the back buffer is RGBA.
            const int w = static_cast<int>(d.Width), h = static_cast<int>(d.Height);
            std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
            for (int y = 0; y < h; ++y) {
                const unsigned char* src = static_cast<const unsigned char*>(m.pData) + y * m.RowPitch;
                unsigned char* dst = px.data() + static_cast<size_t>(y) * w * 4;
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = 255;
                }
            }
            context_->Unmap(staging, 0);
            BITMAPFILEHEADER fh{};
            BITMAPINFOHEADER ih{};
            ih.biSize = sizeof ih;
            ih.biWidth = w;
            ih.biHeight = -h;
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            ih.biCompression = BI_RGB;
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof fh + sizeof ih;
            fh.bfSize = static_cast<DWORD>(fh.bfOffBits + px.size());
            FILE* f = _wfopen(path.c_str(), L"wb");
            ok = f != nullptr;
            if (f) {
                std::fwrite(&fh, sizeof fh, 1, f);
                std::fwrite(&ih, sizeof ih, 1, f);
                std::fwrite(px.data(), 1, px.size(), f);
                std::fclose(f);
            }
        }
        staging->Release();
    }
    back->Release();
    return ok;
}

LRESULT CALLBACK D3dWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<D3dWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;  // messages arrive before CreateWindowExW returns
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<D3dWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    if (self) return self->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT D3dWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                resizeW_ = LOWORD(lp);
                resizeH_ = HIWORD(lp);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = {static_cast<LONG>(900 * dpiScale_), static_cast<LONG>(560 * dpiScale_)};
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            dpiScale_ = HIWORD(wp) / 96.f;
            if (imguiReady_) {
                float size = ImGui::GetStyle().FontSizeBase;
                applyTheme(dpiScale_);
                ImGui::GetStyle().FontSizeBase = size;
            }
            return 0;
        }
        case WM_HOTKEY:
            if (onHotkey) onHotkey(static_cast<int>(wp));
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // no ALT menu beep
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void DynamicTexture::release() {
    safeRelease(srv_);
    safeRelease(tex_);
    width_ = height_ = 0;
}

bool DynamicTexture::update(ID3D11Device* device, ID3D11DeviceContext* ctx, const unsigned char* bgra, int width,
                            int height) {
    if (width != width_ || height != height_ || !tex_) {
        release();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = static_cast<UINT>(width);
        d.Height = static_cast<UINT>(height);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8X8_UNORM;  // camera alpha byte is undefined
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &tex_))) return false;
        if (FAILED(device->CreateShaderResourceView(tex_, nullptr, &srv_))) {
            release();
            return false;
        }
        width_ = width;
        height_ = height;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(tex_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    const size_t row = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        std::memcpy(static_cast<unsigned char*>(m.pData) + y * m.RowPitch, bgra + y * row, row);
    ctx->Unmap(tex_, 0);
    return true;
}

}  // namespace mausely
