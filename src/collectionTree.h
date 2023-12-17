#pragma once
#include "pgstring.h"
#include "pgvector.h"
#include "requests.h"
#include "dynamicBitSet.h"
#include "collection.h"

typedef unsigned int NodeId;

struct CollectionNode
{
    NodeId id;
    int requestIndex;
    int nameIndex;
    int authIndex;

    int parentIndex;
    int firstChildIndex;
    int numChildren;
    int numDescendants;

    bool isDirty;
};

class CollectionDB;

class CollectionTree
{
    // hash table
    struct {
        int capacity;
        DynamicBitSet alive;
        NodeId* ids;
        unsigned int* indices;
        bool needsRebuilding;
    } nodeHashTable;

public:

    static constexpr NodeId InvalidId = 0;
    static constexpr int InvalidIndex = -1;
    static constexpr int TreeIdShift = 16;

    unsigned int treeId;
    unsigned int nextId;
    CollectionDB* db;
    pg::Vector<CollectionNode> nodes;


    void init(CollectionDB* db, const char* name, const Auth* auth);
    
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

    int add(const char* name, const Request* request, const Auth* auth, int parentIndex, /* out */ unsigned int* id);

    void remove(int index);

    int getNodeIndexById(NodeId id)
    {
        if (nodeHashTable.needsRebuilding) {
            rebuildIndexHash();
        }

        const unsigned int hash = (id * 0xDEECE66D);

        for (unsigned int i = 0; i < nodeHashTable.capacity; i++) {
            const int index = (hash + i) % nodeHashTable.capacity;
            if (nodeHashTable.alive.isSet(index) == true) {
                if (nodeHashTable.ids[index] == id) {
                    return nodeHashTable.indices[index];
                }
            }
            else {
                break;
            }
        }
                
        return InvalidIndex;
    }

    CollectionNode* getNodeById(NodeId id)
    {
        int index = getNodeIndexById(id);
        if (index == InvalidIndex) {
            return nullptr;
        }

        return &nodes[index];
    }

    void setDirty(NodeId id, bool dirty) {
        CollectionNode* node = getNodeById(id);
        if (node != nullptr) {
            node->isDirty = dirty;
        }
    }

    bool isDirty(NodeId id) {
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

bool addToTree(int parentIndex, const Item* item, CollectionTree& tree);
