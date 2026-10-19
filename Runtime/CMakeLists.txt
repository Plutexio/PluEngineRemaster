project(Runtime)

# Bootstrap reflection sources on the first configure so the GLOB below has files to
# collect. On later builds the PRE_BUILD command keeps them regenerated, so we skip the
# configure-time run to avoid generating twice whenever CMake reconfigures.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ReflectionCache/Runtime")
    execute_process(
            COMMAND ${VENV_EXECUTABLE} "${CMAKE_SOURCE_DIR}/PythonTools/ReflectionGeneratorRegex.py" -bp Runtime
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

file(GLOB_RECURSE SOURCES_REFLECTION CONFIGURE_DEPENDS "../ReflectionCache/Runtime/*.cpp")

add_executable(PluRuntime WIN32 ${SOURCES_REFLECTION}
        RuntimeApp.cpp
        RuntimeApp.h
        Mains/WindowsMain.cpp
        Mains/LinuxMain.cpp
        Shaders/RuntimeShaderManager.h
        Shaders/RuntimeShaderManager.cpp
        Shaders/RuntimeShaderCode.cpp
        Shaders/RuntimeShaderCode.h
        Python/RuntimePythonRunner.cpp
        Python/RuntimePythonRunner.h)

target_link_libraries(PluRuntime PUBLIC Engine)

add_custom_command(TARGET PluRuntime
        PRE_BUILD
        COMMENT "Generating Reflection Data"
        COMMAND ${VENV_EXECUTABLE} "${CMAKE_SOURCE_DIR}/PythonTools/ReflectionGeneratorRegex.py" -bp Runtime
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

set_target_properties(PluRuntime PROPERTIES UNITY_BUILD ON)

find_package(argparse CONFIG REQUIRED)
target_link_libraries(PluRuntime PRIVATE argparse::argparse)
find_package(Jolt CONFIG REQUIRED)
target_link_libraries(PluRuntime PRIVATE Jolt::Jolt)
target_compile_definitions(PluRuntime PRIVATE JPH_DEBUG_RENDERER)

target_include_directories(PluRuntime PUBLIC ../ReflectionCache/Runtime)

set_target_properties(PluRuntime PROPERTIES
        # Distributed layout: every runtime library sits next to the executable.
        INSTALL_RPATH "$ORIGIN"
        # But not while building. The vcpkg dependencies are dynamic now (x64-linux-dynamic), so a
        # binary in the build tree has to reach into vcpkg_installed/ for libJolt.so and friends --
        # which is exactly what CMake's automatic build RPATH does. Forcing the install RPATH here
        # instead would leave the loader with nothing but $ORIGIN and the app would not start.
        BUILD_WITH_INSTALL_RPATH FALSE
        # Keeps libEngine.so, which the POST_BUILD step copies next to the executable, findable too.
        BUILD_RPATH "$ORIGIN"
)

# Engine is an INTERFACE target now, so there is no single $<TARGET_FILE:Engine> to copy: every
# module is its own shared library and all of them have to sit next to the executable, which is
# what the $ORIGIN RPATH expects to find. glad and spdlog are shared for the same reason (one
# function-pointer table / logger registry for the whole process) and travel with them.
foreach (PLU_MODULE IN LISTS PLU_MODULES PLU_THIRDPARTY_SHARED_LIBS)
    add_custom_command(TARGET PluRuntime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PLU_MODULE}>
            $<TARGET_FILE_DIR:PluRuntime>
    )
endforeach ()