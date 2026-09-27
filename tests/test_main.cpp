#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>

#include "core/log.h"
#include "vision/ort_model.h"

int main(int argc, char** argv) {
    // Integration tests need ONNX Runtime; unit tests run regardless.
    std::string err;
    if (!mausely::OrtRuntime::instance().init(mausely::exeDirectory() + mausely::kOrtLibraryName, &err))
        std::fprintf(stderr, "warning: %s\n", err.c_str());
    doctest::Context ctx(argc, argv);
    return ctx.run();
}
