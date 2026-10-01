# Headless processor tests: the real wrapper code driven by a fake playhead (M3).
FetchContent_GetProperties(Catch2)
if(NOT TARGET Catch2::Catch2)
    message(FATAL_ERROR "plugin tests need Catch2 from core/tests (TG_BUILD_TESTS=ON)")
endif()

add_executable(transitgen_plugin_tests test_plugin.cpp)
target_link_libraries(transitgen_plugin_tests PRIVATE transitgen_plugin_shared Catch2::Catch2)
# Source properties are directory-scoped: apply the wrapper flags to this target's copies too.
set_source_files_properties(test_plugin.cpp ${TG_PLUGIN_SOURCES} PROPERTIES COMPILE_OPTIONS "${TG_PLUGIN_WARNINGS}")

add_test(NAME transitgen_plugin_tests COMMAND transitgen_plugin_tests)
