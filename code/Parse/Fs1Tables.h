/*
 * FS1 compatibility (docs/FS1_COMPAT.md): default text for tables that FreeSpace 2 reads and
 * FreeSpace 1 data does not have.  Data loading only: the parsers read this text as if it were
 * the file, so game logic stays the same for FS1 and FS2 data.
 */

#ifndef _FS1TABLES_H
#define _FS1TABLES_H

// In FS1 data (Cfile_fs1_data), the text to use for a table the data lacks, or NULL.  The text
// stays valid until the next call.
char *fs1_table_default(char *filename);

// In FS1 data, rewrites an FS1 table's text (len bytes, room for max_len) into the form FS2's parser
// reads, and returns its new length.  Tables with no converter come back unchanged.
int fs1_table_convert(char *filename, char *text, int len, int max_len);

#endif
