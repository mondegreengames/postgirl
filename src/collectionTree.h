#pragma once
#include "pgstring.h"
#include "pgvector.h"
#include "requests.h"
#include "dynamicBitSet.h"
#include "collection.h"

struct CollectionNode
{
    int requestIndex;
    int nameIndex;
    int authIndex;

    int parentIndex;
    int firstChildIndex;
    int numChildren;
    int numDescendants;
};



class CollectionTree
{
public:

    static constexpr int InvalidIndex = -1;

    pg::Vector<pg::String> names;
    pg::Vector<Request> requests;
    pg::Vector<Auth> auths;
    pg::Vector<CollectionNode> nodes;
    DynamicBitSet namesAlive;
    DynamicBitSet requestsAlive;
    DynamicBitSet authsAlive;

    void init(const char* name, const Auth* auth)
    {
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
            .requestIndex = InvalidIndex,
            .nameIndex = nameIndex,
            .authIndex = authIndex,
            .parentIndex = InvalidIndex,
            .firstChildIndex = InvalidIndex,
            .numChildren = 0,
            .numDescendants = 0,
        };
        nodes.push_back(node);
    }

    int add(const char* name, const Request* request, const Auth* auth, int parentIndex)
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
            .requestIndex = requestIndex,
            .nameIndex = nameIndex,
            .authIndex = authIndex,
            .parentIndex = parentIndex,
            .firstChildIndex = InvalidIndex,
            .numChildren = 0,
            .numDescendants = 0,
        };
        int index = parentIndex + nodes[parentIndex].numDescendants + 1;
        nodes.insert(nodes.begin() + index, node);
        nodes[parentIndex].numChildren++;

        int cursor = parentIndex;
        while(cursor != InvalidIndex) {
            nodes[cursor].numDescendants++;
            cursor = nodes[cursor].parentIndex;
        }

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

    int index = tree.add(name, req, auth, parentIndex);
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
}