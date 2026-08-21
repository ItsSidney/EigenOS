#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <stdint.h>

/* 
 * Ring-3 Open File Dialog.
 * Opens a modal window to select a file.
 * Returns 1 if confirmed (path written to out_path), 0 if canceled.
 */
int eigen_dialog_open(char* out_path, int maxlen, const char* filter_ext);

/* 
 * Ring-3 Save File Dialog.
 * Opens a modal window to choose save destination and filename.
 * Returns 1 if confirmed (path written to out_path), 0 if canceled.
 */
int eigen_dialog_save(char* out_path, int maxlen, const char* default_name);

#endif /* FILE_DIALOG_H */
