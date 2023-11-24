#pragma once 

#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include "pgstring.h"
#include "pgvector.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

#ifdef _WIN32
#undef DELETE
#endif


typedef enum ThreadStatus {
    IDLE     = 0,
    RUNNING  = 1,
    FINISHED = 2
} ThreadStatus;

#define DEFINE_REQUEST_TYPES \
    DEFINE_ENUM_VALUE(GET, "GET", "GET") \
    DEFINE_ENUM_VALUE(POST, "POST", "POST") \
    DEFINE_ENUM_VALUE(DELETE, "DELETE", "DELETE") \
    DEFINE_ENUM_VALUE(PATCH, "PATCH", "PATCH") \
    DEFINE_ENUM_VALUE(PUT, "PUT", "PUT") \
    DEFINE_ENUM_VALUE(OPTIONS, "OPTIONS", "OPTIONS")

#define DEFINE_BODY_TYPES \
    DEFINE_ENUM_VALUE(NONE, "none", "none") \
    DEFINE_ENUM_VALUE(MULTIPART_FORMDATA, "formdata", "form-data") \
    DEFINE_ENUM_VALUE(URL_ENCODED, "urlencoded", "x-www-form-urlencoded") \
    DEFINE_ENUM_VALUE(RAW, "raw", "raw") \
    DEFINE_ENUM_VALUE(FILE, "file", "binary")

#define DEFINE_RAW_BODY_TYPES \
    DEFINE_ENUM_VALUE(TEXT, "text/plain", "Text") \
    DEFINE_ENUM_VALUE(JSON, "application/json", "JSON") \
    DEFINE_ENUM_VALUE(XML, "application/xml", "XML")

#define DEFINE_ENUM_VALUE(name, string, uistring) name,

enum class RequestType {
    DEFINE_REQUEST_TYPES
};

enum class BodyType {
    DEFINE_BODY_TYPES
};

#undef DEFINE_ENUM_VALUE

#define DEFINE_ENUM_VALUE(name, string, uistring) string,

static const char* requestTypeStrings[] = {
    DEFINE_REQUEST_TYPES
};
constexpr int requestTypeStringsLength = sizeof(requestTypeStrings) / sizeof(const char*);

static const char* bodyTypeStrings[] = {
    DEFINE_BODY_TYPES
};
constexpr int bodyTypeStringsLength = sizeof(bodyTypeStrings) / sizeof(const char*);

static const char* rawBodyTypeStrings[] = {
    DEFINE_RAW_BODY_TYPES
};
constexpr int rawBodyTypeStringsLength = sizeof(rawBodyTypeStrings) / sizeof(const char*);

#undef DEFINE_ENUM_VALUE

#define DEFINE_ENUM_VALUE(name, string, uistring) uistring,

static const char* bodyTypeUIStrings[] = {
    DEFINE_BODY_TYPES
};

static const char* rawBodyTypeUIStrings[] = {
    DEFINE_RAW_BODY_TYPES
};

#undef DEFINE_ENUM_VALUE

typedef struct HeaderKeyValue {
    pg::String key;
    pg::String value;
    bool enabled;
} HeaderKeyValue;

typedef struct HeaderKeyValueCollection {
    pg::Vector<HeaderKeyValue> headers;

    const char* findHeaderValue(const char* key)
    {
        return findHeaderValue(headers, key);
    }

    static const char* findHeaderValue(const pg::Vector<HeaderKeyValue>& headers, const char* key)
    {
        for (auto itr = headers.begin(); itr != headers.end(); ++itr)
        {
            if ((*itr).key == key)
            {
                return (*itr).value.buf_;
            }
        }

        return nullptr;
    }

    static int contentTypeToRawBodyTypeIndex(const char* contentType)
    {
        if (contentType == nullptr) return -1;

        for (int i = 0; i < rawBodyTypeStringsLength; i++)
        {
            if (strcmp(contentType, rawBodyTypeStrings[i]) == 0)
                return i;
        }

        return -1;
    }

    void setHeaderValue(const char* key, const char* value)
    {
        // does the key already exist?
        for (auto itr = headers.begin(); itr != headers.end(); ++itr)
        {
            if ((*itr).key == key)
            {
                (*itr).value.set(value);
                return;
            }
        }

        headers.push_back(HeaderKeyValue { .key = key, .value = value, .enabled = true });
    }
} HeaderKeyValueCollection;

typedef struct Argument { 
    pg::String name;
    pg::String value;
    int arg_type; // TODO: transform this to an ENUM soon!!!!
} Argument; 

typedef struct Request
{
    pg::String url;
    pg::Vector<Argument> query_args;
    pg::Vector<Argument> form_args;
    HeaderKeyValueCollection headers;
    pg::String input_json;
    RequestType req_type;
    BodyType body_type;
    uint64_t timestamp;

    Request()
        : req_type(RequestType::GET), body_type(BodyType::MULTIPART_FORMDATA), timestamp(0)
    {}

} Request;

typedef struct Response
{
    pg::String result;
    HeaderKeyValueCollection result_headers;
    uint64_t timestamp;
    int response_code;

    Response()
        : timestamp(0), response_code(0)
    {
    }
} Response;




pg::String buildUrl(const char* baseUrl, const pg::Vector<Argument>& args);
bool deconstructUrl(const char* url, pg::Vector<Argument>& args);

void threadRequestGetDelete(std::atomic<ThreadStatus>& thread_status, RequestType reqType,  
                      pg::String url, pg::Vector<Argument> args, pg::Vector<HeaderKeyValue> headers, 
                      BodyType contentType, pg::String& thread_result, pg::Vector<HeaderKeyValue>& response_headers, int& response_code);

void threadRequestPostPatchPut(std::atomic<ThreadStatus>& thread_status, RequestType reqType,
                      pg::String url, pg::Vector<Argument> query_args, pg::Vector<Argument> form_args,
                      pg::Vector<HeaderKeyValue> headers, 
                      BodyType contentType, const pg::String& inputJson, 
                      pg::String& thread_result, pg::Vector<HeaderKeyValue>& response_headers, int& response_code);

const char* RequestTypeToString(RequestType req);

const char* BodyTypeToString(BodyType ct);

pg::String prettify(pg::String input);

