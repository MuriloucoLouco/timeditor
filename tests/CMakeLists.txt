# Each test is a small, independent executable exercising one subsystem
# directly against TIMEditorCore (the ImGui-free core/gfx object library
# defined in the root CMakeLists.txt) - plain `int main()`, nonzero exit on
# failure, printed OK/FAILED lines per assertion. `ctest` from the build
# directory runs all of them.
#
# tests/support/synthetic_tmd.h holds a small in-memory TMD_Model shared by
# the OBJ/glTF round-trip tests, standing in for a real .tmd game asset
# (which this repo can't commit).

set(TIMEDITOR_TEST_NAMES
    test_ray
    test_mat_inverse
    test_mesh_ops
    test_object_resolve
    test_mesh_import_splitter
    test_obj_roundtrip
    test_gltf_roundtrip
)

foreach(test_name ${TIMEDITOR_TEST_NAMES})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE TIMEditorCore)
    add_test(NAME ${test_name} COMMAND ${test_name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

# test_fonts additionally needs ImGui (core only - no demo/backends, no GL
# context) and the app's own font-loading code - not part of TIMEditorCore,
# which stays ImGui-free on purpose (see docs/ARCHITECTURE.md).
add_executable(test_fonts test_fonts.cpp ../src/ui/icon_font.cpp ${IMGUI_CORE_SOURCES})
target_link_libraries(test_fonts PRIVATE TIMEditorCore)
add_test(NAME test_fonts COMMAND test_fonts WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Deliberately NOT linked against TIMEditorCore: this is an isolated smoke
# test of the vendored tinygltf library itself (TINYGLTF_IMPLEMENTATION
# provides its own definitions of tinygltf::FileExists/URIDecode/etc, which
# would collide at link time with the copy TIMEditorCore's own
# tmd_gltf_export.cpp/tmd_gltf_import.cpp already provide).
add_executable(test_tinygltf test_tinygltf.cpp)
add_test(NAME test_tinygltf COMMAND test_tinygltf WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
