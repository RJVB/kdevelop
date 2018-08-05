if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # LTO and Clang don't mix well with creating static libraries on Linux
    string(REPLACE "-flto" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REPLACE "-flto" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-flto" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-flto" "" CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
    string(REPLACE "-flto" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
endif()

add_executable(clang-parser
    clang-parser.cpp
)

target_link_libraries(clang-parser
    KDev::Tests
    KDevClangPrivate
)

add_library(codecompletiontestbase STATIC codecompletiontestbase.cpp)
target_link_libraries(codecompletiontestbase PUBLIC
    KDev::Tests
    Qt5::Test
    KDevClangPrivate
)

add_executable(clang-minimal-visitor WIN32
    minimal_visitor.cpp
)
ecm_mark_nongui_executable(clang-minimal-visitor)
target_link_libraries(clang-minimal-visitor
    Clang::clang
)

ecm_add_test(test_buddies.cpp
    TEST_NAME test_buddies-clang
    LINK_LIBRARIES
        KDev::Tests
        Qt5::Test
)

ecm_add_test(test_codecompletion.cpp
    TEST_NAME test_codecompletion
    LINK_LIBRARIES
        codecompletiontestbase
)

ecm_add_test(test_assistants.cpp
    TEST_NAME test_assistants
    LINK_LIBRARIES
        KDev::Tests
        Qt5::Test
        KDevClangPrivate
)

ecm_add_test(test_clangutils.cpp
    TEST_NAME test_clangutils
    LINK_LIBRARIES
        KDev::Tests
        Qt5::Test
        Clang::clang
        KDevClangPrivate
)

ecm_add_test(test_duchain.cpp
    TEST_NAME test_duchain-clang
    LINK_LIBRARIES
        KDev::Tests
        Qt5::Test
        KDevClangPrivate
        KDev::DefinesAndIncludesManager
)

ecm_add_test(test_refactoring.cpp
    TEST_NAME test_refactoring-clang
    LINK_LIBRARIES
        KDev::Tests
        Qt5::Test
        KDevClangPrivate
)

ecm_add_test(test_duchainutils.cpp
    TEST_NAME test_duchainutils
    LINK_LIBRARIES
        KDev::Tests
        Qt5::Test
        KDevClangPrivate
)

ecm_add_test(test_problems.cpp
    TEST_NAME test_problems
    LINK_LIBRARIES
        KDev::Tests
        Qt5::Test
        KDevClangPrivate
)

configure_file("testfilepaths.h.cmake" "testfilepaths.h" ESCAPE_QUOTES)
ecm_add_test(test_files.cpp
TEST_NAME test_files-clang
LINK_LIBRARIES
    Qt5::Test
    Qt5::Core
    KDev::Language
    KDev::Tests
)

if(NOT COMPILER_OPTIMIZATIONS_DISABLED)
    ecm_add_test(bench_codecompletion.cpp
        TEST_NAME bench_codecompletion
        LINK_LIBRARIES
            codecompletiontestbase
    )
    set_tests_properties(bench_codecompletion PROPERTIES TIMEOUT 30)
    ecm_add_test(bench_duchain.cpp
        TEST_NAME bench_duchain
        LINK_LIBRARIES
            KDev::Tests
            Qt5::Test
            KDevClangPrivate
    )
    set_tests_properties(bench_duchain PROPERTIES TIMEOUT 30)
endif()
