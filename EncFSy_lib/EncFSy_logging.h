#pragma once
#include <windows.h>

/**
 * @brief Unconditional formatted print (always outputs regardless of debug mode)
 */
void MyPrint(LPCWSTR format, ...);

/**
 * @brief Debug print implementation (only outputs if g_DebugMode is enabled)
 * @note Use DbgPrint macro instead of calling this directly
 */
void DbgPrintImpl(LPCWSTR format, ...);

/**
 * @brief Error print (always outputs regardless of debug mode)
 */
void ErrorPrint(LPCWSTR format, ...);

/**
 * @brief Informational print (always outputs for important lifecycle events)
 */
void InfoPrint(LPCWSTR format, ...);

// Forward declaration for inline check
extern struct EncFSOptions g_efo;

/**
 * @brief Debug print macro - runtime controlled
 * 
 * When g_DebugMode is false, the condition short-circuits and arguments
 * are not evaluated. Available in both Debug and Release builds.
 * 
 * Usage: DbgPrint(L"Message: %s, value=%d\n", str, val);
 */
#define DbgPrint(...) \
    do { \
        if (g_efo.g_DebugMode) { \
            DbgPrintImpl(__VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Verbose/Trace debug print - compile-time controlled
 * 
 * For high-frequency detailed logging (e.g., every read/write block).
 * Completely eliminated in Release builds for zero overhead.
 * 
 * Usage: DbgPrintV(L"Block %d read\n", blockNum);
 */
#ifdef _DEBUG
#define DbgPrintV(...) \
    do { \
        if (g_efo.g_DebugMode) { \
            DbgPrintImpl(__VA_ARGS__); \
        } \
    } while(0)
#else
#define DbgPrintV(...) ((void)0)
#endif
