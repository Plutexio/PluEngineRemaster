# ThreadSanitizer build of the tests. Opt-in, because TSan roughly halves the speed and
# needs its own binary — the stress suites are where it earns its keep (striped map,
# lock-free array, SPSC ring).
option(PLUSTL_TESTS_TSAN "Build the PluSTL tests with ThreadSanitizer" OFF)

add_executable(PluSTLTests
        Main.cpp
        Test_DynamicArray.cpp
        Test_HashMap.cpp
        Test_HashSet.cpp
        Test_Hashers.cpp
        Test_Queue.cpp
        Test_ConcurrentHashMap.cpp
        Test_ConcurrentHashSet.cpp
        Test_ConcurrentQueue.cpp
        Test_ConcurrentArray.cpp
        Test_ConcurrentString.cpp
)

target_include_directories(PluSTLTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PluSTLTests PRIVATE PluSTL::PluSTL)

if(PLUSTL_TESTS_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(PluSTLTests PRIVATE -fsanitize=thread -g -O1 -fno-omit-frame-pointer)
        target_link_options(PluSTLTests PRIVATE -fsanitize=thread)
    else()
        message(WARNING "PLUSTL_TESTS_TSAN is only supported on GCC/Clang — building without it")
    endif()
endif()

add_test(NAME PluSTLTests COMMAND PluSTLTests)
