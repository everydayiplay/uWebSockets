#include "../src/uWS.h"

#include <node.h>
#include <node_buffer.h>
#include <cstring>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <uv.h>

using namespace std;
using namespace v8;

uWS::Hub hub;

class NativeString {
    char *data;
    size_t length;
    char utf8ValueMemory[sizeof(String::Utf8Value)];
    String::Utf8Value *utf8Value = nullptr;
public:
    NativeString(const Local<Value> &value) {
        if (value->IsUndefined()) {
            data = nullptr;
            length = 0;
        } else if (value->IsString()) {
            utf8Value = new (utf8ValueMemory) String::Utf8Value(value);
            data = (**utf8Value);
            length = utf8Value->length();
        } else if (node::Buffer::HasInstance(value)) {
            data = node::Buffer::Data(value);
            length = node::Buffer::Length(value);
        } else if (value->IsTypedArray()) {
            Local<ArrayBufferView> arrayBufferView = Local<ArrayBufferView>::Cast(value);
            ArrayBuffer::Contents contents = arrayBufferView->Buffer()->GetContents();
            length = contents.ByteLength();
            data = (char *) contents.Data();
        } else if (value->IsArrayBuffer()) {
            Local<ArrayBuffer> arrayBuffer = Local<ArrayBuffer>::Cast(value);
            ArrayBuffer::Contents contents = arrayBuffer->GetContents();
            length = contents.ByteLength();
            data = (char *) contents.Data();
        } else {
            static char empty[] = "";
            data = empty;
            length = 0;
        }
    }

    char *getData() {return data;}
    size_t getLength() {return length;}
    ~NativeString() {
        if (utf8Value) {
            utf8Value->~Utf8Value();
        }
    }
};

struct GroupData {
    Persistent<Function> *connectionHandler, *messageHandler,
                         *disconnectionHandler, *pingHandler, *pongHandler;

    GroupData() {
        connectionHandler = new Persistent<Function>;
        messageHandler = new Persistent<Function>;
        disconnectionHandler = new Persistent<Function>;
        pingHandler = new Persistent<Function>;
        pongHandler = new Persistent<Function>;
    }

    ~GroupData() {
        delete connectionHandler;
        delete messageHandler;
        delete disconnectionHandler;
        delete pingHandler;
        delete pongHandler;
    }
};

template <bool isServer>
void createGroup(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<isServer> *group = hub.createGroup<isServer>();
    group->setUserData(new GroupData);
    args.GetReturnValue().Set(External::New(args.GetIsolate(), group));
}

template <bool isServer>
void deleteGroup(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    delete (GroupData *) group->getUserData();
    delete group;
}

template <bool isServer>
inline Local<External> wrapSocket(uWS::WebSocket<isServer> webSocket, Isolate *isolate) {
    return External::New(isolate, webSocket.getPollHandle());
}

template <bool isServer>
inline uWS::WebSocket<isServer> unwrapSocket(Local<External> number) {
    return uWS::WebSocket<isServer>((uv_poll_t *) number->Value());
}

inline Local<Value> wrapMessage(const char *message, size_t length, uWS::OpCode opCode, Isolate *isolate) {
    return opCode == uWS::OpCode::BINARY ? (Local<Value>) ArrayBuffer::New(isolate, (char *) message, length) : (Local<Value>) String::NewFromUtf8(isolate, message, String::kNormalString, length);
}

template <bool isServer>
inline Local<Value> getDataV8(uWS::WebSocket<isServer> webSocket, Isolate *isolate) {
    return webSocket.getUserData() ? Local<Value>::New(isolate, *(Persistent<Value> *) webSocket.getUserData()) : Local<Value>::Cast(Undefined(isolate));
}

template <bool isServer>
void getUserData(const FunctionCallbackInfo<Value> &args) {
    args.GetReturnValue().Set(getDataV8(unwrapSocket<isServer>(args[0].As<External>()), args.GetIsolate()));
}

template <bool isServer>
void clearUserData(const FunctionCallbackInfo<Value> &args) {
    uWS::WebSocket<isServer> webSocket = unwrapSocket<isServer>(args[0].As<External>());
    ((Persistent<Value> *) webSocket.getUserData())->Reset();
    delete (Persistent<Value> *) webSocket.getUserData();
}

template <bool isServer>
void setUserData(const FunctionCallbackInfo<Value> &args) {
    uWS::WebSocket<isServer> webSocket = unwrapSocket<isServer>(args[0].As<External>());
    if (webSocket.getUserData()) {
        ((Persistent<Value> *) webSocket.getUserData())->Reset(args.GetIsolate(), args[1]);
    } else {
        webSocket.setUserData(new Persistent<Value>(args.GetIsolate(), args[1]));
    }
}

template <bool isServer>
void getAddress(const FunctionCallbackInfo<Value> &args)
{
    typename uWS::WebSocket<isServer>::Address address = unwrapSocket<isServer>(args[0].As<External>()).getAddress();
    Local<Array> array = Array::New(args.GetIsolate(), 3);
    array->Set(0, Integer::New(args.GetIsolate(), address.port));
    array->Set(1, String::NewFromUtf8(args.GetIsolate(), address.address));
    array->Set(2, String::NewFromUtf8(args.GetIsolate(), address.family));
    args.GetReturnValue().Set(array);
}

uv_handle_t *getTcpHandle(void *handleWrap) {
    volatile char *memory = (volatile char *) handleWrap;
    for (volatile uv_handle_t *tcpHandle = (volatile uv_handle_t *) memory; tcpHandle->type != UV_TCP
         || tcpHandle->data != handleWrap || tcpHandle->loop != uv_default_loop(); tcpHandle = (volatile uv_handle_t *) memory) {
        memory++;
    }
    return (uv_handle_t *) memory;
}

struct SendCallbackData {
    Persistent<Function> jsCallback;
    Isolate *isolate;
};

void sendCallback(void *webSocket, void *data, bool cancelled)
{
    SendCallbackData *sc = (SendCallbackData *) data;
    if (!cancelled) {
        HandleScope hs(sc->isolate);
        node::MakeCallback(sc->isolate, sc->isolate->GetCurrentContext()->Global(), Local<Function>::New(sc->isolate, sc->jsCallback), 0, nullptr);
    }
    sc->jsCallback.Reset();
    delete sc;
}

template <bool isServer>
void send(const FunctionCallbackInfo<Value> &args)
{
    uWS::OpCode opCode = (uWS::OpCode) args[2]->IntegerValue();
    NativeString nativeString(args[1]);

    SendCallbackData *sc = nullptr;
    void (*callback)(void *, void *, bool) = nullptr;

    if (args[3]->IsFunction()) {
        callback = sendCallback;
        sc = new SendCallbackData;
        sc->jsCallback.Reset(args.GetIsolate(), Local<Function>::Cast(args[3]));
        sc->isolate = args.GetIsolate();
    }

    unwrapSocket<isServer>(args[0].As<External>()).send(nativeString.getData(),
                           nativeString.getLength(), opCode, callback, sc);
}

void connect(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<uWS::CLIENT> *clientGroup = (uWS::Group<uWS::CLIENT> *) args[0].As<External>()->Value();
    NativeString uri(args[1]);
    hub.connect(std::string(uri.getData(), uri.getLength()), new Persistent<Value>(args.GetIsolate(), args[2]), 5000, clientGroup);
}

struct Ticket {
    uv_os_sock_t fd;
    SSL *ssl;
};

void upgrade(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<uWS::SERVER> *serverGroup = (uWS::Group<uWS::SERVER> *) args[0].As<External>()->Value();
    Ticket *ticket = (Ticket *) args[1].As<External>()->Value();
    NativeString secKey(args[2]);
    NativeString extensions(args[3]);

    // todo: move this check into core!
    if (ticket->fd != INVALID_SOCKET) {
        hub.upgrade(ticket->fd, secKey.getData(), ticket->ssl, extensions.getData(), extensions.getLength(), serverGroup);
    } else {
        if (ticket->ssl) {
            SSL_free(ticket->ssl);
        }
    }
    delete ticket;
}

void transfer(const FunctionCallbackInfo<Value> &args) {
    // (_handle.fd OR _handle), SSL
    Ticket *ticket = new Ticket;
    if (args[0]->IsObject()) {
        uv_fileno(getTcpHandle(args[0]->ToObject()->GetAlignedPointerFromInternalField(0)), (uv_os_fd_t *) &ticket->fd);
    } else {
        ticket->fd = args[0]->IntegerValue();
    }

    ticket->fd = dup(ticket->fd);
    ticket->ssl = nullptr;
    if (args[1]->IsExternal()) {
        ticket->ssl = (SSL *) args[1].As<External>()->Value();
        SSL_up_ref(ticket->ssl);
    }

    args.GetReturnValue().Set(External::New(args.GetIsolate(), ticket));
}

template <bool isServer>
void onConnection(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    GroupData *groupData = (GroupData *) group->getUserData();

    Isolate *isolate = args.GetIsolate();
    Persistent<Function> *connectionCallback = groupData->connectionHandler;
    connectionCallback->Reset(isolate, Local<Function>::Cast(args[1]));
    group->onConnection([isolate, connectionCallback](uWS::WebSocket<isServer> webSocket) {
        HandleScope hs(isolate);
        Local<Value> argv[] = {wrapSocket(webSocket, isolate)};
        node::MakeCallback(isolate, isolate->GetCurrentContext()->Global(), Local<Function>::New(isolate, *connectionCallback), 1, argv);
    });
}

template <bool isServer>
void onMessage(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    GroupData *groupData = (GroupData *) group->getUserData();

    Isolate *isolate = args.GetIsolate();
    Persistent<Function> *messageCallback = groupData->messageHandler;
    messageCallback->Reset(isolate, Local<Function>::Cast(args[1]));
    group->onMessage([isolate, messageCallback](uWS::WebSocket<isServer> webSocket, const char *message, size_t length, uWS::OpCode opCode) {
        HandleScope hs(isolate);
        Local<Value> argv[] = {wrapMessage(message, length, opCode, isolate),
                               getDataV8(webSocket, isolate)};
        node::MakeCallback(isolate, isolate->GetCurrentContext()->Global(), Local<Function>::New(isolate, *messageCallback), 2, argv);
    });
}

template <bool isServer>
void onPing(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    GroupData *groupData = (GroupData *) group->getUserData();

    Isolate *isolate = args.GetIsolate();
    Persistent<Function> *pingCallback = groupData->pingHandler;
    pingCallback->Reset(isolate, Local<Function>::Cast(args[1]));
    group->onPing([isolate, pingCallback](uWS::WebSocket<isServer> webSocket, const char *message, size_t length) {
        HandleScope hs(isolate);
        Local<Value> argv[] = {wrapMessage(message, length, uWS::OpCode::PING, isolate),
                               getDataV8(webSocket, isolate)};
        node::MakeCallback(isolate, isolate->GetCurrentContext()->Global(), Local<Function>::New(isolate, *pingCallback), 2, argv);
    });
}

template <bool isServer>
void onPong(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    GroupData *groupData = (GroupData *) group->getUserData();

    Isolate *isolate = args.GetIsolate();
    Persistent<Function> *pongCallback = groupData->pongHandler;
    pongCallback->Reset(isolate, Local<Function>::Cast(args[1]));
    group->onPong([isolate, pongCallback](uWS::WebSocket<isServer> webSocket, const char *message, size_t length) {
        HandleScope hs(isolate);
        Local<Value> argv[] = {wrapMessage(message, length, uWS::OpCode::PONG, isolate),
                               getDataV8(webSocket, isolate)};
        node::MakeCallback(isolate, isolate->GetCurrentContext()->Global(), Local<Function>::New(isolate, *pongCallback), 2, argv);
    });
}

template <bool isServer>
void onDisconnection(const FunctionCallbackInfo<Value> &args) {
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    GroupData *groupData = (GroupData *) group->getUserData();

    Isolate *isolate = args.GetIsolate();
    Persistent<Function> *disconnectionCallback = groupData->disconnectionHandler;
    disconnectionCallback->Reset(isolate, Local<Function>::Cast(args[1]));

    group->onDisconnection([isolate, disconnectionCallback](uWS::WebSocket<isServer> webSocket, int code, char *message, size_t length) {
        HandleScope hs(isolate);
        Local<Value> argv[] = {wrapSocket(webSocket, isolate),
                               Integer::New(isolate, code),
                               wrapMessage(message, length, uWS::OpCode::CLOSE, isolate),
                               getDataV8(webSocket, isolate)};
        node::MakeCallback(isolate, isolate->GetCurrentContext()->Global(), Local<Function>::New(isolate, *disconnectionCallback), 4, argv);
    });
}

template <bool isServer>
void closeSocket(const FunctionCallbackInfo<Value> &args) {
    NativeString nativeString(args[2]);
    unwrapSocket<isServer>(args[0].As<External>()).close(args[1]->IntegerValue(), nativeString.getData(), nativeString.getLength());
}

template <bool isServer>
void terminateSocket(const FunctionCallbackInfo<Value> &args) {
    unwrapSocket<isServer>(args[0].As<External>()).terminate();
}

template <bool isServer>
void closeGroup(const FunctionCallbackInfo<Value> &args)
{
    NativeString nativeString(args[2]);
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    group->close(/*args[1]->IntegerValue(), nativeString.getData(), nativeString.getLength()*/);
}

template <bool isServer>
void terminateGroup(const FunctionCallbackInfo<Value> &args)
{
    ((uWS::Group<isServer> *) args[0].As<External>()->Value())->terminate();
}

template <bool isServer>
void broadcast(const FunctionCallbackInfo<Value> &args)
{
    uWS::Group<isServer> *group = (uWS::Group<isServer> *) args[0].As<External>()->Value();
    uWS::OpCode opCode = args[2]->BooleanValue() ? uWS::OpCode::BINARY : uWS::OpCode::TEXT;
    NativeString nativeString(args[1]);
    group->broadcast(nativeString.getData(), nativeString.getLength(), opCode);
}

/*

void prepareMessage(const FunctionCallbackInfo<Value> &args)
{
    OpCode opCode = (uWS::OpCode) args[1]->IntegerValue();
    NativeString nativeString(args[0]);
    Local<Object> preparedMessage = Local<Object>::New(args.GetIsolate(), persistentTicket)->Clone();
    preparedMessage->SetAlignedPointerInInternalField(0, WebSocket::prepareMessage(nativeString.getData(), nativeString.getLength(), opCode, false));
    args.GetReturnValue().Set(preparedMessage);
}

void sendPrepared(const FunctionCallbackInfo<Value> &args)
{
    unwrapSocket(args[0]->ToNumber())
                 .sendPrepared((WebSocket::PreparedMessage *) args[1]->ToObject()->GetAlignedPointerFromInternalField(0));
}

void finalizeMessage(const FunctionCallbackInfo<Value> &args)
{
    WebSocket::finalizeMessage((WebSocket::PreparedMessage *) args[0]->ToObject()->GetAlignedPointerFromInternalField(0));
}*/

template <bool isServer>
struct Namespace {
    Local<Object> object;
    Namespace (Isolate *isolate) {
        object = Object::New(isolate);
        NODE_SET_METHOD(object, "send", send<isServer>);
        NODE_SET_METHOD(object, "close", closeSocket<isServer>);
        NODE_SET_METHOD(object, "terminate", terminateSocket<isServer>);

        Local<Object> group = Object::New(isolate);
        NODE_SET_METHOD(group, "onConnection", onConnection<isServer>);
        NODE_SET_METHOD(group, "onMessage", onMessage<isServer>);
        NODE_SET_METHOD(group, "onDisconnection", onDisconnection<isServer>);
        NODE_SET_METHOD(group, "onPing", onPing<isServer>);
        NODE_SET_METHOD(group, "onPong", onPong<isServer>);
        NODE_SET_METHOD(group, "create", createGroup<isServer>);
        NODE_SET_METHOD(group, "delete", deleteGroup<isServer>);
        NODE_SET_METHOD(group, "close", closeGroup<isServer>);
        NODE_SET_METHOD(group, "terminate", terminateGroup<isServer>);
        NODE_SET_METHOD(group, "broadcast", broadcast<isServer>);

        object->Set(String::NewFromUtf8(isolate, "group"), group);
    }
};

void Main(Local<Object> exports) {
    Isolate *isolate = exports->GetIsolate();

    exports->Set(String::NewFromUtf8(isolate, "server"), Namespace<uWS::SERVER>(isolate).object);
    exports->Set(String::NewFromUtf8(isolate, "client"), Namespace<uWS::CLIENT>(isolate).object);

    NODE_SET_METHOD(exports, "setUserData", setUserData<uWS::SERVER>);
    NODE_SET_METHOD(exports, "getUserData", getUserData<uWS::SERVER>);
    NODE_SET_METHOD(exports, "clearUserData", clearUserData<uWS::SERVER>);
    NODE_SET_METHOD(exports, "getAddress", getAddress<uWS::SERVER>);

    NODE_SET_METHOD(exports, "transfer", transfer);
    NODE_SET_METHOD(exports, "upgrade", upgrade);
    NODE_SET_METHOD(exports, "connect", connect);
}

NODE_MODULE(uws, Main)
