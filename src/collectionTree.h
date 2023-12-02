#pragma once
#include "pgstring.h"
#include "pgvector.h"
#include "requests.h"
#include "dynamicBitSet.h"
#include "collection.h"

struct CollectionNode
{
    unsigned int id;
    int requestIndex;
    int nameIndex;
    int authIndex;

    int parentIndex;
    int firstChildIndex;
    int numChildren;
    int numDescendants;

    bool isDirty;
};

class CollectionTree
{
    // hash table
    struct {
        int capacity;
        DynamicBitSet alive;
        unsigned int* ids;
        unsigned int* indices;
        bool needsRebuilding;
    } nodeHashTable;

public:

    static constexpr unsigned int InvalidId = 0;
    static constexpr int InvalidIndex = -1;

    unsigned int nextId;
    pg::Vector<pg::String> names;
    pg::Vector<Request> requests;
    pg::Vector<Auth> auths;
    pg::Vector<CollectionNode> nodes;
    DynamicBitSet namesAlive;
    DynamicBitSet requestsAlive;
    DynamicBitSet authsAlive;

    void init(const char* name, const Auth* auth)
    {
        nextId = 1;

        int nameIndex = InvalidIndex;
        if (name != nullptr) {
            nameIndex = names.Size;
            names.push_back(name);
            namesAlive.set(nameIndex, true);
        }

        int authIndex = InvalidIndex;
        if (auth != nullptr) {
            authIndex = auths.Size;
            auths.push_back(*auth);
            authsAlive.set(authIndex, true);
        }

        CollectionNode node = {
            .id = nextId++,
            .requestIndex = InvalidIndex,
            .nameIndex = nameIndex,
            .authIndex = authIndex,
            .parentIndex = InvalidIndex,
            .firstChildIndex = InvalidIndex,
            .numChildren = 0,
            .numDescendants = 0,
            .isDirty = false,
        };
        nodes.push_back(node);

        nodeHashTable.capacity = 0;
        nodeHashTable.ids = nullptr;
        nodeHashTable.indices = nullptr;
        nodeHashTable.needsRebuilding = true;
    }

    void deinit()
    {
        if (nodeHashTable.capacity > 0) {
            if (nodeHashTable.ids != nullptr) {
                free(nodeHashTable.ids);
                nodeHashTable.ids = nullptr;
            }
            if (nodeHashTable.indices != nullptr) {
                free(nodeHashTable.indices);
                nodeHashTable.indices = nullptr;
            }
        }
        nodeHashTable.capacity = 0;
    }

    int add(const char* name, const Request* request, const Auth* auth, int parentIndex, /* out */ unsigned int* id)
    {
        if (parentIndex < 0 || parentIndex >= nodes.Size) {
            return InvalidIndex;
        }

        int nameIndex = InvalidIndex;
        if (name != nullptr) {
            nameIndex = namesAlive.findFirstWithValue(false);
            if (nameIndex >= 0 && nameIndex < names.Size) {
                names[nameIndex] = name;
            }
            else {
                nameIndex = names.Size;
                names.push_back(name);
            }
            namesAlive.set(nameIndex, true);
        }

        int authIndex = InvalidIndex;
        if (auth != nullptr) {
            authIndex = authsAlive.findFirstWithValue(false);
            if (authIndex >= 0 && authIndex < auths.Size) {
                auths[authIndex] = *auth;
            }
            else {
                authIndex = auths.Size;
                auths.push_back(*auth);
            } 
            authsAlive.set(authIndex, true);
        }

        int requestIndex = InvalidIndex;
        if (request != nullptr) {
            requestIndex = requestsAlive.findFirstWithValue(false);
            if (requestIndex >= 0 && requestIndex < requests.Size) {
                requests[requestIndex] = *request;
            }
            else {
                requestIndex = requests.Size;
                requests.push_back(*request);
            }
            requestsAlive.set(requestIndex, true);
        }

        CollectionNode node = {
            .id = nextId,
            .requestIndex = requestIndex,
            .nameIndex = nameIndex,
            .authIndex = authIndex,
            .parentIndex = parentIndex,
            .firstChildIndex = InvalidIndex,
            .numChildren = 0,
            .numDescendants = 0,
            .isDirty = false,
        };
        int index = parentIndex + nodes[parentIndex].numDescendants + 1;
        nodes.insert(nodes.begin() + index, node);
        nodes[parentIndex].numChildren++;

        int cursor = parentIndex;
        while(cursor != InvalidIndex) {
            nodes[cursor].numDescendants++;
            cursor = nodes[cursor].parentIndex;
        }

        if (id != nullptr) {
            *id = nextId;
        }
        nextId++;

        nodeHashTable.needsRebuilding = true;

        return index;
    }

    void remove(int index)
    {
        if (index < 0 || index >= nodes.Size) {
            return;
        }

        const int parentIndex = nodes[index].parentIndex;
        const int numDescendants = nodes[index].numDescendants;

        for (int i = index; i < index + numDescendants + 1; i++) {
            const int nameIndex = nodes[i].nameIndex;
            const int authIndex = nodes[i].authIndex;
            const int requestIndex = nodes[i].nameIndex;

            if (nameIndex != InvalidIndex) {
                namesAlive.set(nameIndex, false);
            }
            if (authIndex != InvalidIndex) {
                authsAlive.set(authIndex, false);
            }
            if (requestIndex != InvalidIndex) {
                requestsAlive.set(requestIndex, false);
            }
        }

        nodes.erase(nodes.begin() + index, nodes.begin() + index + numDescendants + 1);

        if (parentIndex != InvalidIndex) {
            nodes[parentIndex].numChildren--;

            int cursor = parentIndex;
            while(cursor != InvalidIndex) {
                nodes[cursor].numDescendants -= numDescendants + 1;
                cursor = nodes[cursor].parentIndex;
            }
        }

        nodeHashTable.needsRebuilding = true;
    }

    CollectionNode* getNodeById(unsigned int id)
    {
        if (nodeHashTable.needsRebuilding) {
            rebuildIndexHash();
        }

        const unsigned int hash = (id * 0xDEECE66D);

        for (unsigned int i = 0; i < nodeHashTable.capacity; i++) {
            const int index = (hash + i) % nodeHashTable.capacity;
            if (nodeHashTable.alive.isSet(index) == true) {
                if (nodeHashTable.ids[index] == id) {
                    CollectionNode* node = &nodes[nodeHashTable.indices[index]];
                    return node;
                }
            }
            else {
                break;
            }
        }
                
        return nullptr;
    }

    void setDirty(int id, bool dirty) {
        CollectionNode* node = getNodeById(id);
        if (node != nullptr) {
            node->isDirty = dirty;
        }
    }

    bool isDirty(int id) {
        CollectionNode* node = getNodeById(id);
        if (node != nullptr) {
            return node->isDirty;
        }

        return false;
    }

    void rebuildIndexHash() {
        if (nodeHashTable.needsRebuilding == false) {
            return;
        }

        const int minCapacity = nodes.Size * 3 / 2;
        if (nodeHashTable.capacity < minCapacity) {
            nodeHashTable.ids = (unsigned int*)realloc(nodeHashTable.ids, minCapacity * sizeof(unsigned int));
            nodeHashTable.indices = (unsigned int*)realloc(nodeHashTable.indices, minCapacity * sizeof(unsigned int));
            nodeHashTable.capacity = minCapacity;
        }
        nodeHashTable.alive.setAll(false);

        for (int i = 0; i < nodes.Size; i++) {
            const unsigned int id = nodes[i].id;
            const unsigned int hash = (id * 0xDEECE66D);

            bool inserted = false;
            for (int j = 0; j < nodeHashTable.capacity; j++) {
                const int index = (hash + j) % nodeHashTable.capacity;
                if (nodeHashTable.alive.isSet(index) == false) {
                    nodeHashTable.alive.set(index, true);
                    nodeHashTable.ids[index] = id;
                    nodeHashTable.indices[index] = i;

                    inserted = true;
                    break;
                }
            }

            assert(inserted == true && "If inserted == false then that means we were unable to insert into the hash table, which should never happen!");
        }

        nodeHashTable.needsRebuilding = false;
    }
};

bool addToTree(int parentIndex, const Item* item, CollectionTree& tree)
{
    const char* name = nullptr;
    if (item->name.buf_[0] != 0) {
        name = item->name.buf_;
    }

    const Auth* auth = nullptr;
    if (item->auth.type != AuthType::NONE || item->auth.attributes.Size > 0) {
        auth = &item->auth;
    }

    const Request* req = nullptr;
    if (item->data.index() == 1) {
        req = &std::get<Request>(item->data);
    }

    int index = tree.add(name, req, auth, parentIndex, nullptr);
    if (index == CollectionTree::InvalidIndex) {
        return false;
    }

    if (item->data.index() == 0) {
        const auto& items = std::get<pg::Vector<Item>>(item->data);
        for (auto itr = items.begin(); itr != items.end(); ++itr) {
            if (addToTree(index, itr, tree) == false) {
                return false;
            }
        }
    }

    return true;
}

bool buildTreeFromCollection(const Collection& collection, CollectionTree& result)
{
    const char* name = nullptr;
    if (collection.name.buf_[0] != 0) {
        name = collection.name.buf_;
    }

    const Auth* auth = nullptr;
    if (collection.auth.type != AuthType::NONE || collection.auth.attributes.Size > 0) {
        auth = &collection.auth;
    }

    result.init(name, auth);

    for (auto itr = collection.root.begin(); itr != collection.root.end(); ++itr) {

        if (addToTree(0, itr, result) == false) {
            return false;
        }
    }

    return true;
}