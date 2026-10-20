project(PluTests)

# The container tests themselves live in the PluSTL submodule (PluSTL/tests, target
# PluSTLTests) and are enabled from the root CMakeLists through PLUSTL_BUILD_TESTS. This
# directory holds the engine-side tests that reuse its runner.

add_executable(PluEngineTests
        ${CMAKE_SOURCE_DIR}/PluSTL/tests/Main.cpp
        PluSTLTests/Test_GlmHash.cpp
)

target_include_directories(PluEngineTests PRIVATE
        ${CMAKE_SOURCE_DIR}/PluSTL/tests       # TestFramework.h
        # Test_GlmHash.cpp covers PluEngine/Core/GlmHash.h — the DefaultHash
        # specializations for Vec2/3/4, Quaternion and Matrix4. That header depends on
        # glm and PluSTL only, both header-only, so covering it here costs no engine
        # dependency: nothing below links or builds any PluEngine target.
        ${CMAKE_SOURCE_DIR}/ThirdParty/glm
        ${CMAKE_SOURCE_DIR}/LibEngine/PluCore/include)

target_link_libraries(PluEngineTests PRIVATE PluSTL::PluSTL)

if(PLU_TESTS_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(PluEngineTests PRIVATE -fsanitize=thread -g -O1 -fno-omit-frame-pointer)
        target_link_options(PluEngineTests PRIVATE -fsanitize=thread)
    endif()
endif()

add_test(NAME PluEngineTests COMMAND PluEngineTests)
