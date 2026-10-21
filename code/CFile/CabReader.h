// Claude: a reader for InstallShield 5 cabinets, the format the FreeSpace 1 demo's installer keeps its game
// files in (data1.cab inside fsdemo10.exe; docs/FS1_DEMO.md).  Only what that cabinet uses is read: one
// volume with its header inside the .cab, files stored whole or compressed in raw deflate chunks.  Split,
// obfuscated and multi-volume files are refused, not guessed at.
//
// An InstallShield 5 cabinet keeps no checksum per file, so a file is accepted when every chunk decodes
// cleanly, the chunks use up exactly the compressed size, and the output is exactly the expanded size.
#ifndef _CABREADER_H
#define _CABREADER_H

#define CAB_MAX_PATH		260

typedef struct cab_entry {
	char				path[CAB_MAX_PATH];	// "Data\freespace.vp", or "FS.exe" for the top folder
	unsigned int	expanded;				// size of the file
	unsigned int	compressed;				// size of its data in the cabinet
	unsigned int	offset;					// where its data starts in the cabinet
	unsigned int	flags;					// CAB_FILE_*
} cab_entry;

#define CAB_FILE_SPLIT			0x1
#define CAB_FILE_OBFUSCATED	0x2
#define CAB_FILE_COMPRESSED	0x4

typedef struct cab_archive {
	const unsigned char	*data;			// the whole cabinet, owned by the caller
	unsigned int			len;
	int						num_entries;
	cab_entry				*entries;		// malloced by cab_open
} cab_archive;

// Reads the cabinet's file list.  data must stay valid until cab_close.  0 on success; otherwise non-zero
// and the reason in err.
int cab_open(cab_archive *cab, const unsigned char *data, unsigned int len, char *err, int err_len);
void cab_close(cab_archive *cab);

// Writes entry index into dst, which has room for entries[index].expanded bytes.  0 on success; otherwise
// non-zero and the reason in err.
int cab_extract(cab_archive *cab, int index, unsigned char *dst, char *err, int err_len);

#endif
