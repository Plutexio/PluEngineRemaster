# L3 — pybind11 glue. Optional in principle: nothing below it names Python.
plu_add_engine_module(PluScripting DEPENDS PluCore)

target_link_libraries(PluScripting PUBLIC glm pybind11 Python3::Python)

# The engine's own Python code (python/*.py), compiled into the library as byte arrays — see
# PluEngine/Scripting/EmbeddedPython.h. Regenerated whenever one of the files changes; a new file
# is picked up by the glob on the next build.
file(GLOB PLU_EMBEDDED_PYTHON_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/python/*.py")
set(PLU_EMBEDDED_PYTHON_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedPythonFiles.generated.cpp")
string(REPLACE ";" "|" PLU_EMBEDDED_PYTHON_INPUTS "${PLU_EMBEDDED_PYTHON_FILES}")
add_custom_command(
        OUTPUT "${PLU_EMBEDDED_PYTHON_SOURCE}"
        COMMENT "Embedding engine Python files"
        COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${PLU_EMBEDDED_PYTHON_SOURCE}" "-DINPUTS=${PLU_EMBEDDED_PYTHON_INPUTS}"
                -P "${CMAKE_SOURCE_DIR}/cmake/PluEmbedPython.cmake"
        DEPENDS ${PLU_EMBEDDED_PYTHON_FILES} "${CMAKE_SOURCE_DIR}/cmake/PluEmbedPython.cmake"
        VERBATIM
)
target_sources(PluScripting PRIVATE "${PLU_EMBEDDED_PYTHON_SOURCE}" ${PLU_EMBEDDED_PYTHON_FILES})
# Plain byte arrays: no engine headers, so no precompiled header (it costs seconds per TU to load)
# and no unity batch.
set_source_files_properties("${PLU_EMBEDDED_PYTHON_SOURCE}" PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
)
