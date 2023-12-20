#include "collection.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"
#include "utils.h"
#include "jsonUtils.h"

const char* variableTypeStrings[] = {
    "string",
    "boolean",
    "any",
    "number",
};

const char* variableTypeUIStrings[] = {
    "String",
    "Boolean",
    "Any",
    "Number",
};

bool parseAuth(const rapidjson::Value& auth, Auth& result)
{
    if (auth.HasMember("type") && auth["type"].IsString())
    {
        auto type = auth["type"].GetString();
        for (int i = 0; i < (int)AuthType::_COUNT; i++)
        {
            if (strcmp(type, authTypeStrings[i]) == 0)
            {
                result.type = (AuthType)i;

                if (auth.HasMember(authTypeStrings[i]))
                {
                    const auto& authData = auth[authTypeStrings[i]];
                    if (authData.IsArray())
                    {
                        // v2.1 collection format
                        for (rapidjson::SizeType i = 0; i < authData.Size(); i++)
                        {
                            if (authData[i].IsObject() && authData[i].HasMember("key") && authData[i]["key"].IsString() && authData[i].HasMember("value"))
                            {
                                const auto& key = authData[i]["key"];
                                const auto& value = authData[i]["value"];
                                
                                AuthAttribute attrib;
                                attrib.key = key.GetString();

                                if (value.IsString())
                                    attrib.value = value.GetString();
                                else {
                                    if (authData[i].HasMember("type") && authData[i]["type"].IsString() && strcmp(authData[i]["type"].GetString(), "any") == 0) {
                                        if (value.IsObject()) {
                                            for (auto itr = value.MemberBegin(); itr != value.MemberEnd(); ++itr) {
                                                // TODO: store all the values, not this "last one wins" stuff
                                                if ((*itr).value.IsString()) {
                                                    attrib.value = (*itr).value.GetString();
                                                }
                                            }
                                        }
                                    }
                                }
                                // TODO: are there other types we need to support, like booleans?

                                result.attributes.push_back(attrib);
                            }
                        }
                    }
                    else if (authData.IsObject())
                    {
                        // TODO: v2.0 collection format
                    }
                }

                return true;
            }
        }
    }

    return false;
}

bool parseRequest(const rapidjson::Value& request, Request& result)
{
    if (request.IsObject() == false) return false;

    result.req_type = RequestType::GET;

    if (request.HasMember("method") && request["method"].IsString())
    {
        const char* method = request["method"].GetString();
        if (strcmp(method, "GET") == 0)
            result.req_type = RequestType::GET;
        else if (strcmp(method, "POST") == 0)
            result.req_type = RequestType::POST;
        else if (strcmp(method, "PUT") == 0)
            result.req_type = RequestType::PUT;
        else if (strcmp(method, "PATCH") == 0)
            result.req_type = RequestType::PATCH;
    }

    if (request.HasMember("url") && request["url"].IsObject())
    {
        const auto& url = request["url"];

        if (url.HasMember("raw") && url["raw"].IsString())
            result.url = pg::String(url["raw"].GetString());
    }

    if (request.HasMember("header") && request["header"].IsArray())
    {
        const auto& header = request["header"];
        for (rapidjson::SizeType i = 0; i < header.Size(); i++)
        {
            if (header[i].IsObject())
            {
                const auto& keyvalue = header[i];

                if (keyvalue.HasMember("key") && keyvalue.HasMember("value"))
                {
                    const auto& key = keyvalue["key"];
                    const auto& value = keyvalue["value"];

                    if (key.IsString() && value.IsString())
                    {
                        result.headers.headers.push_back(HeaderKeyValue { .key = key.GetString(), .value = value.GetString() });
                    }
                }
            }
        }
    }

    if (request.HasMember("body") && request["body"].IsObject())
    {
        const auto& body = request["body"];
        
        if (body.HasMember("raw"))
        {
            result.body_type = BodyType::RAW;
            if (body["raw"].IsString())
            {
                result.input_json.set(body["raw"].GetString());
            }

            if (body.HasMember("options")) {
                auto& options = body["options"];
                if (options.IsObject() && options.HasMember("raw")) {
                    auto& raw = options["raw"];
                    if (raw.IsObject()) {
                        const char* rawBodyType = nullptr;
                        if (json::tryGetString(raw, "language", &rawBodyType) && rawBodyType != nullptr) {
                            if (strcmp(rawBodyType, "json") == 0) {
                                result.raw_body_type = RawBodyType::JSON;
                            }
                            else if (strcmp(rawBodyType, "xml") == 0) {
                                result.raw_body_type = RawBodyType::XML;
                            }
                            else {
                                result.raw_body_type = RawBodyType::TEXT;
                            }
                        }
                    }
                }
            }
        }
        else if (body.HasMember("urlencoded"))
        {
            result.body_type = BodyType::URL_ENCODED;
            // TODO: load the parameters
        }
        else if (body.HasMember("formdata"))
        {
            result.body_type = BodyType::MULTIPART_FORMDATA;
            auto& formdata = body["formdata"];
            if (formdata.IsArray()) {
                for (rapidjson::SizeType i = 0; i < formdata.Size(); i++) {
                    const char* key = nullptr, * value = nullptr, * type = nullptr;;

                    json::tryGetString(formdata[i], "key", &key);
                    json::tryGetString(formdata[i], "type", &type);

                    const bool isFile = type != nullptr && strcmp(type, "file") == 0;

                    if (isFile) {
                        json::tryGetString(formdata[i], "src", &value);
                    }
                    else {
                        json::tryGetString(formdata[i], "value", &value);
                    }

                    if (key != nullptr && value != nullptr) {
                        // TODO: does it matter if `value` is null? Should we add it anyway?
                        result.form_args.push_back(Argument{ .name = key, .value = value, .arg_type = isFile ? 1 : 0 });
                    }
                }
            }
        }
        else if (body.HasMember("file"))
        {
            result.body_type = BodyType::FILE;
            // TODO: load the parameters
        }
    }

    

    // TODO: the rest

    return true;
}

bool parseVariable(const rapidjson::Value& item, Variable& result) {
    if (item.HasMember("key") && item["key"].IsString()) {
        result.key = item["key"].GetString();
    }

    if (item.HasMember("value") && item["value"].IsString()) {
        result.value = item["value"].GetString();
    }

    if (item.HasMember("type") && item["type"].IsString()) {
        const char* type = item["type"].GetString();

        for (int i = 0; i < 4; i++) {
            if (strcmp(variableTypeStrings[i], type) == 0) {
                result.type = (VariableType)i;
                break;
            }
        }
    }

    if (item.HasMember("name") && item["name"].IsString()) {
        result.name = item["name"].GetString();
    }

    if (item.HasMember("system") && item["system"].IsBool()) {
        result.system = item["system"].GetBool();
    }

    if (item.HasMember("disabled") && item["disabled"].IsBool()) {
        result.disabled = item["disabled"].GetBool();
    }

    return true;
}

bool parseItem(const rapidjson::Value& item, Item& result)
{
    const char* name = nullptr;
    if (item.HasMember("name") && item["name"].IsString())
    {
        name = item["name"].GetString();
    }

    if (item.HasMember("request"))
    {
        const auto& requestJson = item["request"];
            
        Request request = {};
        if (parseRequest(requestJson, request))
        {
            result.name = name == nullptr ? pg::String("") : pg::String(name);
            result.data = request;

            if (requestJson.HasMember("auth") && requestJson["auth"].IsObject())
            {
                const auto& auth = requestJson["auth"];
                Auth authObj;
                if (parseAuth(auth, authObj)) {
                    result.auth = authObj;
                }
            }

            return true;
        }
    }
    else if (item.HasMember("item"))
    {
        const auto& itemJson = item["item"];
    
        result.name = name == nullptr ? pg::String("") : pg::String(name);

        if (itemJson.IsArray())
        {
            pg::Vector<Item> itemList;
            for (rapidjson::SizeType i = 0; i < itemJson.Size(); i++)
            {
                struct Item item;
                if (parseItem(itemJson[i], item))
                    itemList.push_back(item);
            }

            result.data = itemList;
        }

        if (item.HasMember("auth") && item["auth"].IsObject())
        {
            const auto& auth = item["auth"];

            Auth authObj;
            parseAuth(auth, authObj);
            result.auth = authObj;
        }

        return true;
    }

    if (item.HasMember("variable") && item["variable"].IsArray()) {
        const auto& variable = item["variable"];
        for (unsigned int i = 0; i < variable.Size(); i++) {
            Variable v;
            if (parseVariable(variable[i], v)) {
                result.variables.push_back(v);
            }
        }
    }

    return false;
}

bool Collection::Load(const char* filename, Collection& result)
{
    FILE* fp = fopen(filename, "rb");
    if (fp == nullptr) return false;

    bool r = Load(fp, result);
    
    fclose(fp);

    return r;
}

bool Collection::Load(FILE* fp, Collection& result)
{
    char readBuffer[1024 * 64];
    rapidjson::FileReadStream stream(fp, readBuffer, sizeof(readBuffer));

    rapidjson::Document document;
    if (document.ParseStream(stream).HasParseError())
    {
        return false;
    }

    if (document.HasMember("item") == false)
    {
        return false;
    }

    result.root.clear();
    const auto& items = document["item"];
    for (rapidjson::SizeType j = 0; j < items.Size(); j++)
    {
        struct Item item;
        if (parseItem(items[j], item))
            result.root.push_back(item);
    }

    if (document.HasMember("auth"))
    {
        parseAuth(document["auth"], result.auth);
    }
    else {
        result.auth.type = AuthType::INHERIT;
    }

    if (document.HasMember("info") && document["info"].IsObject())
    {
        const auto& info = document["info"];
        if (info.HasMember("name") && info["name"].IsString())
            result.name = info["name"].GetString();
    }

    if (document.HasMember("variable") && document["variable"].IsArray()) {
        const auto& variable = document["variable"];
        for (unsigned int i = 0; i < variable.Size(); i++) {
            Variable v;
            if (parseVariable(variable[i], v)) {
                result.variables.push_back(v);
            }
        }
    }

    return true;
}

bool Collection::Save(const char* filename, const Collection& collection, bool pretty)
{
    FILE* fp = fopen(filename, "wb");
    if (fp == nullptr) return false;

    bool r = Save(fp, collection, pretty);

    fclose(fp);

    return r;
}

bool writeAuth(rapidjson::Value& parent, rapidjson::Document::AllocatorType& allocator, const Auth& auth) {
    rapidjson::Value authv(rapidjson::kObjectType);

    json::addStringProperty(authv, "type", authTypeStrings[(int)auth.type], allocator);

    rapidjson::Value attributes(rapidjson::kArrayType);

    // TODO: only include relevant attributes for the given auth type
    for (auto itr = auth.attributes.begin(); itr != auth.attributes.end(); ++itr) {
        rapidjson::Value attribute(rapidjson::kObjectType);

        json::addStringProperty(attribute, "key", itr->key.buf_, allocator);
        json::addStringProperty(attribute, "value", itr->value.buf_, allocator);
        json::addStringProperty(attribute, "type", "string", allocator); // TODO: support other types

        attributes.PushBack(attribute, allocator);
    }

    rapidjson::Value namestr;
    namestr.SetString(authTypeStrings[(int)auth.type], allocator);

    authv.AddMember(namestr, attributes, allocator);
    parent.AddMember("auth", authv, allocator);

    return true;
}

bool writeRequest(rapidjson::Value& parent, rapidjson::Document::AllocatorType& allocator, const Request& request) {
    rapidjson::Value requestV(rapidjson::kObjectType);

    json::addStringProperty(requestV, "method", RequestTypeToString(request.req_type), allocator);

    // write the headers
    rapidjson::Value headerV(rapidjson::kArrayType);
    for (auto itr = request.headers.headers.begin(); itr != request.headers.headers.end(); ++itr) {
        // TODO
    }
    requestV.AddMember("header", headerV, allocator);

    // write the body
    if (request.req_type != RequestType::GET) {
        rapidjson::Value bodyV(rapidjson::kObjectType);
        json::addStringProperty(bodyV, "mode", BodyTypeToString(request.body_type), allocator);
        if (request.body_type == BodyType::RAW) {
            json::addStringProperty(bodyV, "raw", request.input_json.buf_, allocator);
        }
        else {
            // TODO
        }
        
        rapidjson::Value optionsV(rapidjson::kObjectType);
        if (request.body_type == BodyType::MULTIPART_FORMDATA) {
            rapidjson::Value formdataV(rapidjson::kArrayType);

            for (auto itr = request.form_args.begin(); itr != request.form_args.end(); ++itr) {
                rapidjson::Value dataV(rapidjson::kObjectType);

                json::addStringProperty(dataV, "key", itr->name.buf_, allocator);
                
                if (itr->arg_type == 1) {
                    json::addStringProperty(dataV, "type", "file", allocator);
                    json::addStringProperty(dataV, "src", itr->value.buf_, allocator);
                }
                else {
                    json::addStringProperty(dataV, "value", itr->value.buf_, allocator);
                    json::addStringProperty(dataV, "type", "default", allocator);
                }

                formdataV.PushBack(dataV, allocator);
            }

            bodyV.AddMember("formdata", formdataV, allocator);
        }
        else if (request.body_type == BodyType::RAW) {

            if (request.raw_body_type == RawBodyType::JSON) {
                json::addStringProperty(optionsV, "language", "json", allocator);
            }
            else if (request.raw_body_type == RawBodyType::XML) {
                json::addStringProperty(optionsV, "language", "xml", allocator);
            }

            bodyV.AddMember("options", optionsV, allocator);
        }
        else {
            // TODO
            bodyV.AddMember("options", optionsV, allocator);
        }
        requestV.AddMember("body", bodyV, allocator);
    }

    // write the url
    if (request.url.buf_[0] != 0) {
        rapidjson::Value urlV(rapidjson::kObjectType);
        json::addStringProperty(urlV, "raw", request.url.buf_, allocator);

        // parse the url
        UrlParts parts;
        if (ParseUrl(request.url.buf_, UrlPartsFlags::URL_PARTS_FLAG_ALL, parts)) {
            if (parts.scheme != nullptr) {
                json::addStringProperty(urlV, "protocol", parts.scheme, allocator);
            }

            if (parts.host != nullptr) {
                rapidjson::Value hostsV(rapidjson::kArrayType);

                TokenIterator itr;
                TokenIterator::init(parts.host, '.', itr);
                std::string_view hostpart;
                while (itr.next(hostpart)) {
                    json::addStringElement(hostsV, hostpart, allocator);
                }

                urlV.AddMember("host", hostsV, allocator);
            }

            if (parts.path != nullptr) {
                rapidjson::Value pathV(rapidjson::kArrayType);

                TokenIterator itr;
                TokenIterator::init(parts.path, '/', itr);
                std::string_view pathpart;
                while (itr.next(pathpart)) {
                    json::addStringElement(pathV, pathpart, allocator);
                }

                urlV.AddMember("path", pathV, allocator);
            }

            if (parts.query != nullptr) {
                rapidjson::Value queryV(rapidjson::kArrayType);

                TokenIterator itr;
                TokenIterator::init(parts.query, '&', itr);
                std::string_view querypart;
                while (itr.next(querypart)) {
                    rapidjson::Value queryV2(rapidjson::kObjectType);

                    TokenIterator itr2;
                    TokenIterator::init(querypart, '=', itr2);

                    std::string_view key, value;
                    if (itr2.next(key)) {
                        json::addStringProperty(queryV2, "key", key, allocator);

                        if (itr2.next(value)) {
                            json::addStringProperty(queryV2, "value", value, allocator);
                        }
                    }

                    queryV.PushBack(queryV2, allocator);
                }

                urlV.AddMember("query", queryV, allocator);
            }

            FreeUrl(parts);
        }

        requestV.AddMember("url", urlV, allocator);
    }

    parent.AddMember("request", requestV, allocator);

    return true;
}

bool writeItem(rapidjson::Value& parent, rapidjson::Document::AllocatorType& allocator, const Item& item) {
    rapidjson::Value itemElement(rapidjson::kObjectType);

    json::addStringProperty(itemElement, "name", item.name.buf_, allocator);

    if (item.data.index() == 0) {
        // write the child items
        rapidjson::Value childItems(rapidjson::kArrayType);

        for (auto itr = std::get<0>(item.data).begin(); itr != std::get<0>(item.data).end(); ++itr) {
            if (writeItem(childItems, allocator, *itr) == false) {
                return false;
            }
        }

        itemElement.AddMember("item", childItems, allocator);
    }
    else {
        // write the request
        if (writeRequest(itemElement, allocator, std::get<1>(item.data)) == false) {
            return false;
        }
    }

    if (item.auth.type != AuthType::INHERIT) {
        writeAuth(itemElement, allocator, item.auth);
    }

    parent.PushBack(itemElement, allocator);

    return true;
}

bool Collection::Save(FILE* fp, const Collection& collection, bool pretty)
{
    rapidjson::Document document;
    document.SetObject();
    
    // must pass an allocator when the object may need to allocate memory
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    // info
    rapidjson::Value info(rapidjson::kObjectType);
    json::addStringProperty(info, "name", collection.name.buf_, allocator);
    json::addStringProperty(info, "schema", "https://schema.getpostman.com/json/collection/v2.1.0/collection.json", allocator);
    document.AddMember("info", info, allocator);

    // item
    rapidjson::Value item(rapidjson::kArrayType);
    for (auto itr = collection.root.begin(); itr != collection.root.end(); ++itr) {
        writeItem(item, allocator, *itr);
    }
    document.AddMember("item", item, allocator);

    // auth
    if (collection.auth.type != AuthType::NONE && collection.auth.type != AuthType::INHERIT) {
        writeAuth(document, allocator, collection.auth);
    }

    rapidjson::StringBuffer strbuf;
    if (pretty)
    {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
        document.Accept(writer);
    }
    else
    {
        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
        document.Accept(writer);
    }

    fputs(strbuf.GetString(), fp);

    return true;
}