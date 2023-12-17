#pragma once

#include "dynamicArray.h"
#include "pgstring.h"
#include "collection.h"
#include "collectionTree.h"

// TODO: create a "database" of collections that acts as a parent of a bunch of collection trees
// we can store all of the lists (like requests, auths, etc) in a massive VirtualAlloc() that can grow as needed.
// That way we can guarantee that pointers will stay valid, no matter how many times it needs to grow.
// - move names, requests, auth into db and out of CollectionTree
// - every tree needs to belong to a collectiondb to work

class CollectionDB
{
public:
    unsigned int nextTreeId;

    DynamicDeletableArray<pg::String> names;
    DynamicDeletableArray<Request> requests;
    DynamicDeletableArray<Auth> auths;
    DynamicDeletableArray<Response> responses;
    DynamicArray<CollectionTree> trees;

    CollectionDB()
        : nextTreeId(1)
    {
    }

    size_t addName(const pg::String& name) {
        return names.insert(name);
    }

    size_t addAuth(const Auth& auth) {
        return auths.insert(auth);
    }

    size_t addRequest(const Request& request) {
        return requests.insert(request);
    }

    CollectionTree* getTreeByNodeId(NodeId nodeId) {
        const unsigned int treeId = (nodeId >> CollectionTree::TreeIdShift);

        for (size_t i = 0; i < trees.Size; i++) {
            if (trees.Data[i].treeId == treeId) {
                return &trees.Data[i];
            }
        }

        return nullptr;
    }

    const CollectionTree* getTreeByNodeId(NodeId nodeId) const {
        const unsigned int treeId = (nodeId >> CollectionTree::TreeIdShift);

        for (size_t i = 0; i < trees.Size; i++) {
            if (trees.Data[i].treeId == treeId) {
                return &trees.Data[i];
            }
        }

        return nullptr;
    }

    int getNodeIndexById(NodeId id) {
        CollectionTree* tree = getTreeByNodeId(id);

        if (tree != nullptr) {
            return tree->getNodeIndexById(id);
        }

        return CollectionTree::InvalidIndex;
    }


    CollectionNode* getNodeById(NodeId id) {
        CollectionTree* tree = getTreeByNodeId(id);

        if (tree != nullptr) {
            return tree->getNodeById(id);
        }

        return nullptr;
    }


    void setDirty(NodeId nodeId, bool dirty) {
        CollectionNode* node = getNodeById(nodeId);
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

    pg::String* getName(int index) {
        return names.tryGetPtr(index);
    }

    const char* getNameStr(int index) {
        auto ptr = names.tryGetPtr(index);
        if (ptr != nullptr) {
            return ptr->buf_;
        }

        return nullptr;
    }

    Request* getRequest(int index) {
        return requests.tryGetPtr(index);
    }

    Auth* getAuth(int index) {
        return auths.tryGetPtr(index);
    }

    bool buildTreeFromCollection(const Collection& collection, CollectionTree& result)
    {
        const char* name = nullptr;
        if (collection.name.buf_[0] != 0) {
            name = collection.name.buf_;
        }

        const Auth* auth = &collection.auth;

        result.init(this, name, auth);

        for (auto itr = collection.root.begin(); itr != collection.root.end(); ++itr) {

            if (addToTree(0, itr, result) == false) {
                return false;
            }
        }

        trees.push_back(result);

        return true;
    }
};