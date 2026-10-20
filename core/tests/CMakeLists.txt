include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(transitgen_tests
    TestUtil.cpp
    test_plan.cpp
    test_transport.cpp
    test_scheduler.cpp
    test_sources.cpp
    test_modifiers.cpp
    test_clicks.cpp
    test_invariance.cpp
    GenTestUtil.cpp
    test_rng.cpp
    test_styles.cpp
    test_generator.cpp
    test_generator_engine.cpp
)
target_link_libraries(transitgen_tests PRIVATE transitgen_core Catch2::Catch2WithMain)
target_compile_options(transitgen_tests PRIVATE ${TG_WARNING_FLAGS})
# Golden plans (03 §8): regenerate with `TG_UPDATE_GOLDEN=1 ./transitgen_tests "[golden]"`.
target_compile_definitions(transitgen_tests PRIVATE TG_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(transitgen_tests)
