cmake_minimum_required(VERSION 3.23)

project(tpa VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ---------------------------------------------------------------------------
# Phiên bản Endstone API cần build plugin — phải khớp với phiên bản server.
# Có thể đổi qua: cmake -DENDSTONE_API_VERSION=0.10 ..
# ---------------------------------------------------------------------------
set(ENDSTONE_API_VERSION "0.11" CACHE STRING "Endstone API version")

include(FetchContent)

# ---- Endstone SDK (chỉ dùng API chính thức) ----
FetchContent_Declare(
        endstone
        GIT_REPOSITORY https://github.com/EndstoneMC/endstone.git
        GIT_TAG v${ENDSTONE_API_VERSION}
)
FetchContent_MakeAvailable(endstone)

# ---- yaml-cpp: đọc config.yml (MIT; biên dịch tĩnh vào plugin, không ảnh hưởng server) ----
# Chỉ cần thiết vì Endstone C++ chưa có API đọc config như Bukkit.
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

set(SOURCES
        src/tpa.cpp
        src/tpa_config.cpp
)

endstone_add_plugin(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE yaml-cpp::yaml-cpp)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
            /EHsc
            /utf-8
            $<$<CONFIG:Debug>:/Zi>
    )
endif()