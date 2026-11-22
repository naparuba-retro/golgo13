/* Translation patches ("G13T" files built by tools/translation/build.py): byte-exact replacements applied to the in-memory copy
 * of the user's ROM after it has been loaded and verified. A record is applied only if the bytes it replaces still have the CRC32
 * recorded when the patch was built, so a patch can never be applied to the wrong ROM or applied twice with a different effect. */
#ifndef GOLGO_TRANSLATE_H
#define GOLGO_TRANSLATE_H
#include <stddef.h>

/* Looks for <dir>/<lang>/patch.g13t in each of dirs[] (first match wins) and applies it.
 * Returns the number of records applied (>= 0), or -1 if no patch was found / the file is invalid. msg receives a one-line report. */
int translate_apply_lang(const char *lang, const char *const *dirs, int ndirs, char *msg, size_t msgsz);
typedef struct { char code[32]; char name[64]; } translate_lang_t;
/* Languages found under the directories (a language = a folder with patch.g13t and/or overlay.g13o; name from info.txt). */
int translate_list(const char *const *dirs, int ndirs, translate_lang_t *out, int max);
/* Undoes every applied patch (restores the ROM bytes) and drops the overlays: back to the original game. */
void translate_revert(void);
int translate_apply_file(const char *path, int *skipped, char *msg, size_t msgsz);
#endif
