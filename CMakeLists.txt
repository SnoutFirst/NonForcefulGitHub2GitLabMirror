cmake_minimum_required(VERSION 3.20)

project(NonForcefulGitHub2GitLabMirror VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

include(FetchContent)

# PlatformFolders 4.3.0 (release of 2025-07-31), pinned to its immutable commit.
FetchContent_Declare(platformfolders
  GIT_REPOSITORY https://github.com/sago007/PlatformFolders.git
  GIT_TAG 1df240bdcbfe8b431f3fa224da086fa805b8162c
  GIT_PROGRESS TRUE
)

# The PlatformFolders test suite and install rules are not part of this project.
set(PLATFORMFOLDERS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(PLATFORMFOLDERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(platformfolders)

# Boost.Process v2 is header-only and builds on Boost.Asio; Boost.Filesystem is required by
# its path handling and Boost.JSON is used to read the `gh` and `glab` API responses.
find_package(Boost 1.83 REQUIRED COMPONENTS filesystem json)
find_package(Threads REQUIRED)

add_executable(NonForcefulGitHub2GitLabMirror
  src/main.cpp
  src/fetchstatus.cpp
  src/githubdiscovery.cpp
  src/gitlabproject.cpp
  src/gitworkspace.cpp
  src/nonforcefulgithub2gitlabmirror.cpp
  src/processrunner.cpp
  src/pushporcelain.cpp
  src/reporting.cpp
  src/storage.cpp
  src/syncresult.cpp
  src/textutil.cpp
)

target_include_directories(NonForcefulGitHub2GitLabMirror PRIVATE src)

target_compile_definitions(NonForcefulGitHub2GitLabMirror PRIVATE BOOST_PROCESS_V2_HEADER_ONLY=1)

target_link_libraries(NonForcefulGitHub2GitLabMirror
  PRIVATE
    sago::platform_folders
    Boost::filesystem
    Boost::json
    Threads::Threads
)

if(MSVC)
  target_compile_options(NonForcefulGitHub2GitLabMirror PRIVATE /W4 /permissive-)
else()
  target_compile_options(NonForcefulGitHub2GitLabMirror PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS NonForcefulGitHub2GitLabMirror RUNTIME DESTINATION bin)
