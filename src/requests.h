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
    DEFINE_ENUM_VALUE(GET, "GET") \
    DEFINE_ENUM_VALUE(POST, "POST") \
    DEFINE_ENUM_VALUE(DELETE, "DELETE") \
    DEFINE_ENUM_VALUE(PATCH, "PATCH") \
    DEFINE_ENUM_VALUE(PUT, "PUT") \
    DEFINE_ENUM_VALUE(OPTIONS, "OPTIONS")

#define DEFINE_BODY_TYPES \
    DEFINE_ENUM_VALUE(MULTIPART_FORMDATA, "formdata") \
    DEFINE_ENUM_VALUE(RAW, "raw") \
    DEFINE_ENUM_VALUE(URL_ENCODED, "urlencoded") \
    DEFINE_ENUM_VALUE(FILE, "file")

#define DEFINE_ENUM_VALUE(name, string) name,

enum class RequestType {
    DEFINE_REQUEST_TYPES
};

enum class BodyType {
    DEFINE_BODY_TYPES
};

#undef DEFINE_ENUM_VALUE

#define DEFINE_ENUM_VALUE(name, string) string,

static const char* requestTypeStrings[] = {
    DEFINE_REQUEST_TYPES
};
constexpr int requestTypeStringsLength = sizeof(requestTypeStrings) / sizeof(const char*);

static const char* bodyTypeStrings[] = {
    DEFINE_BODY_TYPES
};
constexpr int bodyTypeStringsLength = sizeof(bodyTypeStrings) / sizeof(const char*);

#undef DEFINE_ENUM_VALUE

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
    pg::Vector<Argument> headers;
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
    pg::Vector<Argument> result_headers;
    uint64_t timestamp;
    int response_code;

    Response()
        : timestamp(0), response_code(0)
    {
    }
} Response;

typedef struct History {
    Request request;
    Response response;
} History;


pg::String buildUrl(const char* baseUrl, const pg::Vector<Argument>& args);
bool deconstructUrl(const char* url, pg::Vector<Argument>& args);

void threadRequestGetDelete(std::atomic<ThreadStatus>& thread_status, RequestType reqType,  
                      pg::String url, pg::Vector<Argument> args, pg::Vector<Argument> headers, 
                      BodyType contentType, pg::String& thread_result, pg::Vector<Argument>& response_headers, int& response_code);

void threadRequestPostPatchPut(std::atomic<ThreadStatus>& thread_status, RequestType reqType,
                      pg::String url, pg::Vector<Argument> query_args, pg::Vector<Argument> form_args,
                      pg::Vector<Argument> headers, 
                      BodyType contentType, const pg::String& inputJson, 
                      pg::String& thread_result, pg::Vector<Argument>& response_headers, int& response_code);

const char* RequestTypeToString(RequestType req);

const char* BodyTypeToString(BodyType ct);

pg::String prettify(pg::String input);

