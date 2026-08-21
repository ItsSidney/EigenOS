/* jconfig.h — EigenOS freestanding configuration for IJG libjpeg 9e. */
#ifndef JCONFIG_H
#define JCONFIG_H

/* We have ANSI C prototypes and the usual standard headers. */
#define HAVE_PROTOTYPES     1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1
#define HAVE_STDDEF_H      1
#define HAVE_STDLIB_H      1
#define HAVE_STRING_H      1

#undef CHAR_IS_UNSIGNED
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS
#undef NEED_SHORT_EXTERNAL_NAMES
#undef INCOMPLETE_TYPES_BROKEN
#undef HAVE_LOCALE_H
#undef NEED_BSD_STRINGS

/* Memory manager: no backing-store temp files, no getenv(). */
#undef DEFAULT_MAX_MEM
#define NO_MKTEMP
#define NO_GETENV

/* No standalone image-file formats compiled into the library core. */
#undef BMP_SUPPORTED
#undef GIF_SUPPORTED
#undef PPM_SUPPORTED
#undef TARGA_SUPPORTED
#undef RLE_SUPPORTED
#undef TWO_FILE_COMMANDLINE
#undef NEED_SIGNAL_CATCHER
#undef DONT_USE_B_MODE
#undef PROGRESS_REPORT

#endif /* JCONFIG_H */
