#define WANT_VFS_STDIO_COMPAT

#include <cstdlib>
#include <libaudcore/vfs.h>
#include <libaudcore/runtime.h>
#include <utils/DataLoader.h>
#include <zlib.h>
#include "VFSLoader.hpp"

extern const DATA_LOADER_CALLBACKS VFSLoaderCallbacks;

struct VFS_LOADER
{
	VFSFile *file;
	UINT32 size;
	z_stream zstream;
	UINT8 buffer[1024];
	bool is_eof;
	bool is_error;

	UINT32 (*read)(VFS_LOADER *loader, UINT8 *buffer, UINT32 num_bytes);
	UINT8 (*seek)(VFS_LOADER *loader, UINT32 offset, UINT8 whence);
	UINT8 (*close)(VFS_LOADER *loader);
	INT32 (*tell)(VFS_LOADER *loader);
	UINT8 (*eof)(VFS_LOADER *loader);
};

static UINT32 VFSLoader_ReadGZ(VFS_LOADER *loader, UINT8 *buffer, UINT32 num_bytes)
{
	z_stream *zstream;
	UINT32 bytes_wrote;

	zstream = &loader->zstream;
	zstream->next_out = buffer;
	zstream->avail_out = num_bytes;

	bytes_wrote = 0;

	while (zstream->avail_out && !loader->is_error && !loader->is_eof)
	{
		int zret;
		UINT32 old_avail_out = zstream->avail_out;

		if (!zstream->avail_in)
		{
			INT64 bytes_read = loader->file->fread(loader->buffer, 1, sizeof(loader->buffer));

			if (bytes_read <= 0)
			{
				if (loader->file->feof())
				{
					loader->is_eof = true;
					break;
				}
				else
				{
					loader->is_error = true;
					return 0;
				}
			}

			zstream->next_in = loader->buffer;
			zstream->avail_in = bytes_read;
		}

		zret = inflate(zstream, Z_SYNC_FLUSH);
		bytes_wrote += old_avail_out - zstream->avail_out;

		if (zret == Z_STREAM_END)
		{
			loader->is_eof = true;
			break;
		}
		else if (zret != Z_OK)
		{
			loader->is_error = true;
			return 0;
		}
	}

	return bytes_wrote;
}

// TODO (thankfully this doesn't appear to be used in libvgm yet)
static UINT8 VFSLoader_SeekGZ(VFS_LOADER *loader, UINT32 offset, UINT8 whence)
{
	return 1;
}

static UINT8 VFSLoader_CloseGZ(VFS_LOADER *loader)
{
	inflateEnd(&loader->zstream);
	return 0;
}

// TODO (not confident about this, but fortunately unused in libvgm at the moment)
static INT32 VFSLoader_TellGZ(VFS_LOADER *loader)
{
	return loader->zstream.total_out;
}

static UINT8 VFSLoader_EOFGZ(VFS_LOADER *loader)
{
	return loader->is_eof;
}

static UINT32 VFSLoader_ReadRaw(VFS_LOADER *loader, UINT8 *buffer, UINT32 num_bytes)
{
	return loader->file->fread(buffer, 1, num_bytes);
}

static UINT8 VFSLoader_SeekRaw(VFS_LOADER *loader, UINT32 offset, UINT8 whence)
{
	return loader->file->fseek(offset, to_vfs_seek_type(whence));
}

static UINT8 VFSLoader_CloseRaw(VFS_LOADER *loader)
{
	return 0;
}

static INT32 VFSLoader_TellRaw(VFS_LOADER *loader)
{
	return loader->file->ftell();
}

static UINT8 VFSLoader_EOFRaw(VFS_LOADER *loader)
{
	return loader->file->feof();
}

static UINT8 VFSLoader_Open(void *context)
{
	VFS_LOADER *loader = (VFS_LOADER *)context;
	UINT8 buffer[4];

	if (loader->file->fread(buffer, 1, 2) != 2)
		return 1;

	if (buffer[0] == 0x1F && buffer[1] == 0x8B)
	{
		if (loader->file->fseek(-4, VFS_SEEK_END))
			return 1;

		if (loader->file->fread(buffer, 4, 1) != 1)
			return 1;

		loader->size = (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];

		loader->zstream.next_in = Z_NULL;
		loader->zstream.avail_in = 0;
		loader->zstream.total_in = 0;
		loader->zstream.next_out = Z_NULL;
		loader->zstream.avail_out = 0;
		loader->zstream.total_out = 0;
		loader->zstream.zalloc = Z_NULL;
		loader->zstream.zfree = Z_NULL;
		loader->zstream.opaque = Z_NULL;

		if (inflateInit2(&loader->zstream, 15 + 32))
			return 1;

		loader->read = VFSLoader_ReadGZ;
		loader->seek = VFSLoader_SeekGZ;
		loader->close = VFSLoader_CloseGZ;
		loader->tell = VFSLoader_TellGZ;
		loader->eof = VFSLoader_EOFGZ;
	}
	else
	{
		INT64 raw_size = loader->file->fsize();

		if (raw_size == -1)
			return 1;

		loader->size = raw_size;
		loader->read = VFSLoader_ReadRaw;
		loader->seek = VFSLoader_SeekRaw;
		loader->close = VFSLoader_CloseRaw;
		loader->tell = VFSLoader_TellRaw;
		loader->eof = VFSLoader_EOFRaw;
	}

	if (loader->file->fseek(0, VFS_SEEK_SET))
		return 1;

	return 0;
}

static UINT32 VFSLoader_Read(void *context, UINT8 *buffer, UINT32 num_bytes)
{
	VFS_LOADER *loader = (VFS_LOADER *)context;
	return loader->read(loader, buffer, num_bytes);
}

static UINT8 VFSLoader_Seek(void *context, UINT32 offset, UINT8 whence)
{
	VFS_LOADER *loader = (VFS_LOADER *)context;
	return loader->seek(loader, offset, whence);
}

static UINT8 VFSLoader_Close(void *context)
{
	VFS_LOADER *loader = (VFS_LOADER *)context;
	return loader->close(loader);
}

static INT32 VFSLoader_Tell(void *context)
{
	VFS_LOADER *loader = (VFS_LOADER *)context;
	return loader->tell(loader);
}

static UINT32 VFSLoader_Length(void *context)
{
	VFS_LOADER *loader = (VFS_LOADER *)context;
	return loader->size;
}

static UINT8 VFSLoader_EOF(void *context)
{
	VFS_LOADER *loader = (VFS_LOADER *)context;
	return loader->eof(loader);
}

DATA_LOADER *VFSLoader_Init(VFSFile &file)
{
	DATA_LOADER *data_loader;
	VFS_LOADER *vfs_loader;

	data_loader = (DATA_LOADER *)calloc(1, sizeof(DATA_LOADER));
	if (!data_loader)
	{
		return NULL;
	}

	vfs_loader = (VFS_LOADER *)calloc(1, sizeof(VFS_LOADER));
	if (!vfs_loader)
	{
		free(data_loader);
		return NULL;
	}

	vfs_loader->file = &file;
	vfs_loader->is_eof = false;

	DataLoader_Setup(data_loader, &VFSLoaderCallbacks, vfs_loader);

	return data_loader;
}

const DATA_LOADER_CALLBACKS VFSLoaderCallbacks = {
	0x41564653, // "AVFS"
	"Audacious VFS",
	VFSLoader_Open,
	VFSLoader_Read,
	VFSLoader_Seek,
	VFSLoader_Close,
	VFSLoader_Tell,
	VFSLoader_Length,
	VFSLoader_EOF,
	NULL
};
