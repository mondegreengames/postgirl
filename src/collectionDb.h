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

    DynamicArray<pg::String> names;
    DynamicArray<Request> requests;
    DynamicArray<Auth> auths;
    DynamicArray<CollectionTree> trees;

    DynamicBitSet namesAlive;
    DynamicBitSet requestsAlive;
    DynamicBitSet authsAlive;

    CollectionDB()
        : nextTreeId(1)
    {
    }

    size_t addName(const pg::String& name) {
        auto nameIndex = (size_t)namesAlive.findFirstWithValue(false);
        if (nameIndex >= 0 && nameIndex < names.Size) {
            names.Data[nameIndex] = name;
        }
        else {
            nameIndex = names.push_back(name);
        }
        namesAlive.set(nameIndex, true);
        return (size_t)nameIndex;
    }

    size_t addAuth(const Auth& auth) {
        auto authIndex = (size_t)authsAlive.findFirstWithValue(false);
        if (authIndex >= 0 && authIndex < auths.Size) {
            auths.Data[authIndex] = auth;
        }
        else {
            authIndex = auths.push_back(auth);
        } 
        authsAlive.set(authIndex, true);
        return (size_t)authIndex;
    }

    size_t addRequest(const Request& request) {
        auto requestIndex = (size_t)requestsAlive.findFirstWithValue(false);
        if (requestIndex >= 0 && requestIndex < requests.Size) {
            requests.Data[requestIndex] = request;
        }
        else {
            requestIndex = requests.push_back(request);
        }
        requestsAlive.set(requestIndex, true);
        return (size_t)requestIndex;
    }

    CollectionTree* getTreeByNodeId(unsigned int nodeId) {
        const unsigned int treeId = (nodeId >> CollectionTree::TreeIdShift);

        for (size_t i = 0; i < trees.Size; i++) {
            if (trees.Data[i].treeId == treeId) {
                return &trees.Data[i];
            }
        }

        return nullptr;
    }

    const CollectionTree* getTreeByNodeId(unsigned int nodeId) const {
        const unsigned int treeId = (nodeId >> CollectionTree::TreeIdShift);

        for (size_t i = 0; i < trees.Size; i++) {
            if (trees.Data[i].treeId == treeId) {
                return &trees.Data[i];
            }
        }

        return nullptr;
    }

    int getNodeIndexById(unsigned int id) {
        CollectionTree* tree = getTreeByNodeId(id);

        if (tree != nullptr) {
            return tree->getNodeIndexById(id);
        }

        return CollectionTree::InvalidIndex;
    }


    CollectionNode* getNodeById(unsigned int id) {
        CollectionTree* tree = getTreeByNodeId(id);

        if (tree != nullptr) {
            return tree->getNodeById(id);
        }

        return nullptr;
    }


    void setDirty(unsigned int nodeId, bool dirty) {
        CollectionNode* node = getNodeById(nodeId);
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

    pg::String* getName(int index) {
        if (namesAlive.isSet(index)) {
            return &names.Data[index];
        }

        return nullptr;
    }

    const char* getNameStr(int index) {
        if (namesAlive.isSet(index)) {
            return names.Data[index].buf_;
        }

        return nullptr;
    }

    Request* getRequest(int index) {
        if (requestsAlive.isSet(index)) {
            return &requests.Data[index];
        }

        return nullptr;
    }

    Auth* getAuth(int index) {
        if (authsAlive.isSet(index)) {
            return &auths.Data[index];
        }

        return nullptr;
    }

    bool buildTreeFromCollection(const Collection& collection, int treeId, CollectionTree& result)
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