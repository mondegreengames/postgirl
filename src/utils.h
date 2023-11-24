#include <cstdio>
#include "requests.h"
#include "pgstring.h"
#include "pgvector.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "imgui.h"


void readIntFromIni(int& res, FILE* fid);

void readStringFromIni(char* buffer, FILE* fid);

void printArg(const Argument& arg);


void printHeader(const HeaderKeyValue& header);

const char* Stristr(const char* haystack, const char* haystack_end, const char* needle, const char* needle_end);

void Help(const char* desc);
