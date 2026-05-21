cmake_minimum_required(VERSION 3.25)

project(PSI)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall)
#add_compile_options(-fsanitize=address)

add_compile_options(-g -ggdb)

# Set default build type to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif ()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_compile_options(-O0)
endif ()

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_compile_options(-O2)
endif ()

file(GLOB SOURCES "src/*/*.cpp" "src/*/*.hpp" "src/*.cpp" "src/*.hpp")
add_executable(psi ${SOURCES})

target_include_directories(psi PUBLIC cuckoofilter)

#set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_DEBUG_RUNTIME OFF)

find_package(libOTe REQUIRED

        NO_DEFAULT_PATH
        NO_CMAKE_ENVIRONMENT_PATH
        NO_CMAKE_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
        NO_CMAKE_SYSTEM_PATH
        NO_CMAKE_FIND_ROOT_PATH)
target_link_libraries(psi oc::libOTe)



find_package(OpenSSL REQUIRED)

if(OPENSSL_FOUND)
    message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
    message(STATUS "OpenSSL Include Dir: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL Libraries: ${OPENSSL_LIBRARIES}")
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIBRARIES})
endif()
target_link_libraries(psi ${OPENSSL_LIBRARIES})
