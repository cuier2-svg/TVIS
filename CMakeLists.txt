cmake_minimum_required(VERSION 3.25)

project(PSI)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall)
#add_compile_options(-fsanitize=address)

# Set default build type to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif ()

set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb -O0" CACHE STRING "Debug compiler flags" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Release compiler flags" FORCE)

add_executable(psi
        src/main.cpp
        src/vole/receiver.cpp
        src/vole/sender.cpp
        src/vole/vole.cpp
        src/vole/volePSI/SimpleIndex.cpp)

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

option(ENABLE_BATCHPIR "Build the BatchPIR integration when SEAL is available" ON)

if (ENABLE_BATCHPIR)
    find_package(SEAL QUIET)
    if (SEAL_FOUND)
        set(BATCHPIR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/BatchPIR)
        add_library(BatchPIR STATIC
                ${BATCHPIR_ROOT}/src/batchpirclient.cpp
                ${BATCHPIR_ROOT}/src/batchpirparams.cpp
                ${BATCHPIR_ROOT}/src/batchpirserver.cpp
                ${BATCHPIR_ROOT}/src/client.cpp
                ${BATCHPIR_ROOT}/src/pirparams.cpp
                ${BATCHPIR_ROOT}/src/server.cpp)
        target_include_directories(BatchPIR PUBLIC
                ${BATCHPIR_ROOT}
                ${BATCHPIR_ROOT}/header)
        target_compile_features(BatchPIR PUBLIC cxx_std_11)
        if (TARGET SEAL::seal)
            target_link_libraries(BatchPIR PUBLIC SEAL::seal)
        elseif (TARGET SEAL::seal_shared)
            target_link_libraries(BatchPIR PUBLIC SEAL::seal_shared)
        else ()
            message(FATAL_ERROR "SEAL was found, but no supported SEAL CMake target is available")
        endif ()

        target_link_libraries(psi BatchPIR)
        target_compile_definitions(psi PRIVATE PSI_ENABLE_BATCHPIR=1)
    else ()
        message(STATUS "SEAL not found; building without BatchPIR. Set ENABLE_BATCHPIR=OFF to silence this optional integration.")
    endif ()
endif ()



find_package(OpenSSL REQUIRED)

if(OPENSSL_FOUND)
    message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
    message(STATUS "OpenSSL Include Dir: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL Libraries: ${OPENSSL_LIBRARIES}")
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIBRARIES})
endif()
target_link_libraries(psi ${OPENSSL_LIBRARIES})
