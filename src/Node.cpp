#include "Node.h"

namespace uS {

Node::Node(int recvLength, int prePadding, int postPadding) {
    nodeData = new NodeData;
    nodeData->recvBufferMemoryBlock = new char[recvLength];
    nodeData->recvBuffer = nodeData->recvBufferMemoryBlock + prePadding;
    nodeData->recvLength = recvLength - prePadding - postPadding;
    loop = uv_default_loop();
    nodeData->loop = loop;

    int indices = NodeData::getMemoryBlockIndex(NodeData::preAllocMaxSize);
    nodeData->preAlloc = new char*[indices];
    for (int i = 0; i < indices; i++) {
        nodeData->preAlloc[i] = nullptr;
    }

    nodeData->clientContext = SSL_CTX_new(SSLv23_client_method());
    SSL_CTX_set_options(nodeData->clientContext, SSL_OP_NO_SSLv3);
}

void Node::run() {
    uv_run(loop, UV_RUN_DEFAULT);
}

Node::~Node() {
    delete [] nodeData->recvBufferMemoryBlock;
    SSL_CTX_free(nodeData->clientContext);

    int indices = NodeData::getMemoryBlockIndex(NodeData::preAllocMaxSize);
    for (int i = 0; i < indices; i++) {
        if (nodeData->preAlloc[i]) {
            delete [] nodeData->preAlloc[i];
        }
    }
    delete [] nodeData->preAlloc;

    delete nodeData;
}

}
