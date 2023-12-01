#include "collection.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"

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
        }
        else if (body.HasMember("urlencoded"))
        {
            result.body_type = BodyType::URL_ENCODED;
            // TODO: load the parameters
        }
        else if (body.HasMember("formdata"))
        {
            result.body_type = BodyType::MULTIPART_FORMDATA;
            // TODO: load the parameters
        }
        else if (body.HasMember("file"))
        {
            result.body_type = BodyType::FILE;
            // TODO: load the parameters
        }
    }

    if (request.HasMember("auth") && request["auth"].IsObject())
    {
        const auto& auth = request["auth"];

        parseAuth(auth, result.auth);
    }

    // TODO: the rest

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
            
        Request request;
        if (parseRequest(requestJson, request))
        {
            result.name = name == nullptr ? pg::String("") : pg::String(name);
            result.data = request;
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
            parseAuth(auth, result.auth);
        }

        return true;
    }

    return false;
}

bool Collection::Load(const char* filename, Collection& result)
{
    FILE *fp = fopen(filename, "rb");
    if (fp == nullptr) return false;

    char readBuffer[1024 * 64];
    rapidjson::FileReadStream stream(fp, readBuffer, sizeof(readBuffer));

    rapidjson::Document document;
    if (document.ParseStream(stream).HasParseError())
    {
        fclose(fp);
        return false;
    }

    if (document.HasMember("item") == false)
    {
        fclose(fp);
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

    if (document.HasMember("info") && document["info"].IsObject())
    {
        const auto& info = document["info"];
        if (info.HasMember("name") && info["name"].IsString())
            result.name = info["name"].GetString();
    }

    fclose(fp);
    return true;
}
