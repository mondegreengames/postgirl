#include "collectionTree.h"
#include "collectionDb.h"

void CollectionTree::init(CollectionDB* db, const char* name, const Auth* auth)
{
    assert(db != nullptr);
    this->db = db;

    nextId = 1;
    treeId = db->nextTreeId++;

    int nameIndex = InvalidIndex;
    if (name != nullptr) {
        nameIndex = db->addName(name);
    }

    int authIndex = InvalidIndex;
    if (auth != nullptr) {
        authIndex = db->addAuth(*auth);
    }

    CollectionNode node = {
        .id = (treeId << TreeIdShift) + nextId++,
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

int CollectionTree::add(const char* name, const Request* request, const Auth* auth, int parentIndex, /* out */ unsigned int* id)
{
    if (parentIndex < 0 || parentIndex >= nodes.Size) {
        return InvalidIndex;
    }

    int nameIndex = InvalidIndex;
    if (name != nullptr) {
        nameIndex = db->addName(name);
    }

    int authIndex = InvalidIndex;
    if (auth != nullptr) {
        authIndex = db->addAuth(*auth);
    }

    int requestIndex = InvalidIndex;
    if (request != nullptr) {
        requestIndex = db->addRequest(*request);
    }

    CollectionNode node = {
        .id = (treeId << TreeIdShift) + nextId,
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
        *id = (treeId << TreeIdShift) + nextId;
    }
    nextId++;

    nodeHashTable.needsRebuilding = true;

    return index;
}

void CollectionTree::remove(int index)
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
            db->namesAlive.set(nameIndex, false);
        }
        if (authIndex != InvalidIndex) {
            db->authsAlive.set(authIndex, false);
        }
        if (requestIndex != InvalidIndex) {
            db->requestsAlive.set(requestIndex, false);
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

bool addToTree(int parentIndex, const Item* item, CollectionTree& tree)
{
    const char* name = nullptr;
    if (item->name.buf_[0] != 0) {
        name = item->name.buf_;
    }

    const Auth* auth = nullptr;
    if (item->auth.has_value()) {
        auth = &item->auth.value();
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