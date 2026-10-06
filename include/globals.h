#pragma once
#include <ctime>

#define VERSION 0x0002
#define VERSION_STRING "0.0.2"
#define APP_NAME "API-cli"

#define CONFIG_PATH "config/server.lua"
#define MODEL_DIRECTORY "scripts/models"
#define MIGRATION_DIRECTORY "scripts/migrations"

inline struct tm *__localtime_(const time_t *timer, struct tm *buffer)
{
#if defined(_WIN32) || defined(_WIN64)
    if (localtime_s(buffer, timer) == 0)
    {
        return buffer;
    }

    return nullptr;
#else
    return localtime_r(timer, buffer);
#endif
}

