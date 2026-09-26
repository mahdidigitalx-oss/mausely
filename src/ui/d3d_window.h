#pragma once

#include <windows.h>

#include <functional>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

namespace mausely {

// Win32 window + Direct3D 11 swap chain + Dear ImGui/ImPlot contexts.
class D3dWindow {
public:
    ~D3dWindow();

    bool create(const wchar_t* title, int width, int height, std::string* error);
    // Processes pending messages; returns false once the window was closed.
    bool pumpMessages();
    // Returns false when rendering should be skipped (minimised / occluded).
    bool beginFrame();
    // Presents the frame; if screenshotPath is set, also saves it as a .bmp first.
    void endFrame(const std::wstring& screenshotPath = {});

    HWND hwnd() const { return hwnd_; }
    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }
    float dpiScale() const { return dpiScale_; }

    std::function<void(int id)> onHotkey;

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void createRenderTarget();
    void releaseRenderTarget();
    bool saveBackBuffer(const std::wstring& path);

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    UINT resizeW_ = 0, resizeH_ = 0;
    bool occluded_ = false;
    bool imguiReady_ = false;
    float dpiScale_ = 1.f;
};

// A BGRX texture that is updated from CPU frames and drawn with ImGui::Image.
class DynamicTexture {
public:
    ~DynamicTexture() { release(); }
    bool update(ID3D11Device* device, ID3D11DeviceContext* ctx, const unsigned char* bgra, int width, int height);
    ID3D11ShaderResourceView* srv() const { return srv_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void release();
    ID3D11Texture2D* tex_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    int width_ = 0, height_ = 0;
};

}  // namespace mausely
