#ifndef NET_HPP
#define NET_HPP

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN   
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#include "http.hpp"
#include "http_server.hpp"

#endif