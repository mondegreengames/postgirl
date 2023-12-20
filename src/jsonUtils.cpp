#include "jsonUtils.h"

namespace json {
    bool tryGetInt(const rapidjson::Value& value, const char* name, int* result)
    {
        if (value.HasMember(name))
        {
            const auto& ivalue = value[name];
            if (ivalue.IsInt())
            {
                if (result != nullptr) *result = ivalue.GetInt();
                return true;
            }
        }

        return false;
    }

    bool tryGetString(const rapidjson::Value& value, const char* name, pg::String& result)
    {
        if (value.HasMember(name))
        {
            const auto& svalue = value[name];
            if (svalue.IsString())
            {
                result.set(svalue.GetString());
                return true;
            }
        }

        return false;
    }

    bool tryGetString(const rapidjson::Value& value, const char* name, const char** result)
    {
        if (value.HasMember(name))
        {
            const auto& svalue = value[name];
            if (svalue.IsString())
            {
                if (result != nullptr) {
                    *result = svalue.GetString();
                }
                return true;
            }
        }

        return false;
    }

    void addStringProperty(rapidjson::Value& parent, const char* name, const char* value, rapidjson::Document::AllocatorType& allocator) {
        rapidjson::Value str;
        str.SetString(value, allocator);

        rapidjson::Value namestr;
        namestr.SetString(name, allocator);

        parent.AddMember(namestr, str, allocator);
    }

    void addStringProperty(rapidjson::Value& parent, const char* name, std::string_view value, rapidjson::Document::AllocatorType& allocator) {
        rapidjson::Value str;
        str.SetString(value.data(), value.length(), allocator);

        rapidjson::Value namestr;
        namestr.SetString(name, allocator);

        parent.AddMember(namestr, str, allocator);
    }

    void addStringElement(rapidjson::Value& parent, const char* value, rapidjson::Document::AllocatorType& allocator) {
        rapidjson::Value str;
        str.SetString(value, allocator);

        parent.PushBack(str, allocator);
    }

    void addStringElement(rapidjson::Value& parent, std::string_view value, rapidjson::Document::AllocatorType& allocator) {
        rapidjson::Value str;
        str.SetString(value.data(), value.length(), allocator);

        parent.PushBack(str, allocator);
    }
}