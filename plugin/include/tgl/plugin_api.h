// Plugin interface of The Grand Loader (https://github.com/The-Grand-Combination/The-Grand-Loader), API version 1.
// Written for this project from the loader's documented ABI; only the layout below matters to the loader.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TGL_PLUGIN_API_VERSION 1

typedef struct TGL_HostApi {
    uint32_t api_version;        // TGL_PLUGIN_API_VERSION
    uint32_t struct_size;        // sizeof(TGL_HostApi) as built into the loader
    const char* plugin_name;     // stem of mod\<name>.ini
    const wchar_t* game_dir;     // folder of v2game.exe (no trailing backslash)
    const wchar_t* config_file;  // full path of mod\<name>.ini
    const wchar_t* plugin_dir;   // full path of mod\<name>
    void* exe_base;              // load address of v2game.exe
    size_t exe_size;             // SizeOfImage of v2game.exe
    void (*log)(const struct TGL_HostApi* host, const char* message);  // writes one line to the_grand_loader.log
} TGL_HostApi;

// Every plugin exports `int TGL_PluginInit(const TGL_HostApi*)`; 0 = success.
// It runs inside the loader's DllMain: patching memory is fine, starting threads or waiting is not.
typedef int (*TGL_PluginInitFn)(const TGL_HostApi* host);

#ifdef __cplusplus
}
#endif
