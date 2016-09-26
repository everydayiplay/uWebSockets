#ifndef HUB_H
#define HUB_H

#include "Group.h"
#include "Node.h"
#include <string>

namespace uWS {

struct Hub : private uS::Node, public Group<SERVER>, public Group<CLIENT> {

    template <bool isServer>
    Group<isServer> *createGroup() {
        return new Group<isServer>(*nodeData);
    }

    template <bool isServer>
    Group<isServer> &getDefaultGroup() {
        return (Group<isServer> &) *this;
    }

    struct ConnectionData {
        std::string path;
        void *user;
        Group<CLIENT> *group;
    };

    static void onServerAccept(uS::Socket s);
    static void onClientConnection(uS::Socket s, bool error);

    bool listen(int port, uS::TLS::Context sslContext = nullptr, Group<SERVER> *eh = nullptr);
    void connect(std::string uri, void *user, int timeoutMs = 5000, Group<CLIENT> *eh = nullptr);
    bool upgrade(uv_os_sock_t fd, const char *secKey, SSL *ssl, const char *extensions, size_t extensionsLength, Group<SERVER> *serverGroup = nullptr);

    Hub() : uS::Node(1024 * 300, /*WebSocketProtocol<SERVER>::CONSUME_PRE_PADDING*/ 16, WebSocketProtocol<SERVER>::CONSUME_POST_PADDING), Group<SERVER>(*nodeData), Group<CLIENT>(*nodeData) {

    }

    ~Hub() {

    }

    using uS::Node::run;
    using Group<SERVER>::onConnection;
    using Group<CLIENT>::onConnection;
    using Group<SERVER>::onMessage;
    using Group<CLIENT>::onMessage;
    using Group<SERVER>::onDisconnection;
    using Group<CLIENT>::onDisconnection;
    using Group<SERVER>::onPing;
    using Group<CLIENT>::onPing;
    using Group<SERVER>::onPong;
    using Group<CLIENT>::onPong;
    using Group<SERVER>::onError;
    using Group<CLIENT>::onError;
};

}

#endif // HUB_H
