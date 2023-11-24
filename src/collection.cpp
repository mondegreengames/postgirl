#include "collection.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"

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
            result.Data = request;
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

            result.Data = itemList;
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

    result.Root.clear();
    const auto& items = document["item"];
    for (rapidjson::SizeType j = 0; j < items.Size(); j++)
    {
        struct Item item;
        if (parseItem(items[j], item))
            result.Root.push_back(item);
    }

    fclose(fp);
    return true;
}