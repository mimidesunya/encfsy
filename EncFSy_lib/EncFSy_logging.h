#pragma once
#include <windows.h>

/**
 * @brief Unconditional formatted print (always outputs regardless of debug mode)
 * @param format Wide-character format string (printf-style)
 * @param ... Variable arguments
 * 
 * Outputs to stderr with "EncFSy " prefix or to debug console.
 */
void MyPrint(LPCWSTR format, ...);

/**
 * @brief Debug print (only outputs if g_DebugMode is enabled)
 * @param format Wide-character format string (printf-style)
 * @param ... Variable arguments
 * 
 * Controlled by g_efo.g_DebugMode flag.
 */
void DbgPrint(LPCWSTR format, ...);

/**
 * @brief Error print (delegates to DbgPrint, respects debug mode)
 * @param format Wide-character format string (printf-style)
 * @param ... Variable arguments
 * 
 * Note: Despite the name, this only outputs when debug mode is enabled.
 */
void ErrorPrint(LPCWSTR format, ...);
