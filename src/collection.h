#pragma once
#include <variant>
#include "requests.h"
#include "pgstring.h"
#include "pgvector.h"

struct Item
{
    pg::String name;
    Auth auth;

    std::variant<pg::Vector<Item>, Request> data;
};

struct Collection
{
    pg::Vector<Item> root;
    Auth auth;

    static bool Load(const char* filename, Collection& result);
};
