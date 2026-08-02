#pragma once

#if defined(_WIN32)
  #if defined(LODE_CORE_BUILD)
    #define LODE_API __declspec(dllexport)
  #else
    #define LODE_API __declspec(dllimport)
  #endif
  #define LODE_EXPORT extern "C" __declspec(dllexport)
#else
  #define LODE_API __attribute__((visibility("default")))
  #define LODE_EXPORT extern "C" __attribute__((visibility("default")))
#endif
