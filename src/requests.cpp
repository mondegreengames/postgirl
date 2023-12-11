#include "requests.h"
#include "utils.h"
#include "../third_party/llhttp/include/llhttp.h"

#define DEFINE_ENUM_VALUE(name, string, uistring) string,
#define DEFINE_LAST_ENUM_VALUE(name, string, uistring) DEFINE_ENUM_VALUE(name, string, uistring)

const char* requestTypeStrings[] = {
    DEFINE_REQUEST_TYPES
};

const char* bodyTypeStrings[] = {
    DEFINE_BODY_TYPES
};

const char* rawBodyTypeStrings[] = {
    DEFINE_RAW_BODY_TYPES
};

const char* authTypeStrings[] = {
    DEFINE_AUTH_TYPES
};

#undef DEFINE_ENUM_VALUE
#define DEFINE_ENUM_VALUE(name, string, uistring) uistring,

const char* bodyTypeUIStrings[] = {
    DEFINE_BODY_TYPES
};

const char* rawBodyTypeUIStrings[] = {
    DEFINE_RAW_BODY_TYPES
};

const char* authTypeUIStrings[] = {
    DEFINE_AUTH_TYPES
};

struct WriteThis {
  const char *readptr;
  size_t sizeleft;
};
 
static size_t read_callback(void *dest, size_t size, size_t nmemb, void *userp)
{
  struct WriteThis *wt = (struct WriteThis *)userp;
  size_t buffer_size = size*nmemb;
 
  if(wt->sizeleft) {
    /* copy as much as possible from the source to the destination */ 
    size_t copy_this_much = wt->sizeleft;
    if(copy_this_much > buffer_size)
      copy_this_much = buffer_size;
    memcpy(dest, wt->readptr, copy_this_much);
 
    wt->readptr += copy_this_much;
    wt->sizeleft -= copy_this_much;
    return copy_this_much; /* we copied this many bytes */ 
  }
 
  return 0; /* no more data left to deliver */ 
}


typedef struct MemoryStruct {
    MemoryStruct() { memory = (char*)malloc(1); size=0; };
    
    char *memory;
    size_t size;
} MemoryStruct;
 
static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL) {
        /* out of memory! */
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

struct HttpHeaderParser
{
    bool successful;
    pg::Vector<HeaderKeyValue>& headers;

    HttpHeaderParser(pg::Vector<HeaderKeyValue>& headers)
        : headers(headers)
    {
        this->headers = headers; 
    }

    bool parse(const MemoryStruct* memory)
    {
        llhttp_settings_t settings;
        llhttp_settings_init(&settings);
        settings.on_header_field = on_header_field;
        settings.on_header_value = on_header_value;

        llhttp_t parser;
        llhttp_init(&parser, HTTP_RESPONSE, &settings);
        parser.data = this;

        auto err = llhttp_execute(&parser, memory->memory, memory->size);

        return err == HPE_OK;
    }

private:
    
    static int on_header_field(llhttp_t* parser, const char *at, size_t length)
    {
        HttpHeaderParser* self = (HttpHeaderParser*)parser->data;

        HeaderKeyValue arg;
        arg.key = pg::String(at, (int)length);
        self->headers.push_back(arg);

        return 0;
    }

    static int on_header_value(llhttp_t* parser, const char *at, size_t length)
    {
        HttpHeaderParser* self = (HttpHeaderParser*)parser->data;

        if (self->headers.empty() || self->headers.back().value.length() > 0)
        {
            // we have a value, but without a header name to correlate it to
            HeaderKeyValue arg;
            arg.value = pg::String(at, (int)length);
            self->headers.push_back(arg);
        }
        else
        {
            // add the value to the most recently added header
            auto &arg = self->headers.back();
            arg.value = pg::String(at, (int)length);
        }

        return 0;
    }
};

pg::String buildUrl(const char* baseUrl, const pg::Vector<Argument>& args)
{
    CURLU* url = curl_url();
    CURLUcode err = curl_url_set(url, CURLUPART_URL, baseUrl, 0);
    if (err != CURLUE_OK)
    {
        curl_url_cleanup(url);
        return baseUrl;
    }

    // remove existing parameters
    curl_url_set(url, CURLUPART_QUERY, NULL, 0);

    pg::String buffer;
    for (int i=0; i<(int)args.size(); i++) {

        // clear
        buffer.buf_[0] = 0;

        buffer.append(args[i].name);
        buffer.append("=");
        buffer.append(args[i].value);

        curl_url_set(url, CURLUPART_QUERY, buffer.buf_, CURLU_APPENDQUERY | CURLU_URLENCODE);
    }

    char* result;
    err = curl_url_get(url, CURLUPART_URL, &result, 0);

    buffer.buf_[0] = 0;
    buffer.append(result);
    curl_free(result);
    curl_url_cleanup(url);

    return buffer;
}

char* unescapeUrl(const char* input, int length)
{
    auto version = curl_version_info(CURLVERSION_NOW);
    int major = (version->version_num >> 16) & 0xff;
    int minor = (version->version_num >> 8) & 0xff;

    if (major >= 7 || (major == 7 && minor >= 82))
    {
        return curl_easy_unescape(NULL, input, length, NULL);
    }

    return curl_unescape(input, length);
}

bool deconstructUrl(const char* url, pg::Vector<Argument>& args)
{
    CURLU* urlParser = curl_url();
    CURLUcode err = curl_url_set(urlParser, CURLUPART_URL, url, 0);
    if (err != CURLUE_OK)
    {
        curl_url_cleanup(urlParser);
        return false;
    }

    char* query;
    err = curl_url_get(urlParser, CURLUPART_QUERY, &query, 0);
    if (err != CURLUE_OK)
    {
        curl_url_cleanup(urlParser);
        return false;
    }

    // split the query string into parts
    char* rest = query;
    if (rest[0] == '?')
        rest += 1;

    char* token;
#ifdef _WIN32
    while ((token = strtok_s(rest, "&", &rest)))
#else
    while((token = strtok_r(rest, "&", &rest)))
#endif
    {
        Argument arg;

        char* eql = strchr(token, '=');
        if (eql == NULL)
            arg.name = pg::String(token); // TODO: escape
        else
        {
            arg.name = pg::String(token, eql - token); // TODO: escape
            arg.value = pg::String(eql + 1);
        }

        args.push_back(arg);
    }

    curl_free(query);
    curl_url_cleanup(urlParser);

    return true;
}

const char* findContentType(const pg::Vector<Argument>& headers)
{
    for (auto itr = headers.begin(); itr != headers.end(); ++itr)
    {
        if ((*itr).name == "Content-Type")
        {
            return (*itr).value.buf_;
        }
    }

    return nullptr;
}

void addAuthentication(CURL* curl, const Auth& authentication)
{
    if (authentication.type == AuthType::BASIC) {
        const char* username = authentication.findAttributeValue("username");
        const char* password = authentication.findAttributeValue("password");

        curl_easy_setopt(curl, CURLOPT_USERNAME, username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    }
    // TODO: The rest
}

void threadRequestGetDelete(std::atomic<ThreadStatus>& thread_status, Request request, Auth auth,
    pg::String& thread_result, pg::Vector<HeaderKeyValue>& response_headers, int& response_code) 
{ 
    CURLcode res;
    CURL* curl;
    curl = curl_easy_init();

    const char* contentType = HeaderKeyValueCollection::findHeaderValue(request.headers.headers, "Content-Type");

    MemoryStruct chunk;
    MemoryStruct headerChunk;
    if (request.req_type == RequestType::GET) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    request.url = buildUrl(request.url.buf_, request.query_args);

    struct curl_slist *header_chunk = NULL;
    if (contentType != nullptr && contentType[0] != 0) {
        pg::String aux("Content-Type: ");
        aux.append(contentType);
        header_chunk = curl_slist_append(header_chunk, aux.buf_);
    }  
    for (int i=0; i<(int)request.headers.headers.size(); i++) {
        pg::String header(request.headers.headers[i].key);
        if (request.headers.headers[i].key.length() > 0) header.append(": ");
        header.append(request.headers.headers[i].value);
        header_chunk = curl_slist_append(header_chunk, header.buf_);
    }
    if (header_chunk) {
        res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_chunk);
        if (res != CURLE_OK) {
            thread_result = "Problem setting header!";
            thread_status = FINISHED;
            curl_easy_cleanup(curl);
            return;
        }
    }

    addAuthentication(curl, auth);

    curl_easy_setopt(curl, CURLOPT_URL, request.url.buf_);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)&headerChunk);

    res = curl_easy_perform(curl);
    pg::String response_body;
    long resp_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp_code);
    response_code = (int)resp_code;
    if(res != CURLE_OK) {
        thread_result = pg::String(curl_easy_strerror(res));
        if(chunk.size > 0) thread_result = pg::String(chunk.memory); 
    } else {
        thread_result = pg::String("All ok");
        if(chunk.size > 0) thread_result = prettify(chunk.memory); 
    }

    HttpHeaderParser parser(response_headers);
    parser.parse(&headerChunk);
    
    thread_status = FINISHED;
    curl_easy_cleanup(curl);
}



void threadRequestPostPatchPut(std::atomic<ThreadStatus>& thread_status, Request request, Auth auth,
    pg::String& thread_result, pg::Vector<HeaderKeyValue>& response_headers, int& response_code) 
{ 
    CURL *curl;
    CURLcode res;
    MemoryStruct chunk;
    MemoryStruct headerChunk;

    const char* contentType = HeaderKeyValueCollection::findHeaderValue(request.headers.headers, "Content-Type");
    if (contentType == nullptr || contentType[0] == 0)
    {
        switch(request.body_type)
        {
            case BodyType::MULTIPART_FORMDATA: contentType = "multipart/form-data"; break;
            case BodyType::URL_ENCODED: contentType = "application/x-www-form-urlencoded"; break;
            case BodyType::FILE: contentType = "application/octet-stream"; break;
            default: contentType = "application/json"; break;
        }
    }

    if (request.form_args.size() == 0 && request.input_json.length() == 0) {
        thread_result = "No argument passed for POST";
        thread_status = FINISHED;
        return;
    }

    struct WriteThis wt = {};
    if (request.body_type == BodyType::RAW) {
        wt.readptr = request.input_json.buf_;
        wt.sizeleft = request.input_json.length();
    } 

    /* In windows, this will init the winsock stuff */ 
    res = curl_global_init(CURL_GLOBAL_DEFAULT);
    /* Check for errors */ 
    if(res != CURLE_OK) {
        thread_result = curl_easy_strerror(res);
        thread_status = FINISHED;
        return;
    }

    curl_mime *form = NULL;
    curl_mimepart *field = NULL;

    /* get a curl handle */ 
    curl = curl_easy_init();
    if(curl) {

        pg::String formBuffer;

        if (request.body_type == BodyType::MULTIPART_FORMDATA) {
            for (int i = 0; i < request.form_args.size(); i++) {
                if (form == NULL) {
                    /* Create the form */
                    form = curl_mime_init(curl);
                }

                /* Fill in the file upload field */
                field = curl_mime_addpart(form);
                curl_mime_name(field, request.form_args[i].name.buf_);
                if (request.form_args[i].arg_type == 1)
                    curl_mime_filedata(field, request.form_args[i].value.buf_);
                else
                    curl_mime_data(field, request.form_args[i].value.buf_, CURL_ZERO_TERMINATED);
            }
        }
        else if (request.body_type == BodyType::RAW || request.body_type == BodyType::URL_ENCODED) {

            if (request.body_type == BodyType::URL_ENCODED) {
                for (int i = 0; i < request.form_args.Size; i++) {
                    if (i > 0) {
                        formBuffer.append("&");
                    }
                    formBuffer.append(request.form_args[i].name.buf_);
                    formBuffer.append("=");
                    formBuffer.append(request.form_args[i].value.buf_); // TODO: url encode
                }

                wt.readptr = formBuffer.buf_;
                wt.sizeleft = formBuffer.length();
            }
            else {
                wt.readptr = request.input_json.buf_;
                wt.sizeleft = request.input_json.length();
            }

            curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
            curl_easy_setopt(curl, CURLOPT_READDATA, &wt);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)wt.sizeleft);
        }

        request.url = buildUrl(request.url.buf_, request.query_args);

        struct curl_slist *header_chunk = NULL;
        if (contentType != nullptr && contentType[0] != 0) {
            pg::String aux("Content-Type: ");
            aux.append(contentType);
            header_chunk = curl_slist_append(header_chunk, aux.buf_);
        }
        for (int i=0; i<(int)request.headers.headers.size(); i++) {
            pg::String header(request.headers.headers[i].key);
            if (request.headers.headers[i].key.length() > 0) header.append(": ");
            header.append(request.headers.headers[i].value);
            header_chunk = curl_slist_append(header_chunk, header.buf_);
        }
        if (header_chunk) {
            res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_chunk);
            if (res != CURLE_OK) {
                thread_result = "Problem setting header!";
                thread_status = FINISHED;
                curl_easy_cleanup(curl);
                return;
            }
        }
        curl_easy_setopt(curl, CURLOPT_URL, request.url.buf_);

        addAuthentication(curl, auth);

        if (request.req_type == RequestType::POST) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        } else if (request.req_type == RequestType::PATCH) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        } else {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        }
        
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)&headerChunk);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        if (form != NULL) {
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
        }
        

        res = curl_easy_perform(curl);
        long resp_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp_code);
        response_code = (int)resp_code;

        if(res != CURLE_OK) {
            thread_result = pg::String(curl_easy_strerror(res));
            if(chunk.size > 0) thread_result = prettify(chunk.memory); 
        } else {
            thread_result = pg::String("All ok");
            if(chunk.size > 0) thread_result = prettify(chunk.memory); 
        }
        /* always cleanup */ 
        curl_easy_cleanup(curl);
        if (form != NULL) curl_mime_free(form);

        HttpHeaderParser parser(response_headers);
        parser.parse(&headerChunk);
    }
    thread_status = FINISHED;
}



const char* RequestTypeToString(RequestType req) {

    if ((int)req >= 0 && (int)req < (int)RequestType::_COUNT)
        return requestTypeStrings[(int)req];

    return "UNDEFINED";
}

const char* BodyTypeToString(BodyType ct) {
    if ((int)ct >= 0 && (int)ct < (int)BodyType::_COUNT)
        return bodyTypeStrings[(int)ct];

    return "<NONE>";
}


pg::String prettify(pg::String input) {
    rapidjson::Document document;
    if (document.Parse(input.buf_).HasParseError()) {
        return input;
    }
    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
    document.Accept(writer);    // Accept() traverses the DOM and generates Handler events.
    return pg::String(sb.GetString());
}


