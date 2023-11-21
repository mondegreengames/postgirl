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

    document.AddMember("settings", root, allocator);


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

    char readBuffer[1024 * 64];
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

    fclose(fp);
    return true;
}