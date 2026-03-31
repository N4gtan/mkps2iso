# mkps2iso build script

cmake_minimum_required(VERSION 3.28)

project(mkps2iso
    LANGUAGES    CXX
    VERSION      1.1.0
    DESCRIPTION  "PlayStation 2 ISO image maker & dumping tool"
    HOMEPAGE_URL "https://github.com/N4gtan/mkps2iso"
)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES  OFF)
set(BUILD_SHARED_LIBS           OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

## External dependencies

include(FetchContent)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    EXCLUDE_FROM_ALL
)

# Build tinyxml2
set(tinyxml2_BUILD_TESTING OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(tinyxml2)

## Internal dependencies

# Useful paths
set(mkps2iso_dir    "src/mkps2iso")
set(dumps2iso_dir   "src/dumps2iso")
set(shared_dir      "src/shared")

# Populate shared files
add_library(iso_shared OBJECT
    "${shared_dir}/common.cpp"
    "${shared_dir}/mmappedfile.cpp"
    "${shared_dir}/platform.cpp"
)
target_include_directories(iso_shared PUBLIC "${shared_dir}")
target_compile_definitions(iso_shared PUBLIC VERSION="${PROJECT_VERSION}")
target_link_libraries(iso_shared tinyxml2)

## Executables

add_executable(mkps2iso
    "${mkps2iso_dir}/dvdwriter.cpp"
    "${mkps2iso_dir}/isobuilder.cpp"
    "${mkps2iso_dir}/main.cpp"
    "${mkps2iso_dir}/xmlreader.cpp"
)
target_link_libraries(mkps2iso iso_shared)
if(MINGW)
    target_link_libraries(mkps2iso "-municode")
endif()

add_executable(dumps2iso
    "${dumps2iso_dir}/dvdreader.cpp"
    "${dumps2iso_dir}/isoparser.cpp"
    "${dumps2iso_dir}/main.cpp"
    "${dumps2iso_dir}/xmlwriter.cpp"
)
target_link_libraries(dumps2iso iso_shared)
if(MINGW)
    target_link_libraries(dumps2iso "-municode")
endif()

## Warnings & Sanitizers

include(CheckCXXCompilerFlag)
if(MSVC)
    set(W_FLAGS "/W2")
    set(S_FLAGS "/fsanitize=address" "/Zi")
else()
    set(W_FLAGS "-fno-strict-aliasing" "-Wall" "-Wextra" "-Wpedantic" "-Wno-missing-field-initializers")
    set(S_FLAGS "-fsanitize=address,undefined" "-fno-omit-frame-pointer" "-g")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
check_cxx_compiler_flag("${S_FLAGS}" HAS_ASAN)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "EXECUTABLE")

foreach(t iso_shared mkps2iso dumps2iso)
    target_compile_options(${t} PRIVATE ${W_FLAGS})
    if(HAS_ASAN)
        target_compile_options(${t} PRIVATE $<$<CONFIG:Debug>:${S_FLAGS}>)
        if(NOT t STREQUAL "iso_shared")
            if(MSVC)
                target_link_options(${t} PRIVATE $<$<CONFIG:Debug>:/DEBUG /INCREMENTAL:NO>)
            else()
                target_link_options(${t} PRIVATE $<$<CONFIG:Debug>:${S_FLAGS}>)
            endif()
        endif()
    endif()
endforeach()

## Installation

include(GNUInstallDirs)
if (WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
    set(CMAKE_INSTALL_DATADIR "doc")
else()
    set(CMAKE_INSTALL_DATADIR "${CMAKE_INSTALL_DOCDIR}")
endif()
install(TARGETS dumps2iso mkps2iso RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES
    "${PROJECT_SOURCE_DIR}/examples/example.xml"
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}"
)

## CPack (used by GitHub Actions to package releases)

if(NOT DEFINED CPACK_GENERATOR)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CPACK_GENERATOR "ZIP;DEB;RPM")
    else()
        set(CPACK_GENERATOR "ZIP")
    endif()
endif()

set(CPACK_VERBATIM_VARIABLES            ON)
set(CPACK_ARCHIVE_THREADS               0)
set(CPACK_PACKAGE_NAME                  "mkps2iso")
set(CPACK_PACKAGE_VERSION               "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR                "Nagtan")
set(CPACK_PACKAGE_CONTACT               "Nagtan <45178494+N4gtan@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY   "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL          "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_DIRECTORY             "${PROJECT_BINARY_DIR}/packages")
set(CPACK_RESOURCE_FILE_LICENSE         "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README          "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY     "mkps2iso")

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS      ON)
set(CPACK_DEBIAN_PACKAGE_SECTION        "devel")

include(CPack)
