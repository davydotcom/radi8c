cmake_minimum_required(VERSION 3.11)
project(radi8c2 
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Add FTXUI
add_subdirectory(external/FTXUI)

# Source files
set(SOURCES
    src/Connection.cpp
    src/Protocol.cpp
    src/TUI.cpp
    src/FileTransfer.cpp
    src/Config.cpp
    src/main.cpp
)

# Create executable
add_executable(radi8c2 ${SOURCES})

# Include directories
target_include_directories(radi8c2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(radi8c2 PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(radi8c2 PRIVATE ws2_32 shell32)
else()
    target_link_libraries(radi8c2 PRIVATE pthread)
endif()

# macOS specific
if(APPLE)
    target_include_directories(radi8c2 PRIVATE /opt/homebrew/opt/openssl@3/include)
    target_link_directories(radi8c2 PRIVATE /opt/homebrew/opt/openssl@3/lib)
endif()
