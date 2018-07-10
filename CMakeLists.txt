cmake_minimum_required(VERSION 3.4)

project(uWebSockets)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-fPIC -stdlib=libc++ -Wall -Wextra -Wpedantic)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi") 
endif()

find_package(OpenSSL REQUIRED)
set(LIBUV_USE_STATIC ON)
find_package(LibUV REQUIRED)


set(PublicHeaders src/Asio.h
                  src/Backend.h
                  src/Epoll.h
                  src/Extensions.h
                  src/Group.h
                  src/HTTPSocket.h
                  src/Hub.h
                  src/Libuv.h
                  src/Networking.h
                  src/Node.h
                  src/Room.h
                  src/Socket.h
                  src/uWS.h
                  src/WebSocket.h
                  src/WebSocketProtocol.h)



set (SourceFiles src/Epoll.cpp
                 src/Extensions.cpp
                 src/Group.cpp
                 src/HTTPSocket.cpp
                 src/Hub.cpp
                 src/Networking.cpp
                 src/Node.cpp
                 src/Room.cpp
                 src/Socket.cpp
                 src/WebSocket.cpp)



add_library(uWS ${PublicHeaders} ${SourceFiles})
set_target_properties(uWS PROPERTIES PUBLIC_HEADER "${PublicHeaders}")

set_property(TARGET uWS PROPERTY CXX_STANDARD 11)
set_property(TARGET uWS PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(uWS PUBLIC ${LIBUV_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})
if (BUILD_SHARED_LIBS)
    target_link_libraries(uWS PRIVATE OpenSSL::SSL OpenSSL::Crypto ${LIBUV_LIBRARIES})
else()
    target_link_libraries(uWS PUBLIC OpenSSL::SSL OpenSSL::Crypto ${LIBUV_LIBRARIES})
    target_compile_definitions(uWS PUBLIC UWEBSOCKET_STATIC)
endif()

if (WIN32)
    target_compile_definitions(uWS PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()




install(TARGETS uWS
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include/uWS)
