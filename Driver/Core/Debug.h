/*
Copyright 2014 Cloudbase Solutions Srl

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http ://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once

#define LOG_LOUD             8
#define LOG_INFO             6
#define LOG_TRACE            5
#define LOG_WARN             4
#define LOG_ERROR            2

#if DBG

extern ULONG g_debugLevel;

#define DEBUGP(lev, str, ...)          \
{                                      \
    if ((lev) <= g_debugLevel)         \
    {                                  \
         DbgPrint(str, __VA_ARGS__);   \
    }                                  \
}

#else

#define DEBUGP(lev, stmt, ...)

#endif

#define OVS_DBGPRINT_OID         0
#define OVS_DBGPRINT_ARG         0
#define OVS_DBGPRINT_FRAMES      0
#define OVS_DBGPRINT_FLOW        0
#define OVS_DBGPRINT_FILE        0
#define OVS_DBGPRINT_LOCK        0

#define OVS_VERIFY_WINL_MESSAGES    1
#define OVS_USE_ASSERTS             0

#ifdef DBG

#define OVS_USE_REFCOUNT_CALL_STACK    1

#undef OVS_USE_ASSERTS
#define OVS_USE_ASSERTS                1

#else
#define OVS_USE_REFCOUNT_CALL_STACK    0

//we won't verify WINL messages on release mode
#undef OVS_VERIFY_WINL_MESSAGES
#define OVS_VERIFY_WINL_MESSAGES       0
#endif //DBG

//OVS_USE_ASSERTS is not #define-d on release mode
#if OVS_USE_ASSERTS
#define OVS_CHECK(x) ASSERT(x)
#define OVS_CHECK_OR(x, expr) { if (!(x)) { ASSERT(0); expr; } }
#define OVS_CHECK_BREAK(x) { if (!(x)) { ASSERT(0); break; } }
#define OVS_CHECK_RET(x, value) { if (!(x)) { ASSERT(0); return value; } }
#define OVS_CHECK_GC(x) { if (!(x)) { ASSERT(0); goto Cleanup; } }
#else
#define OVS_CHECK(x)
#define OVS_CHECK_OR(x, expr) { if (!(x)) expr; }
#define OVS_CHECK_BREAK(x) { if (!(x)) break; }
#define OVS_CHECK_RET(x, value) { if (!(x)) return value; }
#define OVS_CHECK_GC(x) { if (!(x)) goto Cleanup; }
#endif //OVS_USE_ASSERTS

//on failure, return FALSE
#define EXPECT(expr)    OVS_CHECK_RET(expr, FALSE)

#define CHECK_E(expr)                           \
{                                               \
    error = (expr);                             \
    if (error != OVS_ERROR_NOERROR)             \
    {                                           \
    OVS_CHECK(__UNEXPECTED__);                  \
    goto Cleanup;                               \
    }                                           \
}

//check boolean expr, on failure, set ok = FALSE and goto Cleanup
#define CHECK_B(expr)                           \
{                                               \
    if (!(expr))                                \
    {                                           \
    ok = FALSE;                                 \
    OVS_CHECK(__UNEXPECTED__);                  \
    goto Cleanup;                               \
    }                                           \
}

//check boolean expr, and use OVS_ERROR
#define CHECK_B_E(expr, errorVal)               \
{                                               \
    if (!(expr))                                \
    {                                           \
    error = errorVal;                           \
    OVS_CHECK(__UNEXPECTED__);                  \
    goto Cleanup;                               \
    }                                           \
}

#pragma warning( disable: 4127)

#if OVS_DBGPRINT_OID
#define DEBUGP_OID(level, str, ...) DEBUGP(level, str, __VA_ARGS__)
#else
#define DEBUGP_OID(level, str, ...) {if (level <= LOG_WARN) DEBUGP(level, str, __VA_ARGS__);}
#endif

#if OVS_DBGPRINT_ARG
#define DEBUGP_ARG(level, str, ...) DEBUGP(level, str, __VA_ARGS__)
#else
#define DEBUGP_ARG(level, str, ...) {if (level <= LOG_WARN) DEBUGP(level, str, __VA_ARGS__);}
#endif

#if OVS_DBGPRINT_FLOW
#define DEBUGP_FLOW(level, str, ...) DEBUGP(level, str, __VA_ARGS__)
#else
#define DEBUGP_FLOW(level, str, ...) {if (level <= LOG_WARN) DEBUGP(level, str, __VA_ARGS__);}
#endif

#if OVS_DBGPRINT_FRAMES
#define DEBUGP_FRAMES(level, str, ...) DEBUGP(level, str, __VA_ARGS__)
#else
#define DEBUGP_FRAMES(level, str, ...) {if (level <= LOG_WARN) DEBUGP(level, str, __VA_ARGS__);}
#endif

#if OVS_DBGPRINT_FILE
#define DEBUGP_FILE(level, str, ...) DEBUGP(level, str, __VA_ARGS__)
#else
#define DEBUGP_FILE(level, str, ...) {if (level <= LOG_WARN) DEBUGP(level, str, __VA_ARGS__);}
#endif

#if OVS_DBGPRINT_LOCK
#define DEBUGP_LOCK(level, str, ...) DEBUGP(level, str, __VA_ARGS__)
#else
#define DEBUGP_LOCK(level, str, ...) {if (level <= LOG_WARN) DEBUGP(level, str, __VA_ARGS__);}
#endif

#define OVS_IPV4_U32_TO_4_BYTES(ip)     \
    RtlUlongByteSwap(ip) >> 24,            \
    (RtlUlongByteSwap(ip) >> 16) & 0xff,\
    (RtlUlongByteSwap(ip) >> 8) & 0xff, \
    RtlUlongByteSwap(ip) & 0xff