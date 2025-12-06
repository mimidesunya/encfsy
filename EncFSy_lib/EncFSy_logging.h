#pragma once
#include <windows.h>

/**
 * @brief Unconditional formatted print (always outputs regardless of debug mode)
 */
void MyPrint(LPCWSTR format, ...);

/**
 * @brief Debug print (only outputs if g_DebugMode is enabled)
 */
void DbgPrint(LPCWSTR format, ...);

/**
 * @brief Error print (always outputs regardless of debug mode)
 */
void ErrorPrint(LPCWSTR format, ...);

/**
 * @brief Informational print (always outputs for important lifecycle events)
 */
void InfoPrint(LPCWSTR format, ...);
