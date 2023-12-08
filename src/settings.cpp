#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"

#include "settings.h"
#include <cstdio>

bool Settings::Save(const char* filename, const Settings& settings)
{
    rapidjson::Document document;
	document.SetObject();
	rapidjson::Value collection_array(rapidjson::kArrayType);
 
	// must pass an allocator when the object may need to allocate memory
	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    rapidjson::Value root(rapidjson::kObjectType);

    root.AddMember("saveCollectionsPretty", settings.PrettifyCollectionsJson, allocator);

    rapidjson::Value themeValue;
    themeValue.SetString(Settings::ThemeTypeToString(settings.Theme), allocator);
    root.AddMember("theme", themeValue, allocator);

    if (settings.Font.buf_[0] != 0)
    {
        rapidjson::Value fontValue;
        fontValue.SetString(settings.Font.buf_, allocator);
        root.AddMember("font", fontValue, allocator);
    }

    if (settings.FontSize != 0)
    {
        root.AddMember("fontSize", settings.FontSize, allocator);
    }

    document.AddMember("settings", root, allocator);

    // add collections
    rapidjson::Value collections(rapidjson::kArrayType);
    for (auto itr = settings.CollectionList.begin(); itr != settings.CollectionList.end(); ++itr) {
        rapidjson::Value value;
        value.SetString(itr->buf_, allocator);
        collections.PushBack(value, allocator);
    }
    document.AddMember("collections", collections, allocator);

    rapidjson::StringBuffer strbuf;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);

    FILE *fp = fopen(filename, "wb");
    if (fp != NULL)
    {
        fputs(strbuf.GetString(), fp);
        fclose(fp);
    
        return true;
    }

    return false;
}

bool Settings::Load(const char* filename, Settings& settings)
{
    FILE *fp = fopen(filename, "rb");
    if (fp == nullptr) return false;

    static char readBuffer[1024 * 64];
    rapidjson::FileReadStream stream(fp, readBuffer, sizeof(readBuffer));

    rapidjson::Document document;
    if (document.ParseStream(stream).HasParseError())
    {
        fclose(fp);
        return false;
    }

    if (document.HasMember("settings") == false)
    {
        fclose(fp);
        return false;
    }
    const auto& settingsNode = document["settings"];
    if (settingsNode.HasMember("saveCollectionsPretty"))
    {
        const auto& saveCollectionsPretty = settingsNode["saveCollectionsPretty"];
        if (saveCollectionsPretty.IsBool())
            settings.PrettifyCollectionsJson = saveCollectionsPretty.GetBool();
    }

    if (settingsNode.HasMember("theme"))
    {
        const auto& theme = settingsNode["theme"];
        if (theme.IsString())
        {
            ThemeType themeSetting;
            if (Settings::TryParseThemeType(theme.GetString(), &themeSetting))
                settings.Theme = themeSetting;
        }
    }

    if (settingsNode.HasMember("font"))
    {
        const auto& font = settingsNode["font"];
        if (font.IsString())
        {
            settings.Font = pg::String(font.GetString());
        }
    }

    if (settingsNode.HasMember("fontSize"))
    {
        const auto& fontSize = settingsNode["fontSize"];
        if (fontSize.IsNumber())
        {
            settings.FontSize = fontSize.GetFloat();
        }
    }

    // load collections
    if (document.HasMember("collections")) {
        const auto& collections = document["collections"];

        if (collections.IsArray()) {
            for (unsigned int i = 0; i < collections.Size(); i++) {
                const auto& collection = collections[i];
                if (collection.IsString()) {
                    settings.CollectionList.push_back(collection.GetString());
                }
            }
        }
    }

    fclose(fp);
    return true;
}
