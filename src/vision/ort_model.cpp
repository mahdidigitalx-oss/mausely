#include "vision/ort_model.h"

#include <windows.h>

#include <onnxruntime_c_api.h>

#include "core/log.h"

namespace mausely {
namespace {

// Converts an OrtStatus into an error string (and releases it). Returns true on success.
bool check(const OrtApi* api, OrtStatus* status, std::string* error, const char* what) {
    if (!status) return true;
    if (error) *error = std::string(what) + ": " + api->GetErrorMessage(status);
    api->ReleaseStatus(status);
    return false;
}

}  // namespace

OrtRuntime& OrtRuntime::instance() {
    static OrtRuntime rt;
    return rt;
}

bool OrtRuntime::init(const std::wstring& dllPath, std::string* error) {
    if (api_) return true;
    // Full path: never pick up an older onnxruntime.dll from System32.
    HMODULE dll = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) {
        if (error) {
            *error = "Cannot load " + toUtf8(dllPath) + " (error " + std::to_string(GetLastError()) +
                     "). Install the Microsoft Visual C++ 2015-2022 x64 Redistributable if it is missing.";
        }
        return false;
    }
    using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)();
    auto getApiBase = reinterpret_cast<GetApiBaseFn>(reinterpret_cast<void*>(GetProcAddress(dll, "OrtGetApiBase")));
    if (!getApiBase) {
        if (error) *error = "OrtGetApiBase not found in " + toUtf8(dllPath);
        return false;
    }
    const OrtApiBase* base = getApiBase();
    const OrtApi* api = base->GetApi(ORT_API_VERSION);
    if (!api) {
        if (error) *error = std::string("onnxruntime.dll ") + base->GetVersionString() + " is older than required API " +
                            std::to_string(ORT_API_VERSION);
        return false;
    }
    OrtEnv* env = nullptr;
    if (!check(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "mausely", &env), error, "CreateEnv")) return false;
    api_ = api;
    env_ = env;
    version_ = base->GetVersionString();
    logInfo("ONNX Runtime " + version_ + " loaded");
    return true;
}

OrtModel::OrtModel() = default;

OrtModel::~OrtModel() { release(); }

void OrtModel::release() {
    const OrtApi* api = OrtRuntime::instance().api();
    if (api && session_) api->ReleaseSession(session_);
    if (api && memInfo_) api->ReleaseMemoryInfo(memInfo_);
    session_ = nullptr;
    memInfo_ = nullptr;
    inputs_.clear();
    outputs_.clear();
    inputNames_.clear();
    outputNames_.clear();
}

bool OrtModel::load(const std::wstring& path, int intraOpThreads, std::string* error) {
    release();  // reloading after a settings restart
    bool ok = loadSession(path, intraOpThreads, error);
    if (!ok) release();  // never leave a half-initialised model behind
    return ok;
}

bool OrtModel::loadSession(const std::wstring& path, int intraOpThreads, std::string* error) {
    OrtRuntime& rt = OrtRuntime::instance();
    const OrtApi* api = rt.api();
    if (!api) {
        if (error) *error = "ONNX Runtime is not initialised";
        return false;
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (error) *error = "Model file not found: " + toUtf8(path);
        return false;
    }

    OrtSessionOptions* opts = nullptr;
    if (!check(api, api->CreateSessionOptions(&opts), error, "CreateSessionOptions")) return false;
    bool ok = check(api, api->SetIntraOpNumThreads(opts, intraOpThreads), error, "SetIntraOpNumThreads") &&
              check(api, api->SetInterOpNumThreads(opts, 1), error, "SetInterOpNumThreads") &&
              check(api, api->SetSessionExecutionMode(opts, ORT_SEQUENTIAL), error, "SetSessionExecutionMode") &&
              check(api, api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL), error, "SetOptLevel") &&
              // Worker threads sleep instead of spinning: the app runs all day
              // next to other programs, so idle CPU matters more than ~0.1 ms.
              check(api, api->AddSessionConfigEntry(opts, "session.intra_op.allow_spinning", "0"), error, "spinning") &&
              check(api, api->CreateSession(rt.env(), path.c_str(), opts, &session_), error, "CreateSession");
    api->ReleaseSessionOptions(opts);
    if (!ok) return false;

    if (!check(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memInfo_), error, "MemoryInfo"))
        return false;

    OrtAllocator* alloc = nullptr;
    if (!check(api, api->GetAllocatorWithDefaultOptions(&alloc), error, "Allocator")) return false;

    auto readInfo = [&](bool input, size_t i, TensorInfo& info) -> bool {
        char* name = nullptr;
        OrtStatus* st = input ? api->SessionGetInputName(session_, i, alloc, &name)
                              : api->SessionGetOutputName(session_, i, alloc, &name);
        if (!check(api, st, error, "GetName")) return false;
        info.name = name;
        api->AllocatorFree(alloc, name);

        OrtTypeInfo* typeInfo = nullptr;
        st = input ? api->SessionGetInputTypeInfo(session_, i, &typeInfo)
                   : api->SessionGetOutputTypeInfo(session_, i, &typeInfo);
        if (!check(api, st, error, "GetTypeInfo")) return false;
        const OrtTensorTypeAndShapeInfo* tinfo = nullptr;
        size_t dims = 0;
        bool good = check(api, api->CastTypeInfoToTensorInfo(typeInfo, &tinfo), error, "CastTypeInfo") && tinfo &&
                    check(api, api->GetDimensionsCount(tinfo, &dims), error, "GetDimensionsCount");
        if (good) {
            info.shape.resize(dims);
            good = check(api, api->GetDimensions(tinfo, info.shape.data(), dims), error, "GetDimensions");
        }
        api->ReleaseTypeInfo(typeInfo);
        return good;
    };

    size_t nIn = 0, nOut = 0;
    if (!check(api, api->SessionGetInputCount(session_, &nIn), error, "InputCount")) return false;
    if (!check(api, api->SessionGetOutputCount(session_, &nOut), error, "OutputCount")) return false;
    inputs_.resize(nIn);
    outputs_.resize(nOut);
    for (size_t i = 0; i < nIn; ++i)
        if (!readInfo(true, i, inputs_[i])) return false;
    for (size_t i = 0; i < nOut; ++i)
        if (!readInfo(false, i, outputs_[i])) return false;
    for (auto& t : inputs_) inputNames_.push_back(t.name.c_str());
    for (auto& t : outputs_) outputNames_.push_back(t.name.c_str());
    return true;
}

int OrtModel::outputIndex(const std::string& name) const {
    for (size_t i = 0; i < outputs_.size(); ++i)
        if (outputs_[i].name == name) return static_cast<int>(i);
    return -1;
}

bool OrtModel::run(const std::vector<const float*>& inputData,
                   const std::vector<std::vector<int64_t>>& inputShapes,
                   std::vector<Tensor>& outputs,
                   std::string* error) {
    const OrtApi* api = OrtRuntime::instance().api();
    if (!session_) {
        if (error) *error = "model not loaded";
        return false;
    }
    if (inputData.size() != inputs_.size() || inputShapes.size() != inputs_.size()) {
        if (error) *error = "wrong number of inputs";
        return false;
    }

    std::vector<OrtValue*> in(inputs_.size(), nullptr);
    std::vector<OrtValue*> out(outputs_.size(), nullptr);
    auto releaseAll = [&] {
        for (OrtValue* v : in)
            if (v) api->ReleaseValue(v);
        for (OrtValue* v : out)
            if (v) api->ReleaseValue(v);
    };

    for (size_t i = 0; i < in.size(); ++i) {
        size_t count = 1;
        for (int64_t d : inputShapes[i]) count *= static_cast<size_t>(d);
        OrtStatus* st = api->CreateTensorWithDataAsOrtValue(
            memInfo_, const_cast<float*>(inputData[i]), count * sizeof(float), inputShapes[i].data(),
            inputShapes[i].size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[i]);
        if (!check(api, st, error, "CreateTensor")) {
            releaseAll();
            return false;
        }
    }

    OrtStatus* st = api->Run(session_, nullptr, inputNames_.data(), in.data(), in.size(), outputNames_.data(),
                             out.size(), out.data());
    if (!check(api, st, error, "Run")) {
        releaseAll();
        return false;
    }

    outputs.resize(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        OrtTensorTypeAndShapeInfo* info = nullptr;
        size_t count = 0, dims = 0;
        float* data = nullptr;
        bool good = check(api, api->GetTensorTypeAndShape(out[i], &info), error, "TypeAndShape") &&
                    check(api, api->GetTensorShapeElementCount(info, &count), error, "ElementCount") &&
                    check(api, api->GetDimensionsCount(info, &dims), error, "Dims");
        if (good) {
            outputs[i].shape.resize(dims);
            good = check(api, api->GetDimensions(info, outputs[i].shape.data(), dims), error, "GetDimensions") &&
                   check(api, api->GetTensorMutableData(out[i], reinterpret_cast<void**>(&data)), error, "Data");
        }
        if (info) api->ReleaseTensorTypeAndShapeInfo(info);
        if (!good) {
            releaseAll();
            return false;
        }
        outputs[i].data.assign(data, data + count);
    }
    releaseAll();
    return true;
}

std::string OrtModel::metadata(const std::string& key) const {
    const OrtApi* api = OrtRuntime::instance().api();
    if (!session_) return {};
    OrtModelMetadata* meta = nullptr;
    if (!check(api, api->SessionGetModelMetadata(session_, &meta), nullptr, "")) return {};
    OrtAllocator* alloc = nullptr;
    std::string result;
    char* value = nullptr;
    if (check(api, api->GetAllocatorWithDefaultOptions(&alloc), nullptr, "") &&
        check(api, api->ModelMetadataLookupCustomMetadataMap(meta, alloc, key.c_str(), &value), nullptr, "") &&
        value) {
        result = value;
        api->AllocatorFree(alloc, value);
    }
    api->ReleaseModelMetadata(meta);
    return result;
}

}  // namespace mausely
