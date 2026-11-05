#ifndef BUILDER_H
#define BUILDER_H
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include <wchar.h>
typedef wchar_t pathchar;
#define PATH_TEXT(s) L##s
#else
typedef char pathchar;
#define PATH_TEXT(s) s
#endif
enum Result {
    RESULT_OK, RESULT_PACKAGE_MISSING, RESULT_PACKAGE_INVALID,
    RESULT_MEMORY, RESULT_MPQ_INVALID, RESULT_AUTHENTICATION,
    RESULT_ROM_INVALID, RESULT_OUTPUT, RESULT_CANCELLED,
    RESULT_OUTPUT_EXISTS, RESULT_OUTPUT_EXTENSION
};
enum Stage { STAGE_CHECK, STAGE_MPQ, STAGE_PACKAGE, STAGE_DECRYPT,
             STAGE_VERIFY, STAGE_SAVE, STAGE_DONE };
/* Returning zero requests cancellation. No callback runs after builder_run returns. */
typedef int (*Progress)(enum Stage stage, unsigned percent, void *userdata);
int builder_run(const pathchar *mpq, const pathchar *package, const pathchar *output,
                Progress callback, void *userdata);
/* Package remains read-only and valid for the duration of the call. */
int builder_run_memory(const pathchar *mpq, const unsigned char *package, size_t size,
                       const pathchar *output, Progress callback, void *userdata);
const char *builder_message(int code);
const char *builder_stage(enum Stage stage);
#endif
