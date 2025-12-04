#include "EncFSy_logging.h"
#include "EncFSy_globals.h"
#include <malloc.h>
#include <stdio.h>

/**
 * @brief Internal formatted print function (outputs to stderr or debug console)
 * @param format Wide-character format string (printf-style)
 * @param argp Variable argument list
 * 
 * Outputs to stderr with "EncFSy " prefix if g_UseStdErr is enabled,
 * otherwise outputs to debug console via OutputDebugStringW.
 */
static void PrintF(LPCWSTR format, va_list argp) {
	const WCHAR* outputString;
	WCHAR* buffer = NULL;
	size_t length;
	
	// Calculate required buffer size and format the string
	length = _vscwprintf(format, argp) + 1;
	buffer = (WCHAR*)_malloca(length * sizeof(WCHAR));
	if (buffer) {
		vswprintf_s(buffer, length, format, argp);
		outputString = buffer;
	}
	else {
		// Fallback to unformatted string if allocation fails
		outputString = format;
	}
	
	// Output to stderr or debug console
	if (g_efo.g_UseStdErr) {
		fputws(L"EncFSy ", stderr);
		fputws(outputString, stderr);
	}
	else {
		OutputDebugStringW(outputString);
	}
	
	// Clean up
	if (buffer)
		_freea(buffer);
	if (g_efo.g_UseStdErr)
		fflush(stderr);
}

/**
 * @brief Unconditional formatted print (always outputs)
 * @param format Wide-character format string (printf-style)
 * @param ... Variable arguments
 */
void MyPrint(LPCWSTR format, ...) {
	va_list argp;
	va_start(argp, format);
	PrintF(format, argp);
	va_end(argp);
}

/**
 * @brief Debug print (only outputs if debug mode is enabled)
 * @param format Wide-character format string (printf-style)
 * @param ... Variable arguments
 * 
 * Checks g_DebugMode flag before outputting.
 */
void DbgPrint(LPCWSTR format, ...) {
	if (!g_efo.g_DebugMode) {
		return;
	}
	va_list argp;
	va_start(argp, format);
	PrintF(format, argp);
	va_end(argp);
}

/**
 * @brief Error print (delegates to DbgPrint, respects debug mode)
 * @param format Wide-character format string (printf-style)
 * @param ... Variable arguments
 * 
 * Note: Despite the name, this uses DbgPrint internally,
 * so it only outputs when debug mode is enabled.
 */
void ErrorPrint(LPCWSTR format, ...) {
	va_list argp;
	va_start(argp, format);
	DbgPrint(format, argp);
	va_end(argp);
}
