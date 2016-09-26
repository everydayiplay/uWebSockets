#include "Hub.h"
#include "HTTPSocket.h"
#include <regex>
#include <openssl/sha.h>

namespace uWS {

void Hub::onServerAccept(uS::Socket s) {
    uS::SocketData *socketData = s.getSocketData();
    s.startTimeout<HTTPSocket<SERVER>::onEnd>();
    s.enterState<HTTPSocket<SERVER>>(new HTTPSocket<SERVER>::Data(socketData));
    delete socketData;
}

void Hub::onClientConnection(uS::Socket s, bool error) {
    HTTPSocket<CLIENT>::Data *httpSocketData = (HTTPSocket<CLIENT>::Data *) s.getSocketData();

    if (error) {
        ((Group<CLIENT> *) httpSocketData->nodeData)->errorHandler(httpSocketData->httpUser);
        delete httpSocketData;
    } else {
        s.enterState<HTTPSocket<CLIENT>>(s.getSocketData());

        // if this returns false, emit error?
        if (!HTTPSocket<CLIENT>(s).upgrade()) {
            ((Group<CLIENT> *) httpSocketData->nodeData)->errorHandler(httpSocketData->httpUser);
            // delete here?
        }
    }
}

bool Hub::listen(int port, uS::TLS::Context sslContext, Group<SERVER> *eh) {
    if (!eh) {
        eh = (Group<SERVER> *) this;
    }

    if (uS::Node::listen<onServerAccept>(port, sslContext, (uS::NodeData *) eh, nullptr)) {
        eh->errorHandler(port);
        return false;
    }
    return true;
}

void Hub::connect(std::string uri, void *user, int timeoutMs, Group<CLIENT> *eh) {
    if (!eh) {
        eh = (Group<CLIENT> *) this;
    }

    std::regex regex("([a-z]+):\\/\\/([^\\/:]+)[:]*([0-9]*)[\\/]*(.*)");
    std::smatch match;
    if (std::regex_match(uri, match, regex)) {
        int port = 80;
        bool secure = false;
        if (match[1].str() == "wss") {
            port = 443;
            secure = true;
        } else if (match[1].str() != "ws") {
            eh->errorHandler(user);
        }

        if (match[3].str().length()) {
            port = stoi(match[3].str());
        }

        uS::SocketData socketData((uS::NodeData *) eh);
        HTTPSocket<CLIENT>::Data *httpSocketData = new HTTPSocket<CLIENT>::Data(&socketData);

        httpSocketData->host = match[2].str();
        httpSocketData->path = match[4].str();
        httpSocketData->httpUser = user;

        uS::Socket s = uS::Node::connect<onClientConnection>(match[2].str().c_str(), port, secure, httpSocketData);
        if (s) {
            s.startTimeout<HTTPSocket<CLIENT>::onEnd>(timeoutMs);
        }
    } else {
        eh->errorHandler(user);
    }
}

bool Hub::upgrade(uv_os_sock_t fd, const char *secKey, SSL *ssl, const char *extensions, size_t extensionsLength, Group<SERVER> *serverGroup)
{
    if (!serverGroup) {
        serverGroup = &getDefaultGroup<SERVER>();
    }

    uS::Socket s = uS::Socket::init((uS::NodeData *) serverGroup, fd, ssl);
    s.enterState<HTTPSocket<SERVER>>(new HTTPSocket<SERVER>::Data(s.getSocketData()));

    if (HTTPSocket<SERVER>(s).upgrade(secKey)) {
        s.enterState<WebSocket<SERVER>>(new WebSocket<SERVER>::Data(s.getSocketData()));
        serverGroup->addWebSocket(s);
        serverGroup->connectionHandler(WebSocket<SERVER>(s));
        return true;
    }
    return false;
}

}
