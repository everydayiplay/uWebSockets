// NOTE: This is not part of the library, this file holds examples and tests

#include "uWS.h"
#include <iostream>
#include <chrono>
#include <cmath>
#include <thread>
#include <fstream>

int countOccurrences(std::string word, std::string &document) {
    int count = 0;
    for (size_t pos = document.find(word); pos != std::string::npos; pos = document.find(word, pos + word.length())) {
        count++;
    }
    return count;
}

void serveAutobahn() {
    uWS::Hub h;

    uWS::Group<uWS::SERVER> sslGroup = *h.createGroup<uWS::SERVER>();
    uWS::Group<uWS::SERVER> group = *h.createGroup<uWS::SERVER>();

    auto messageHandler = [](uWS::WebSocket<uWS::SERVER> ws, char *message, size_t length, uWS::OpCode opCode) {
        ws.send(message, length, opCode);
    };

    sslGroup.onMessage(messageHandler);
    group.onMessage(messageHandler);

    sslGroup.onDisconnection([&sslGroup](uWS::WebSocket<uWS::SERVER> ws, int code, char *message, size_t length) {
        static int disconnections = 0;
        if (++disconnections == 519) {
            std::cout << "SSL server done with Autobahn, shutting down!" << std::endl;
            sslGroup.close();
        }
    });

    group.onDisconnection([&group](uWS::WebSocket<uWS::SERVER> ws, int code, char *message, size_t length) {
        static int disconnections = 0;
        if (++disconnections == 519) {
            std::cout << "Non-SSL server done with Autobahn, shutting down!" << std::endl;
            group.close();
        }
    });

    uS::TLS::Context c = uS::TLS::createContext("/home/alexhultman/uws-connections-dropped/secrets/cert.pem",
                                                "/home/alexhultman/uws-connections-dropped/secrets/key.pem");
    if (!h.listen(3001, c, &sslGroup) || !h.listen(3000, nullptr, &group)) {
        std::cout << "FAILURE: Error listening for Autobahn connections!" << std::endl;
        exit(-1);
    } else {
        std::cout << "Acepting Autobahn connections (SSL/non-SSL)" << std::endl;
    }

    std::thread t([]() {
        system("cd /home/alexhultman/uWebSockets && wstest -m fuzzingclient -s Autobahn.json");
    });

    h.run();
    t.join();

    // "FAILED", "OK", "NON-STRICT"
    std::ifstream fin("/home/alexhultman/uWebSockets/autobahn/index.json");
    fin.seekg (0, fin.end);
    int length = fin.tellg();
    fin.seekg (0, fin.beg);
    char *buffer = new char[length];
    fin.read(buffer, length);
    std::string index(buffer, length);

    std::cout << std::endl << std::endl;
    std::cout << "OK: " << countOccurrences("\"OK\"", index) << std::endl;
    std::cout << "NON-STRICT: " << countOccurrences("\"NON-STRICT\"", index) << std::endl;
    std::cout << "FAILED: " << countOccurrences("\"FAILED\"", index) << std::endl;

    delete [] buffer;
}

void serveBenchmark() {
    uWS::Hub h;

    h.onMessage([](uWS::WebSocket<uWS::SERVER> ws, char *message, size_t length, uWS::OpCode opCode) {
        ws.send(message, length, opCode);
    });

    h.listen(3000);
    h.run();
}

void measureInternalThroughput(int payloadLength, int echoes, bool ssl) {
    uWS::Hub h;

    char *payload = new char[payloadLength];
    for (int i = 0; i < payloadLength; i++) {
        payload[i] = rand();
    }

    char *closeMessage = "I'm closing now";
    size_t closeMessageLength = strlen(closeMessage);

    uS::TLS::Context c = uS::TLS::createContext("/home/alexhultman/uws-connections-dropped/secrets/cert.pem",
                                                "/home/alexhultman/uws-connections-dropped/secrets/key.pem");

    h.onConnection([payload, payloadLength, echoes](uWS::WebSocket<uWS::CLIENT> ws) {
        for (int i = 0; i < echoes; i++) {
            ws.send(payload, payloadLength, uWS::OpCode::BINARY);
        }
    });

    h.onError([](void *user) {
        std::cout << "FAILURE: Connection failed! Timeout?" << std::endl;
        exit(-1);
    });

    h.onMessage([payload, payloadLength, closeMessage, closeMessageLength, echoes](uWS::WebSocket<uWS::CLIENT> ws, char *message, size_t length, uWS::OpCode opCode) {
        if (strncmp(message, payload, payloadLength) || payloadLength != length) {
            std::cout << "FAIL: Messages differ!" << std::endl;
            exit(-1);
        }

        static int echoesPerformed = 0;
        if (echoesPerformed++ == echoes) {
            echoesPerformed = 0;
            ws.close(1000, closeMessage, closeMessageLength);
        } else {
            ws.send(payload, payloadLength, opCode);
        }
    });

    h.onDisconnection([](uWS::WebSocket<uWS::CLIENT> ws, int code, char *message, size_t length) {
        std::cout << "CLIENT CLOSE: " << code << std::endl;
    });

    h.onMessage([](uWS::WebSocket<uWS::SERVER> ws, char *message, size_t length, uWS::OpCode opCode) {
        ws.send(message, length, opCode);
    });

    h.onDisconnection([&h, closeMessage, closeMessageLength](uWS::WebSocket<uWS::SERVER> ws, int code, char *message, size_t length) {
        std::cout << "SERVER CLOSE: " << code << std::endl;
        if (!length) {
            std::cout << "FAIL: No close message! Code: " << code << std::endl;
            exit(-1);
        }

        if (strncmp(message, closeMessage, closeMessageLength) || closeMessageLength != length) {
            std::cout << "FAIL: Close message differ!" << std::endl;
            exit(-1);
        }

        if (code != 1000) {
            std::cout << "FAIL: Close code differ!" << std::endl;
            exit(-1);
        }

        h.getDefaultGroup<uWS::SERVER>().close();
    });

    // we need to update libuv internal timepoint!
    uv_update_time(uv_default_loop());

    if (ssl) {
        if (!h.listen(3000, c)) {
            std::cout << "FAIL: ERR_LISTEN" << std::endl;
            exit(-1);
        }
        h.connect("wss://localhost:3000", nullptr);
    } else {
        if (!h.listen(3000)) {
            std::cout << "FAIL: ERR_LISTEN" << std::endl;
            exit(-1);
        }
        h.connect("ws://localhost:3000", nullptr);
    }

    std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
    start = std::chrono::high_resolution_clock::now();
    h.run();
    end = std::chrono::high_resolution_clock::now();
    int ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    if (!ms) {
        std::cout << "Too small load!" << std::endl;
    } else {
        std::cout << "Throughput: " << (float(echoes) / float(ms)) << " echoes/ms (" << ms << " ms elapsed)" << std::endl;
    }

    delete [] payload;
}

void stressTest() {
    for (int i = 0; i < 25; i++) {
        int payloadLength = std::pow(2, i);
        int echoes = 1;//std::max<int>(std::pow(2, 24 - i) / 50, 1);

        std::cout << "[Size: " << payloadLength << ", echoes: " << echoes << "]" << std::endl;
        for (int ssl = 0; ssl < 2; ssl++) {
            std::cout << "SSL: " << bool(ssl) << std::endl;
            measureInternalThroughput(payloadLength, echoes, ssl);
        }
    }
}

void testConnections() {
    uWS::Hub h;

    h.onError([](void *user) {
        switch ((long) user) {
        case 1:
            std::cout << "Client emitted error on invalid URI" << std::endl;
            break;
        case 2:
            std::cout << "Client emitted error on resolve failure" << std::endl;
            break;
        case 3:
            std::cout << "Client emitted error on connection timeout (non-SSL)" << std::endl;
            break;
        case 5:
            std::cout << "Client emitted error on connection timeout (SSL)" << std::endl;
            break;
        case 6:
            std::cout << "Client emitted error on HTTP response without upgrade (non-SSL)" << std::endl;
            break;
        case 7:
            std::cout << "Client emitted error on HTTP response without upgrade (SSL)" << std::endl;
            break;
        default:
            std::cout << "FAILURE: " << user << " should not emit error!" << std::endl;
            exit(-1);
        }
    });

    h.onConnection([](uWS::WebSocket<uWS::CLIENT> ws) {
        switch ((long) ws.getUserData()) {
        case 4:
            std::cout << "Client established a remote connection over non-SSL" << std::endl;
            ws.close(1000);
            break;
        case 5:
            std::cout << "Client established a remote connection over SSL" << std::endl;
            ws.close(1000);
            break;
        default:
            std::cout << "FAILURE: " << ws.getUserData() << " should not connect!" << std::endl;
            exit(-1);
        }
    });

    h.onDisconnection([](uWS::WebSocket<uWS::CLIENT> ws, int code, char *message, size_t length) {
        std::cout << "Client got disconnected with data: " << ws.getUserData() << ", code: " << code << ", message: <" << std::string(message, length) << ">" << std::endl;
    });

    h.connect("invalid URI", (void *) 1);
    h.connect("ws://validButUnknown.yolo", (void *) 2);
    h.connect("ws://echo.websocket.org", (void *) 3, 10);
    h.connect("ws://echo.websocket.org", (void *) 4);
    h.connect("wss://echo.websocket.org", (void *) 5, 10);
    h.connect("wss://echo.websocket.org", (void *) 5);
    h.connect("ws://google.com", (void *) 6);
    h.connect("wss://google.com", (void *) 7);

    h.run();
}

void testListening() {
    uWS::Hub h;

    h.onError([](int port) {
        switch (port) {
        case 80:
            std::cout << "Server emits error listening to port 80 (permission denied)" << std::endl;
            break;
        case 3000:
            std::cout << "Server emits error listening to port 3000 twice" << std::endl;
            break;
        default:
            std::cout << "FAILURE: port " << port << " should not emit error" << std::endl;
            exit(-1);
        }
    });

    h.listen(80);
    if (h.listen(3000)) {
        std::cout << "Server listens to port 3000" << std::endl;
    }
    h.listen(3000);
    h.getDefaultGroup<uWS::SERVER>().close();
    h.run();
    std::cout << "Server falls through after group closes" << std::endl;
}

void testClosing() {
    uWS::Hub h;
    char *closeMessage = "Closing you down!";

    h.onConnection([&h, closeMessage](uWS::WebSocket<uWS::SERVER> ws) {
        ws.terminate();
        h.onConnection([&h, closeMessage](uWS::WebSocket<uWS::SERVER> ws) {
            ws.close(1000, closeMessage, strlen(closeMessage));
        });
        h.connect("ws://localhost:3000", (void *) 2);
    });

    h.onDisconnection([closeMessage](uWS::WebSocket<uWS::CLIENT> ws, int code, char *message, size_t length) {
        switch ((long) ws.getUserData()) {
        case 1:
            if (code == 1006) {
                std::cout << "Client gets terminated on first connection" << std::endl;
            } else {
                std::cout << "FAILURE: Terminate leads to invalid close code!" << std::endl;
                exit(-1);
            }
            break;
        case 2:
            if (code == 1000 && length == strlen(closeMessage) && !strncmp(closeMessage, message, length)) {
                std::cout << "Client gets closed on second connection with correct close code" << std::endl;
            } else {
                std::cout << "FAILURE: Close leads to invalid close code or message!" << std::endl;
                exit(-1);
            }
            break;
        }
    });

    h.onDisconnection([&h, closeMessage](uWS::WebSocket<uWS::SERVER> ws, int code, char *message, size_t length) {
        if (code == 1006) {
            std::cout << "Server recives terminate close code after terminating" << std::endl;
        } else if (code != 1000) {
            std::cout << "FAILURE: Server does not receive correct close code!" << std::endl;
            exit(-1);
        } else {
            std::cout << "Server receives correct close code after closing" << std::endl;
            h.getDefaultGroup<uWS::SERVER>().close();
        }
    });

    if (!h.listen(3000)) {
        std::cout << "FAILURE: Cannot listen to port 3000!" << std::endl;
        exit(-1);
    }
    h.connect("ws://localhost:3000", (void *) 1);

    h.run();
    std::cout << "Falling through after testClosing!" << std::endl;
}

void testBroadcast() {
    uWS::Hub h;

    char *broadcastMessage = "This will be broadcasted!";
    size_t broadcastMessageLength = strlen(broadcastMessage);

    int connections = 14;
    h.onConnection([&h, &connections, broadcastMessage, broadcastMessageLength](uWS::WebSocket<uWS::SERVER> ws) {
        if (!--connections) {
            std::cout << "Broadcasting & closing now!" << std::endl;
            h.getDefaultGroup<uWS::SERVER>().broadcast(broadcastMessage, broadcastMessageLength, uWS::OpCode::TEXT);
            h.getDefaultGroup<uWS::SERVER>().close();
        }
    });

    int broadcasts = connections;
    h.onMessage([&broadcasts, broadcastMessage, broadcastMessageLength](uWS::WebSocket<uWS::CLIENT> ws, char *message, size_t length, uWS::OpCode opCode) {
        if (length != broadcastMessageLength || strncmp(message, broadcastMessage, broadcastMessageLength)) {
            std::cout << "FAILURE: bad broadcast message!" << std::endl;
            exit(-1);
        } else {
            broadcasts--;
        }
    });

    h.onDisconnection([](uWS::WebSocket<uWS::CLIENT> ws, int code, char *message, size_t length) {
        if (code != 1000) {
            std::cout << "FAILURE: Invalid close code!" << std::endl;
            exit(-1);
        }
    });

    h.listen(3000);

    for (int i = 0; i < connections; i++) {
        h.connect("ws://localhost:3000", nullptr);
    }

    h.run();

    if (broadcasts != 0) {
        std::cout << "FAILURE: Invalid amount of broadcasts received!" << std::endl;
        exit(-1);
    }

    std::cout << "Falling through now!" << std::endl;
}

int main(int argc, char *argv[])
{
    /*testClosing();
    testConnections();
    testListening();
    stressTest();
    serveAutobahn();*/

    testBroadcast();


    //serveBenchmark();
}
