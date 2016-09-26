#ifndef GROUP_H
#define GROUP_H

#include "WebSocket.h"
#include <functional>

namespace uWS {

template <bool isServer>
struct Group : protected uS::NodeData {
    friend class Hub;
    std::function<void(WebSocket<isServer>)> connectionHandler;
    std::function<void(WebSocket<isServer>, char *message, size_t length, OpCode opCode)> messageHandler;
    std::function<void(WebSocket<isServer>, int code, char *message, size_t length)> disconnectionHandler;
    std::function<void(WebSocket<isServer>, char *, size_t)> pingHandler;
    std::function<void(WebSocket<isServer>, char *, size_t)> pongHandler;

    using errorType = typename std::conditional<isServer, int, void *>::type;
    std::function<void(errorType)> errorHandler;

    void *user = nullptr;
    void setUserData(void *user) {
        this->user = user;
    }

    void *getUserData() {
        return user;
    }

    uv_poll_t *webSocketHead = nullptr, *httpSocketHead = nullptr;
    void addWebSocket(uv_poll_t *webSocket) {
        if (webSocketHead) {
            uS::SocketData *nextData = (uS::SocketData *) webSocketHead->data;
            nextData->prev = webSocket;
            uS::SocketData *data = (uS::SocketData *) webSocket->data;
            data->next = webSocketHead;
        }
        webSocketHead = webSocket;
    }

    void removeWebSocket(uv_poll_t *webSocket) {
        uS::SocketData *socketData = (uS::SocketData *) webSocket->data;
        if (socketData->prev == socketData->next) {
            webSocketHead = (uv_poll_t *) nullptr;
        } else {
            if (socketData->prev) {
                ((uS::SocketData *) socketData->prev->data)->next = socketData->next;
            } else {
                webSocketHead = socketData->next;
            }
            if (socketData->next) {
                ((uS::SocketData *) socketData->next->data)->prev = socketData->prev;
            }
        }
    }

    struct WebSocketIterator {
        uv_poll_t *webSocket;
        WebSocketIterator(uv_poll_t *webSocket) : webSocket(webSocket) {

        }

        WebSocket<isServer> operator*() {
            return WebSocket<isServer>(webSocket);
        }

        bool operator!=(const WebSocketIterator &other) {
            return !(webSocket == other.webSocket);
        }

        WebSocketIterator &operator++() {
            uS::SocketData *socketData = (uS::SocketData *) webSocket->data;
            webSocket = socketData->next;
            return *this;
        }
    };

protected:
    Group(uS::NodeData nodeData) : uS::NodeData(nodeData) {
        connectionHandler = [](WebSocket<isServer>) {};
        messageHandler = [](WebSocket<isServer>, char *, size_t, OpCode) {};
        disconnectionHandler = [](WebSocket<isServer>, int, char *, size_t) {};
        pingHandler = pongHandler = [](WebSocket<isServer>, char *, size_t) {};
        errorHandler = [](errorType) {};
    }

public:
    void onConnection(std::function<void(WebSocket<isServer>)> handler) {
        connectionHandler = handler;
    }

    void onMessage(std::function<void(WebSocket<isServer>, char *, size_t, OpCode)> handler) {
        messageHandler = handler;
    }

    void onDisconnection(std::function<void(WebSocket<isServer>, int code, char *message, size_t length)> handler) {
        disconnectionHandler = handler;
    }

    void onPing(std::function<void(WebSocket<isServer>, char *, size_t)> handler) {
        pingHandler = handler;
    }

    void onPong(std::function<void(WebSocket<isServer>, char *, size_t)> handler) {
        pongHandler = handler;
    }

    void onError(std::function<void(errorType)> handler) {
        errorHandler = handler;
    }

    WebSocketIterator begin() {
        return WebSocketIterator(webSocketHead);
    }

    WebSocketIterator end() {
        return WebSocketIterator(nullptr);
    }

    void broadcast(char *message, size_t length, OpCode opCode) {
        typename WebSocket<isServer>::PreparedMessage *preparedMessage = WebSocket<isServer>::prepareMessage(message, length, opCode, false);
        for (WebSocket<isServer> ws : *this) {
            ws.sendPrepared(preparedMessage);
        }
        WebSocket<isServer>::finalizeMessage(preparedMessage);
    }

    void terminate() {
        for (WebSocket<isServer> ws : *this) {
            ws.terminate();
        }

        // shared code!
        uS::ListenData *listenData = (uS::ListenData *) user;
        if (listenData) {
            uS::Socket(listenData->listenPoll).close();
            delete listenData;
        }
    }

    void close() {
        for (WebSocket<isServer> ws : *this) {
            ws.close();
        }

        // shared code!
        uS::ListenData *listenData = (uS::ListenData *) user;
        if (listenData) {
            uS::Socket(listenData->listenPoll).close();
            delete listenData;
        }
    }
};

}

#endif // GROUP_H
