# Factory styles (styles/*.json) are embedded as generated source (04, 07 §2).
file(GLOB TG_FACTORY_STYLES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/styles/*.json)
list(SORT TG_FACTORY_STYLES)
set(TG_FACTORY_STYLES_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/FactoryStyles.cpp)
add_custom_command(
    OUTPUT  ${TG_FACTORY_STYLES_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${TG_FACTORY_STYLES_CPP} "-DINPUTS=${TG_FACTORY_STYLES}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedStyles.cmake
    DEPENDS ${TG_FACTORY_STYLES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedStyles.cmake
    COMMENT "Embedding factory styles"
    VERBATIM)

add_library(transitgen_core STATIC
    src/FillPlan.cpp
    src/Transport.cpp
    src/CaptureBuffer.cpp
    src/PlanPlayer.cpp
    src/FillScheduler.cpp
    src/Engine.cpp
    src/TestPlanProvider.cpp
    src/Json.cpp
    src/StyleLoader.cpp
    src/Generator.cpp
    src/GeneratorPlanProvider.cpp
    ${TG_FACTORY_STYLES_CPP}
)
target_include_directories(transitgen_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(transitgen_core PUBLIC cxx_std_20)

# Determinism: identical float results regardless of block size require no FMA
# contraction and no fast-math (02 §8).
if(MSVC)
    set(TG_STRICT_FP_FLAGS /fp:precise)
    set(TG_WARNING_FLAGS /W4 /permissive-)
else()
    set(TG_STRICT_FP_FLAGS -ffp-contract=off -fno-fast-math)
    set(TG_WARNING_FLAGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion)
endif()
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
