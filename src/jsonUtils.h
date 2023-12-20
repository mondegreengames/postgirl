#pragma once
#include <string_view>
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "pgstring.h"

namespace json {
    bool tryGetInt(const rapidjson::Value& value, const char* name, int* result);

    bool tryGetString(const rapidjson::Value& value, const char* name, pg::String& result);

    bool tryGetString(const rapidjson::Value& value, const char* name, const char** result);

    template<typename T>
    bool tryGetStringEnum(const rapidjson::Value& value, const char* name, const char* values[], int numValues, T* result)
    {
        if (value.HasMember(name))
        {
            const auto& svalue = value[name];
            if (svalue.IsString())
            {
                auto str = svalue.GetString();
                for (int i = 0; i < numValues; i++)
                {
                    if (strcmp(values[i], str) == 0)
                    {
                        if (result != nullptr) *result = (T)i;
                        return true;
                    }
                }
            }
        }

        return false;
    }


    void addStringProperty(rapidjson::Value& parent, const char* name, const char* value, rapidjson::Document::AllocatorType& allocator);
    void addStringProperty(rapidjson::Value& parent, const char* name, std::string_view value, rapidjson::Document::AllocatorType& allocator);
    void addStringElement(rapidjson::Value& parent, const char* value, rapidjson::Document::AllocatorType& allocator);
    void addStringElement(rapidjson::Value& parent, std::string_view value, rapidjson::Document::AllocatorType& allocator);
}