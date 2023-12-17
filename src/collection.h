#pragma once
#include <variant>
#include "requests.h"
#include "pgstring.h"
#include "pgvector.h"

enum class VariableType {
    String,
    Boolean,
    Any,
    Number,
};

extern const char* variableTypeStrings[4];
extern const char* variableTypeUIStrings[4];

struct Variable
{
    pg::String variable;
    pg::String key;
    pg::String value;
    VariableType type;
    pg::String name;
    bool system;
    bool disabled;

    Variable()
        : type(VariableType::String), system(false), disabled(false)
    {
    }
};

struct Item
{
    pg::String name;
    Auth auth;
    pg::Vector<Variable> variables;

    std::variant<pg::Vector<Item>, Request> data;
};

struct Collection
{
    pg::Vector<Item> root;
    Auth auth;
    pg::String name;
    pg::Vector<Variable> variables;

    static bool Load(const char* filename, Collection& result);
    static bool Load(FILE* fp, Collection& result);
};
