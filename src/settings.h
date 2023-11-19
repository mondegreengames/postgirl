#pragma once
#include "pgstring.h"
#include <cstring>

#define DEFINE_THEMES \
DEFINE_THEME(DARK, "dark", 0) \
DEFINE_THEME(LIGHT, "light", 1) \
DEFINE_THEME(CLASSIC, "classic", 2)

#define DEFINE_THEME(name, str, value) name = value,
enum class ThemeType
{
    DEFINE_THEMES
};
#undef DEFINE_THEME

struct Settings
{
    bool PrettifyCollectionsJson;
    ThemeType Theme;
    pg::String Font;
    float FontSize;

    Settings()
    {
        PrettifyCollectionsJson = true;
        Theme = ThemeType::DARK;
        FontSize = 0;
    }

    // Converts a theme to a string. If `theme` is not actually a valid theme then this returns null.
    static const char* ThemeTypeToString(ThemeType theme)
    {
        #define DEFINE_THEME(name, str, value) case ThemeType::name: return str;
        switch(theme)
        {
            DEFINE_THEMES
        }
        #undef DEFINE_THEME

        return nullptr;
    }

    // Attempts to convert a string into a `ThemeType`. Returns `true` if successful,
    // `false` otherwise. If this returns `false` then the value of `result` is unchanged. 
    static bool TryParseThemeType(const char* theme, ThemeType* result)
    {
        if (theme == nullptr) return false;

        #define DEFINE_THEME(name, str, value) if (strcmp(theme, str) == 0) { if (result != nullptr) *result = ThemeType::name; return true; }

        DEFINE_THEMES

        #undef DEFINE_THEME

        return false;
    }

    static bool Save(const char* filename, const Settings& settings);
    static bool Load(const char* filename, Settings& settings);
};

#undef DEFINE_THEMES