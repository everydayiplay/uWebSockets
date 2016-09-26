// Implements the connection between protocol and networking & events

#include "Hub.h"

namespace uWS {

// implement protocol vs networking hooks
template <const bool isServer>
bool WebSocketProtocol<isServer>::setCompressed(void *user) {
    // we do not support compressed frames!
    return false;
}

template <const bool isServer>
bool WebSocketProtocol<isServer>::refusePayloadLength(void *user, int length) {
    // >16 mb = drop connection
    return length > 1024 * 1024 * 16;
}

template <const bool isServer>
void WebSocketProtocol<isServer>::forceClose(void *user) {
    WebSocket<isServer>((uv_poll_t *) user).terminate();
}

template <const bool isServer>
bool WebSocketProtocol<isServer>::handleFragment(char *data, int length, unsigned int remainingBytes, int opCode, bool fin, void *user) {
    uS::Socket s((uv_poll_t *) user);
    typename WebSocket<isServer>::Data *webSocketData = (typename WebSocket<isServer>::Data *) s.getSocketData();

    if (opCode < 3) {

        if (!remainingBytes && fin && !webSocketData->fragmentBuffer.length()) {
            if (opCode == 1 && !isValidUtf8((unsigned char *) data, length)) {
                forceClose(user);
                return true;
            }

            ((Group<isServer> *) s.getSocketData()->nodeData)->messageHandler(WebSocket<isServer>(s), data, length, (OpCode) opCode);
            if (s.isClosed() || s.isShuttingDown()) {
                return true;
            }
        } else {
            webSocketData->fragmentBuffer.append(data, length);
            if (!remainingBytes && fin) {
                data = (char *) webSocketData->fragmentBuffer.data();
                length = webSocketData->fragmentBuffer.length();

                if (opCode == 1 && !isValidUtf8((unsigned char *) data, length)) {
                    forceClose(user);
                    return true;
                }

                ((Group<isServer> *) s.getSocketData()->nodeData)->messageHandler(WebSocket<isServer>(s), data, length, (OpCode) opCode);
                if (s.isClosed() || s.isShuttingDown()) {
                    return true;
                }
                webSocketData->fragmentBuffer.clear();
            }
        }
    } else {
        // we don't need to buffer up in most cases!
        webSocketData->controlBuffer.append(data, length);
        if (!remainingBytes && fin) {
            if (opCode == CLOSE) {
                CloseFrame closeFrame = parseClosePayload((char *) webSocketData->controlBuffer.data(), webSocketData->controlBuffer.length());
                WebSocket<isServer>(s).close(closeFrame.code, closeFrame.message, closeFrame.length);
                return true;
            } else {
                if (opCode == PING) {
                    WebSocket<isServer>(s).send(webSocketData->controlBuffer.data(), webSocketData->controlBuffer.length(), (OpCode) OpCode::PONG);
                    ((Group<isServer> *) s.getSocketData()->nodeData)->pingHandler(WebSocket<isServer>(s), (char *) webSocketData->controlBuffer.data(), webSocketData->controlBuffer.length());
                    if (s.isClosed() || s.isShuttingDown()) {
                        return true;
                    }
                } else if (opCode == PONG) {
                    ((Group<isServer> *) s.getSocketData()->nodeData)->pongHandler(WebSocket<isServer>(s), (char *) webSocketData->controlBuffer.data(), webSocketData->controlBuffer.length());
                    if (s.isClosed() || s.isShuttingDown()) {
                        return true;
                    }
                }
            }
            webSocketData->controlBuffer.clear();
        }
    }

    return false;
}

template class WebSocketProtocol<SERVER>;
template class WebSocketProtocol<CLIENT>;

}
