/**
 * @file debug.h
 * @brief Define a <b>simple</b> debugging interface to use in C programs
 * @details One method of debugging your C program is to add <tt>printf()</tt>
 * statements to your code. This file provides a way of including debug
 * output, and being able to turn it on/off either at compile time or
 * at runtime, without making <b>any</b> changes to your code.
 * Two levels of debugging are provided. If the value <tt>DEBUG</tt> is
 * defined, your debug calls are compiled into your code. Otherwise, they are
 * removed by the optimizer. There is an additional run time check as to
 * whether to actually print the debugging output. This is controlled by the
 * value <tt>debugLevel</tt>.
 * <p>
 * To use it with <tt>gcc</tt>, simply write a <tt>printf()</tt> call, but
 * replace the <tt>printf()</tt> call by <tt>debug()</tt>.
 * <p>
 * To easily print the value of a <b>single</b> variable, use
 * <pre><tt>
   vDebug("format", var); // "format" is the specifier (e.g. "%d" or "%s", etc)
 * </tt></pre>
 * <p>
 * To use debug(), but control when it prints, use
 * <pre><tt>
   lDebug(level, "format", var); // print when debugLevel >= level
 * </tt></pre>
 * <p>
 * Based on code and ideas found
 * <a href="http://stackoverflow.com/questions/1644868/c-define-macro-for-debug-printing">
 * here</a> and
 * <a href="http://stackoverflow.com/questions/679979/how-to-make-a-variadic-macro-variable-number-of-arguments">
 * here</a>.
 * <p>
 * @author Fritz Sieker
 */

#pragma once

#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"

inline xSemaphoreHandle s_PrintfSemaphore;

#if defined(NDEBUG)
#define vDebug(fmt, name)
#define debug(fmt, ...)
#define lDebug(level, fmt, ...)
#define debugWrite(data, size)
#define HERE
#define debugPrintf(format, ...)
#else

enum debugLevels {
    Debug,
    Info,
    Warn,
    Error,
};

static inline const char *levelText(enum debugLevels level) {
    const char *ret;
    switch (level) {
    case Debug: ret = "Debug"; break;
    case Info: ret = "Info"; break;
    case Warn: ret = "Warn"; break;
    case Error: ret = "Error"; break;
    default: ret = ""; break;
    }
    return ret;
}

/**
 * controls how much debug output is produced. Higher values produce more
 * output. See the use in <tt>lDebug()</tt>.
 */
inline enum debugLevels debugLevel = Info;

/**
 * The file where debug output is written. Defaults to <tt>stderr</tt>.
 * <tt>debugToFile()</tt> allows output to any file.
 */

void debugSetLevel(enum debugLevels lvl);

/**
 * Expands a name into a string and a value.
 * @param name name of variable
 */
#define debugV(name)      #name, (name)

/**
 * @brief 	outputs the name and value of a single variable.
 * @param 	fmt 	: format to print the var
 * @param 	name 	: name of the variable to print
 */
#define vDebug(fmt, name) debug("%s=(" fmt ")", debugV(name))

/**
 * @brief prints this message if the variable <tt>debugLevel</tt> is greater
 * than or equal to the parameter.
 * @param level the level at which this information should be printed
 * @param fmt the formatting string (<b>MUST</b> be a literal
 */
#define lDebug(level, fmt, ...)                                                                                         \
    do {                                                                                                                \
        if (debugLevel <= level) {                                                                                      \
            xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);                                                           \
            printf("%s %s[%d] %s() " fmt "\n", levelText(level), __FILE__, __LINE__, __func__, ##__VA_ARGS__);          \
            xSemaphoreGive(s_PrintfSemaphore);                                                                          \
        }                                                                                                               \
    } while (0)

/** Simple alias for <tt>lDebug()</tt> */
#define debug(fmt, ...) lDebug(Info, fmt, ##__VA_ARGS__)

/** Prints the file name, line number, function name and "HERE" */
#define HERE            debug("HERE")

#define debugPrintf(format, ...)                                                                                        \
    do {                                                                                                                \
        xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);                                                               \
        printf(format, ##__VA_ARGS__);                                                                                  \
        xSemaphoreGive(s_PrintfSemaphore);                                                                              \
    } while (0)

void debugWrite(const void *data, int size);

#endif // defined(NDEBUG)