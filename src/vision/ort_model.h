#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct OrtApi;
struct OrtEnv;
struct OrtSession;
struct OrtMemoryInfo;

namespace mausely {

// File name of the ONNX Runtime shared library on this platform.
#ifdef _WIN32
inline constexpr wchar_t kOrtLibraryName[] = L"onnxruntime.dll";
#else
inline constexpr wchar_t kOrtLibraryName[] = L"libonnxruntime.so";
#endif

// Loads the ONNX Runtime shared library at runtime and owns the global OrtEnv.
class OrtRuntime {
public:
    static OrtRuntime& instance();

    // Loads the library from `dllPath` (idempotent). A bare file name uses the
    // platform's search path (Android: the app's native library folder).
    // Returns false with `error` set on failure.
    bool init(const std::wstring& dllPath, std::string* error);
    bool ready() const { return api_ != nullptr; }
    const OrtApi* api() const { return api_; }
    OrtEnv* env() const { return env_; }
    std::string version() const { return version_; }

private:
    OrtRuntime() = default;
    const OrtApi* api_ = nullptr;
    OrtEnv* env_ = nullptr;
    std::string version_;
};

struct TensorInfo {
    std::string name;
    std::vector<int64_t> shape;  // -1 for dynamic dims
};

struct Tensor {
    std::vector<float> data;
    std::vector<int64_t> shape;
};

// One ONNX model session with float32 inputs/outputs.
class OrtModel {
public:
    OrtModel();
    ~OrtModel();
    OrtModel(const OrtModel&) = delete;
    OrtModel& operator=(const OrtModel&) = delete;

    bool load(const std::wstring& path, int intraOpThreads, std::string* error);
    bool loaded() const { return session_ != nullptr; }

    const std::vector<TensorInfo>& inputs() const { return inputs_; }
    const std::vector<TensorInfo>& outputs() const { return outputs_; }
    // Index of the output whose name equals `name`, or -1.
    int outputIndex(const std::string& name) const;

    // Runs with one pointer per model input (shapes given explicitly); fills
    // every model output in declaration order.
    bool run(const std::vector<const float*>& inputData,
             const std::vector<std::vector<int64_t>>& inputShapes,
             std::vector<Tensor>& outputs,
             std::string* error);

    // Custom metadata value stored in the model (empty if absent).
    std::string metadata(const std::string& key) const;

private:
    bool loadSession(const std::wstring& path, int intraOpThreads, std::string* error);
    void release();

    OrtSession* session_ = nullptr;
    OrtMemoryInfo* memInfo_ = nullptr;
    std::vector<TensorInfo> inputs_;
    std::vector<TensorInfo> outputs_;
    std::vector<const char*> inputNames_;
    std::vector<const char*> outputNames_;
};

}  // namespace mausely
