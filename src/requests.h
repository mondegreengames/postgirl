#pragma once 

#include <atomic>
#include <chrono>
#include <optional>
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
    DEFINE_LAST_ENUM_VALUE(OPTIONS, "OPTIONS", "OPTIONS")

#define DEFINE_BODY_TYPES \
    DEFINE_ENUM_VALUE(NONE, "none", "none") \
    DEFINE_ENUM_VALUE(MULTIPART_FORMDATA, "formdata", "form-data") \
    DEFINE_ENUM_VALUE(URL_ENCODED, "urlencoded", "x-www-form-urlencoded") \
    DEFINE_ENUM_VALUE(RAW, "raw", "raw") \
    DEFINE_LAST_ENUM_VALUE(FILE, "file", "binary")

#define DEFINE_RAW_BODY_TYPES \
    DEFINE_ENUM_VALUE(TEXT, "text/plain", "Text") \
    DEFINE_ENUM_VALUE(JSON, "application/json", "JSON") \
    DEFINE_LAST_ENUM_VALUE(XML, "application/xml", "XML")

#define DEFINE_AUTH_TYPES \
    DEFINE_ENUM_VALUE(INHERIT, "inherit", "Inherit auth from parent") \
    DEFINE_ENUM_VALUE(NONE, "none", "None") \
    DEFINE_ENUM_VALUE(APIKEY, "apikey", "API key") \
    DEFINE_ENUM_VALUE(AWSV4, "awsv4", "AWS v4") \
    DEFINE_ENUM_VALUE(BASIC, "basic", "Basic") \
    DEFINE_ENUM_VALUE(BEARER, "bearer", "Bearer") \
    DEFINE_ENUM_VALUE(DIGEST, "digest", "Digest") \
    DEFINE_ENUM_VALUE(EDGEGRID, "edgegrid", "EdgeGrid") \
    DEFINE_ENUM_VALUE(HAWK, "hawk", "Hawk") \
    DEFINE_ENUM_VALUE(NOAUTH, "noauth", "nOAuth") \
    DEFINE_ENUM_VALUE(OAUTH1, "oauth1", "OAuth 1") \
    DEFINE_ENUM_VALUE(OAUTH2, "oauth2", "OAuth 2") \
    DEFINE_LAST_ENUM_VALUE(NTLM, "ntlm", "NTLM")

#define DEFINE_ENUM_VALUE(name, string, uistring) name,
#define DEFINE_LAST_ENUM_VALUE(name, string, uistring) DEFINE_ENUM_VALUE(name, string, uistring) _LAST = name, _COUNT = _LAST + 1

#define COUNTER_BASE __COUNTER__
enum class RequestType {
    DEFINE_REQUEST_TYPES
};

enum class BodyType {
    DEFINE_BODY_TYPES
};

enum class RawBodyType {
    DEFINE_RAW_BODY_TYPES
};

enum class AuthType {
    DEFINE_AUTH_TYPES
};

#undef DEFINE_ENUM_VALUE
#undef DEFINE_LAST_ENUM_VALUE

#define DEFINE_ENUM_VALUE(name, string, uistring) string,
#define DEFINE_LAST_ENUM_VALUE(name, string, uistring) DEFINE_ENUM_VALUE(name, string, uistring)

extern const char* requestTypeStrings[(int)RequestType::_COUNT];
extern const char* bodyTypeStrings[(int)BodyType::_COUNT];
extern const char* rawBodyTypeStrings[(int)RawBodyType::_COUNT];
extern const char* authTypeStrings[(int)AuthType::_COUNT];

extern const char* bodyTypeUIStrings[(int)BodyType::_COUNT];
extern const char* rawBodyTypeUIStrings[(int)RawBodyType::_COUNT];
extern const char* authTypeUIStrings[(int)AuthType::_COUNT];

#undef DEFINE_ENUM_VALUE
#undef DEFINE_LAST_ENUM_VALUE

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

        for (int i = 0; i < (int)RawBodyType::_COUNT; i++)
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

struct AuthAttribute
{
    pg::String key;
    pg::String value;
};

struct Auth
{
    AuthType type;
    pg::Vector<AuthAttribute> attributes;

    Auth()
        : type(AuthType::INHERIT)
    {}

    const char* findAttributeValue(const char* name) const
    {
        assert(name != nullptr);
        for (auto itr = attributes.begin(); itr != attributes.end(); ++itr) {
            if (strcmp(itr->key.buf_, name) == 0) {
                return itr->value.buf_;
            }
        }

        return nullptr;
    }

    pg::String* findAttributeValuePtr(const char* name) {
        assert(name != nullptr);
        for (auto itr = attributes.begin(); itr != attributes.end(); ++itr) {
            if (strcmp(itr->key.buf_, name) == 0) {
                return &itr->value;
            }
        }

        return nullptr;
    }

    // the value of `result` can be invalidated if any more attributes are added to the authentication!
    bool findAttributeValue(const char* name, pg::String** result) {
        assert(name != nullptr);
        for (auto itr = attributes.begin(); itr != attributes.end(); ++itr) {
            if (strcmp(itr->key.buf_, name) == 0) {
                if (result != nullptr) {
                    *result = &itr->value;
                }
                return true;
            }
        }

        return false;
    }

    bool addAttributeIfDoesntExist(const char* name, const char* value) {
        assert(name != nullptr);
        if (findAttributeValue(name) == nullptr) {
            addAttribute(name, value);
            
            return true;
        }

        return false;
    }

    void addOrUpdateAttribute(const char* name, const char* value) {
        assert(name != nullptr);
        pg::String* str;
        if (findAttributeValue(name, &str) == false) {
            addAttribute(name, value);
        }
        else {
            str->set(value);
        }
    }

    // adds the attribute if it doesn't exist, and returns a reference to the value
    void reserveAttribute(const char* name, const char* defaultValue) {
        assert(name != nullptr);
        pg::String* str;
        if (findAttributeValue(name, &str) == false) {
            addAttribute(name, defaultValue);
        }
    }

    bool removeAttribute(const char* name) {
        assert(name != nullptr);
        for (auto itr = attributes.begin(); itr != attributes.end(); ++itr) {
            if (strcmp(itr->key.buf_, name) == 0) {
                attributes.erase(itr);
                return true;
            }
        }
        return false;
    }

private:
    void addAttribute(const char* name, const char* value) {
        assert(name != nullptr);

        AuthAttribute attribute;
        attribute.key = name;

        if (value != nullptr) {
            attribute.value = value;
        }

        attributes.push_back(attribute);
    }
};

typedef struct Request
{
    pg::String url;
    //std::optional<Auth> auth;
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

    Request(const Request& request)
        : url(request.url), 
        //auth(request.auth), 
        query_args(request.query_args), 
        form_args(request.form_args), 
        headers(request.headers), 
        input_json(request.input_json), 
        req_type(request.req_type),
        body_type(request.body_type), 
        timestamp(0)
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

void threadRequestGetDelete(std::atomic<ThreadStatus>& thread_status, Request request, Auth auth,
    pg::String& thread_result, pg::Vector<HeaderKeyValue>& response_headers, int& response_code);

void threadRequestPostPatchPut(std::atomic<ThreadStatus>& thread_status, Request request, Auth auth,
    pg::String& thread_result, pg::Vector<HeaderKeyValue>& response_headers, int& response_code);

const char* RequestTypeToString(RequestType req);

const char* BodyTypeToString(BodyType ct);

pg::String prettify(pg::String input);

