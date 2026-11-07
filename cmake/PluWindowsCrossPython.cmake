# Python for the Linux -> Windows cross build, included from the root CMakeLists.txt.
#
# The engine embeds CPython through pybind11, so a Windows build needs the Windows headers and import
# library — and a Linux host has neither: FindPython would happily hand out the host's Linux headers.
# Two official packages of the same version are fetched into the build directory instead:
#   - the NuGet "python" package: include/ and libs/python3xx.lib to build against;
#   - the python.org embeddable package: python3xx.dll, the zipped stdlib and the extension modules,
#     which the distribution scripts put next to the executable, so the result runs on a machine
#     without Python installed.
#
# Keep the minor version equal to the host Python's (the build venv): LibEngine asks FindPython for the
# interpreter and the development files together, and it wants them to agree. Updating: change the
# version, then the two hashes (sha256sum of the downloaded files).

set(PLU_WINDOWS_PYTHON_VERSION "3.14.10")
set(PLU_WINDOWS_PYTHON_NUGET_SHA256 "f54ba67c9951c3d773ea47d2bb095150c037c399f0423a968bcda22809241784")
set(PLU_WINDOWS_PYTHON_EMBED_SHA256 "90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159")

set(_plu_py_root "${CMAKE_BINARY_DIR}/WindowsPython/${PLU_WINDOWS_PYTHON_VERSION}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+" _plu_py_minor "${PLU_WINDOWS_PYTHON_VERSION}")
string(REPLACE "." "" _plu_py_tag "${_plu_py_minor}")   # 3.14 -> 314

function(plu_fetch_windows_python_package URL SHA256 ARCHIVE DEST)
    if(EXISTS "${DEST}/.complete")
        return()
    endif()
    message(STATUS "Windows Python: downloading ${URL}")
    file(DOWNLOAD "${URL}" "${ARCHIVE}"
            EXPECTED_HASH SHA256=${SHA256}
            STATUS _status
            TLS_VERIFY ON
    )
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        list(GET _status 1 _message)
        file(REMOVE "${ARCHIVE}")
        message(FATAL_ERROR "Windows Python: download of ${URL} failed: ${_message}")
    endif()
    file(REMOVE_RECURSE "${DEST}")
    file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${DEST}")
    file(REMOVE "${ARCHIVE}")
    file(TOUCH "${DEST}/.complete")
endfunction()

plu_fetch_windows_python_package(
        "https://api.nuget.org/v3-flatcontainer/python/${PLU_WINDOWS_PYTHON_VERSION}/python.${PLU_WINDOWS_PYTHON_VERSION}.nupkg"
        "${PLU_WINDOWS_PYTHON_NUGET_SHA256}"
        "${_plu_py_root}/python.nupkg"
        "${_plu_py_root}/nuget"
)
plu_fetch_windows_python_package(
        "https://www.python.org/ftp/python/${PLU_WINDOWS_PYTHON_VERSION}/python-${PLU_WINDOWS_PYTHON_VERSION}-embed-amd64.zip"
        "${PLU_WINDOWS_PYTHON_EMBED_SHA256}"
        "${_plu_py_root}/embed.zip"
        "${_plu_py_root}/embed"
)

# FindPython takes these "artifacts" as given instead of asking the (host) interpreter for them.
set(Python3_INCLUDE_DIR "${_plu_py_root}/nuget/tools/include" CACHE PATH "Windows Python headers (cross build)" FORCE)
set(Python3_LIBRARY "${_plu_py_root}/nuget/tools/libs/python${_plu_py_tag}.lib" CACHE FILEPATH "Windows Python import library (cross build)" FORCE)

# Everything a distribution copies next to the executable to run Python without an installation.
set(PLU_WINDOWS_PYTHON_RUNTIME_DIR "${_plu_py_root}/embed" CACHE INTERNAL "Embeddable Windows Python (cross build)")

# pyconfig.h auto-links python3xx.lib by bare name through #pragma comment(lib); lld-link then has to
# find it on its library path, which only knows the xwin sysroot. CMake links the full path anyway.
add_compile_definitions(Py_NO_LINK_LIB)
