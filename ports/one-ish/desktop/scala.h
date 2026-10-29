/* Scala .scl / .kbm files into the engine's compact microtuning texts (scala.c; the texts: src/tuning.h). 1: done;
 * 0: err holds why. path: the file's (its name becomes the scale's name). */
#pragma once
int scl_compact(const char *text, const char *path, char *out, int n, char *err, int en);
int kbm_compact(const char *text, const char *path, char *out, int n, char *err, int en);
