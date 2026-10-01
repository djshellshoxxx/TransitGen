add_library(transitgen_core STATIC
    src/FillPlan.cpp
    src/Transport.cpp
    src/CaptureBuffer.cpp
    src/PlanPlayer.cpp
    src/FillScheduler.cpp
    src/Engine.cpp
    src/TestPlanProvider.cpp
)
target_include_directories(transitgen_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(transitgen_core PUBLIC cxx_std_20)

# Determinism: identical float results regardless of block size require no FMA
# contraction and no fast-math (02 §8).
set(TG_STRICT_FP_FLAGS -ffp-contract=off -fno-fast-math)
set(TG_WARNING_FLAGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion)
target_compile_options(transitgen_core PRIVATE ${TG_STRICT_FP_FLAGS} ${TG_WARNING_FLAGS})
target_compile_options(transitgen_core PUBLIC ${TG_STRICT_FP_FLAGS})

if(TG_SANITIZE)
    target_compile_options(transitgen_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(transitgen_core PUBLIC -fsanitize=address,undefined)
    target_compile_definitions(transitgen_core PUBLIC TG_SANITIZED=1)
endif()

if(TG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
