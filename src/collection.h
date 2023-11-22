#pragma once
#include <variant>
#include "requests.h"
#include "pgstring.h"
#include "pgvector.h"

struct Item
{
    pg::String name;

    std::variant<pg::Vector<Item>, Request> Data;
};

struct Collection
{
    pg::Vector<Item> Root;

    static bool Load(const char* filename, Collection& result);
};
