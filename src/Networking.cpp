#include "Networking.h"

namespace uS {

namespace TLS {

Context::Context(const Context &other)
{
    if (other.context) {
        context = other.context;
        SSL_CTX_up_ref(context);
    }
}

Context &Context::operator=(const Context &other) {
    if (other.context) {
        context = other.context;
        SSL_CTX_up_ref(context);
    }
}

Context::~Context()
{
    if (context) {
        SSL_CTX_free(context);
    }
}

struct Init {
    Init() {SSL_library_init();}
    ~Init() {/*EVP_cleanup();*/}
} init;

Context createContext(std::string certChainFileName, std::string keyFileName)
{
    SSL_CTX *context = SSL_CTX_new(SSLv23_server_method());
    if (!context) {
        return nullptr;
    }

    SSL_CTX_set_options(context, SSL_OP_NO_SSLv3);

    if (SSL_CTX_use_certificate_chain_file(context, certChainFileName.c_str()) != 1) {
        return nullptr;
    } else if (SSL_CTX_use_PrivateKey_file(context, keyFileName.c_str(), SSL_FILETYPE_PEM) != 1) {
        return nullptr;
    }

    return context;
}

}

struct Init {
    Init() {signal(SIGPIPE, SIG_IGN);}
} init;

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")

struct WindowsInit {
    WSADATA wsaData;
    WindowsInit() {WSAStartup(MAKEWORD(2, 2), &wsaData);}
    ~WindowsInit() {WSACleanup();}
} windowsInit;

#endif

}
