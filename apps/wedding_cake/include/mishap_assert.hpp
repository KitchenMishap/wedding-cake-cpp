#pragma once

#include <iostream>

#if defined(_MSC_VER)
    #define MISHAP_BREAK() __debugbreak()
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    // Emits an inline NOP/soft-break marker that GDB/CLion can step over safely
    #define MISHAP_BREAK() asm volatile("int3")
#else
    #define MISHAP_BREAK() ((void)0)
#endif

#ifndef NDEBUG
    #define MISHAP_ASSERT(expr, msg) \
        do { \
            if (!(expr)) { \
                std::cerr << "\n[MISHAP_ASSERT FAILED] " << msg << "\n" \
                          << "  Location: " << __FILE__ << ":" << __LINE__ << "\n"; \
                MISHAP_BREAK(); \
            } \
        } while (false)
#else
    #define MISHAP_ASSERT(expr, msg) ((void)0)
#endif