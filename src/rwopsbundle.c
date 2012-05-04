#include <errno.h>
#include <string.h>
#include <SDL_rwops.h>
#include <SDL_error.h>

#include "rwopsbundle.h"

// Our custom RWops type id. Not strictly needed, but it helps catching bugs
// if somehow we end up trying to delete a different type of RWops.
#define CUSTOM_RWOPS_TYPE 3819859

// Media resource information for our custom RWops implementation. Media
// resources are embedded inside media bundle files. They begin at 'startPos'
// and end at 'endPos' inside the 'file' bundle.
typedef struct {
    FILE* file;
    long startPos;
    long endPos;
} BundleFileInfo;


/* RWops seek callback. We apply offsets to make all seek operations relative
 * to the start/end of the media resource embedded inside the media bundle
 * file.
 *
 * Must return the new current SEET_SET position.
 */
static int
RWOpsSeekFunc( SDL_RWops* rwops, int offset, int whence )
{
    BundleFileInfo* info = rwops->hidden.unknown.data1;
    int seekRet;
    errno = 0;
    if (whence == SEEK_CUR) {
        seekRet = fseek(info->file, offset, SEEK_CUR);
    } else if (whence == SEEK_SET) {
        seekRet = fseek(info->file, info->startPos + offset, SEEK_SET);
    } else {
        seekRet = fseek(info->file, info->endPos + offset, SEEK_SET);
    }
    if (seekRet != 0) {
        SDL_SetError("Could not fseek() in media bundle (%s)",
                     errno != 0 ? strerror(errno) : "unknown error");
        return -1;
    }
    return ftell(info->file) - info->startPos;
}


/* RWops read callback. We don't allow reading past the end of the media
 * resource embedded inside the media bundle file.
 *
 * Must return the number of elements (not bytes) that have been read.
 */
static int
RWOpsReadFunc( SDL_RWops* rwops, void* ptr, int size, int maxnum )
{
    BundleFileInfo* info = rwops->hidden.unknown.data1;
    long bytesToRead = size * maxnum;
    long curPos = ftell(info->file);
    size_t itemsRead;
    // Make sure we don't read past the end of the embedded media resource.
    if (curPos + bytesToRead > info->endPos) {
        bytesToRead = info->endPos - curPos;
        maxnum = bytesToRead / size;
    }
    itemsRead = fread(ptr, size, maxnum, info->file);
    if (maxnum != 0 && itemsRead == 0) {
        SDL_SetError("Could not read from file stream with fread()");
        return -1;
    }
    return itemsRead;
}


/* RWops write callback. This always fails, since we never write to media
 * bundle files.
 */
static int
RWOpsWriteFunc( SDL_RWops* rwops, const void* ptr, int size, int num )
{
    SDL_SetError("Media bundle files are not supposed to be written to");
    return -1;
}


/* RWops close callback. Frees the RWops as well as our custom data.
 */
static int
RWOpsCloseFunc( SDL_RWops* rwops )
{
    if (rwops->type != CUSTOM_RWOPS_TYPE) {
        SDL_SetError("RWOpsCloseFunc() called with unrecognized RWops type %u",
                     rwops->type);
        return -1;
    }
    BundleFileInfo* info = rwops->hidden.unknown.data1;
    fclose(info->file);
    SDL_free(info);
    SDL_FreeRW(rwops);
    return 0;
}


SDL_RWops*
RWFromMediaBundle( FILE* mediaBundle, long resLength )
{
    SDL_RWops* rwops = SDL_AllocRW();
    if (rwops == NULL) {
        return NULL;
    }

    errno = 0;
    BundleFileInfo* info = SDL_malloc(sizeof *info);
    if (info == NULL) {
        SDL_SetError(errno != 0 ? strerror(errno) : "Cannot allocate memory");
        SDL_FreeRW(rwops);
        return NULL;
    }
    info->file = mediaBundle;
    errno = 0;
    info->startPos = ftell(mediaBundle);
    if (info->startPos == -1) {
        SDL_SetError("Could not obtain current file stream position (%s)",
                     errno != 0 ? strerror(errno) : "unknown error");
        SDL_free(info);
        SDL_FreeRW(rwops);
        return NULL;
    }
    info->endPos = info->startPos + resLength;

    rwops->hidden.unknown.data1 = info;
    rwops->seek = RWOpsSeekFunc;
    rwops->read = RWOpsReadFunc;
    rwops->write = RWOpsWriteFunc;
    rwops->close = RWOpsCloseFunc;
    rwops->type = CUSTOM_RWOPS_TYPE;
    return rwops;
}
