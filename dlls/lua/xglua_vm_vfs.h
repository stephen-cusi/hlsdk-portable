#ifndef XGLUA_VM_VFS_H
#define XGLUA_VM_VFS_H

// The engine owns the input buffer; XGLuaReadScript callers own malloc data.
// Engine file APIs do not distinguish a missing file from a read failure.
typedef unsigned char *(*XGLuaVFSLoad)(const char *, int *);
typedef void (*XGLuaVFSFree)(void *);

static XGLuaReadStatus XGLua_ReadVFSFile(const char *prefix, const char *rel,
    XGLuaVFSLoad load, XGLuaVFSFree release, char **out, size_t *len)
{
    if (!prefix || !rel || !load || !release || !out || !len)
        return XGLUA_READ_ERROR;
    // Xash COM_LoadFileForMe copies the name into its 256-byte string.
    // Reject overlong paths here instead of silently loading a truncated name.
    char path[256];
    int n = snprintf(path, sizeof(path), "%s%s", prefix, rel);
    if (n < 0 || (size_t)n >= sizeof(path)) return XGLUA_READ_ERROR;
    int size = 0;
    unsigned char *source = load(path, &size);
    if (!source) return XGLUA_READ_MISSING;
    if (size < 0) { release(source); return XGLUA_READ_ERROR; }
    char *data = (char *)malloc((size_t)size + 1);
    if (!data) { release(source); return XGLUA_READ_ERROR; }
    memcpy(data, source, (size_t)size);
    data[size] = '\0';
    release(source);
    *out = data;
    *len = (size_t)size;
    return XGLUA_READ_OK;
}

#endif
