/* persist.h — EigenOS persistent root: ramfs ⇄ disk snapshot bridge. */

#ifndef EIGEN_PERSIST_H
#define EIGEN_PERSIST_H

/* Load the newest valid snapshot from disk into the files[] table.
 * Call once, after the table is cleared and BEFORE default trees are
 * seeded (restored entries make those seeders no-op). */
void persist_load(void);

/* Save the whole files[] table to disk if anything changed since the
 * last save. Safe to call repeatedly; no-op when clean. */
void persist_sync(const char* reason);

/* Mark the tree mutated (called by fs syscall layer on successful
 * create/delete/write/rename/mkdir/truncate). */
void persist_mark_dirty(void);

int  persist_dirty(void);

/* Background autosave daemon entry (spawn as a ring-0 task). */
void persistd_entry(void);

#endif /* EIGEN_PERSIST_H */
