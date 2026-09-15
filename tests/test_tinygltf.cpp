// Smoke test for the vendored tinygltf library itself (isolated from
// TmdGltfExport/Import - see test_gltf_roundtrip.cpp for that): build a
// trivial in-memory model, write it out, read it back, to confirm the
// vendored header actually links and runs, not just parses.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include <cstdio>

int main() {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    model.asset.version = "2.0";
    model.asset.generator = "tinygltf smoke test";

    tinygltf::Buffer buffer;
    buffer.data = { 0, 0, 0, 0 };
    model.buffers.push_back(buffer);

    tinygltf::Scene scene;
    model.scenes.push_back(scene);
    model.defaultScene = 0;

    const char* path = "tinygltf_smoke_tmp.gltf";
    bool write_ok = loader.WriteGltfSceneToFile(&model, path, false, false, true, false);
    printf("write_ok=%d\n", write_ok);

    tinygltf::Model reloaded;
    bool load_ok = loader.LoadASCIIFromFile(&reloaded, &err, &warn, path);
    printf("load_ok=%d err='%s' warn='%s'\n", load_ok, err.c_str(), warn.c_str());
    printf("reloaded buffers=%zu\n", reloaded.buffers.size());
    return (write_ok && load_ok) ? 0 : 1;
}
