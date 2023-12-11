#pragma once
#include "pgstring.h"
#include "pgvector.h"
#include "requests.h"

typedef struct History {
    Request request;
    Auth requestAuth;
    Response response;
} History;

void printHistory(const History& hist);

pg::Vector<History> loadHistory(const pg::String& filename);

void saveHistory(const pg::Vector<History>& histories, const pg::String& filename, bool pretty);