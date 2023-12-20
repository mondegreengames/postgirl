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

struct TokenIterator {
	std::string_view source;
	char delimiter;
	size_t index;

	static void init(const char* source, char delimiter, TokenIterator& result) {
		result.source = source;
		result.delimiter = delimiter;
		result.index = 0;
	}

	static void init(std::string_view source, char delimiter, TokenIterator& result) {
		result.source = source;
		result.delimiter = delimiter;
		result.index = 0;
	}

	bool next(std::string_view& result) {
		if (peek(result)) {
			index += result.length();
			return true;
		}

		return false;
	}

	bool peek(std::string_view& result) {
		while (index < source.length() && source[index] == delimiter) {
			index++;
		}

		if (index >= source.length()) {
			return false;
		}

		size_t end = index;
		while (end < source.length() && source[end] != delimiter) {
			end++;
		}

		result = source.substr(index, end - index);
		return true;
	}
};

struct UrlParts {
	const char* host;
	const char* scheme;
	const char* user;
	const char* password;
	const char* port;
	const char* path;
	const char* query;
	const char* fragment;
	const char* zoneId;

	void* _internal;
};

enum UrlPartsFlags : unsigned int {
	URL_PARTS_FLAG_HOST = 1 << 0,
	URL_PARTS_FLAG_SCHEME = 1 << 1,
	URL_PARTS_FLAG_USER = 1 << 2,
	URL_PARTS_FLAG_PASSWORD = 1 << 3,
	URL_PARTS_FLAG_PORT = 1 << 4,
	URL_PARTS_FLAG_PATH = 1 << 5,
	URL_PARTS_FLAG_QUERY = 1 << 6,
	URL_PARTS_FLAG_FRAGMENT = 1 << 7,
	URL_PARTS_FLAG_ZONEID = 1 << 8,

	URL_PARTS_FLAG_ALL = 0xffffffff,
};

bool ParseUrl(const char* url, unsigned int flags, UrlParts& result);
void FreeUrl(UrlParts& url);
