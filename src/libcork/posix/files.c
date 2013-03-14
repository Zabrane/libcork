/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2013, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the COPYING file in this distribution for license details.
 * ----------------------------------------------------------------------
 */

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libcork/core/attributes.h"
#include "libcork/core/error.h"
#include "libcork/core/types.h"
#include "libcork/ds/buffer.h"
#include "libcork/helpers/errors.h"
#include "libcork/helpers/posix.h"
#include "libcork/os/files.h"


#if !defined(CORK_DEBUG_FILES)
#define CORK_DEBUG_FILES  0
#endif

#if CORK_DEBUG_FILES
#include <stdio.h>
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...) /* no debug messages */
#endif


/*-----------------------------------------------------------------------
 * Paths
 */

struct cork_path {
    struct cork_buffer  given;
};

static struct cork_path *
cork_path_new_internal(void)
{
    struct cork_path  *path = cork_new(struct cork_path);
    cork_buffer_init(&path->given);
    return path;
}

struct cork_path *
cork_path_new(const char *source)
{
    struct cork_path  *path = cork_path_new_internal();
    if (source == NULL) {
        cork_buffer_ensure_size(&path->given, 16);
        cork_buffer_set(&path->given, "", 0);
    } else {
        cork_buffer_set_string(&path->given, source);
    }
    return path;
}

struct cork_path *
cork_path_clone(const struct cork_path *other)
{
    struct cork_path  *path = cork_path_new_internal();
    cork_buffer_copy(&path->given, &other->given);
    return path;
}

void
cork_path_free(struct cork_path *path)
{
    cork_buffer_done(&path->given);
    free(path);
}

const char *
cork_path_get(const struct cork_path *path)
{
    return path->given.buf;
}

#define cork_path_get(path) ((const char *) (path)->given.buf)
#define cork_path_size(path)  ((path)->given.size)
#define cork_path_truncate(path, size) \
    (cork_buffer_truncate(&(path)->given, (size)))


int
cork_path_set_absolute(struct cork_path *path)
{
    struct cork_buffer  buf;

    if (path->given.size > 0 &&
        cork_buffer_char(&path->given, path->given.size - 1) == '/') {
        /* The path is already absolute */
        return 0;
    }

    cork_buffer_init(&buf);
    cork_buffer_ensure_size(&buf, PATH_MAX);
    ep_check_posix(getcwd(buf.buf, PATH_MAX));
    buf.size = strlen(buf.buf);
    cork_buffer_append(&buf, "/", 1);
    cork_buffer_append_copy(&buf, &path->given);
    cork_buffer_done(&path->given);
    path->given = buf;
    return 0;

error:
    cork_buffer_done(&buf);
    return -1;
}

struct cork_path *
cork_path_absolute(const struct cork_path *other)
{
    struct cork_path  *path = cork_path_clone(other);
    cork_path_set_absolute(path);
    return path;
}


void
cork_path_append(struct cork_path *path, const char *more)
{
    if (more == NULL || more[0] == '\0') {
        return;
    }

    if (more[0] == '/') {
        /* If more starts with a "/", then its absolute, and should replace the
         * contents of the current path. */
        cork_buffer_set_string(&path->given, more);
    } else {
        /* Otherwise, more is relative, and should be appended to the current
         * path.  If the current given path doesn't end in a "/", then we need
         * to add one to keep the path well-formed. */

        if (path->given.size > 0 &&
            cork_buffer_char(&path->given, path->given.size - 1) != '/') {
            cork_buffer_append(&path->given, "/", 1);
        }

        cork_buffer_append_string(&path->given, more);
    }
}

struct cork_path *
cork_path_join(const struct cork_path *other, const char *more)
{
    struct cork_path  *path = cork_path_clone(other);
    cork_path_append(path, more);
    return path;
}

void
cork_path_append_path(struct cork_path *path, const struct cork_path *more)
{
    cork_path_append(path, more->given.buf);
}

struct cork_path *
cork_path_join_path(const struct cork_path *other, const struct cork_path *more)
{
    struct cork_path  *path = cork_path_clone(other);
    cork_path_append_path(path, more);
    return path;
}


void
cork_path_set_basename(struct cork_path *path)
{
    char  *given = path->given.buf;
    const char  *last_slash = strrchr(given, '/');
    if (last_slash != NULL) {
        size_t  offset = last_slash - given;
        size_t  basename_length = path->given.size - offset - 1;
        memmove(given, last_slash + 1, basename_length);
        given[basename_length] = '\0';
        path->given.size = basename_length;
    }
}

struct cork_path *
cork_path_basename(const struct cork_path *other)
{
    struct cork_path  *path = cork_path_clone(other);
    cork_path_set_basename(path);
    return path;
}


void
cork_path_set_dirname(struct cork_path *path)
{
    const char  *given = path->given.buf;
    const char  *last_slash = strrchr(given, '/');
    if (last_slash == NULL) {
        cork_buffer_clear(&path->given);
    } else {
        size_t  offset = last_slash - given;
        cork_buffer_truncate(&path->given, offset);
    }
}

struct cork_path *
cork_path_dirname(const struct cork_path *other)
{
    struct cork_path  *path = cork_path_clone(other);
    cork_path_set_dirname(path);
    return path;
}


/*-----------------------------------------------------------------------
 * Files
 */

struct cork_file {
    struct cork_path  *path;
    struct stat  stat;
    enum cork_file_type  type;
    bool  has_stat;
};

static void
cork_file_init(struct cork_file *file, struct cork_path *path)
{
    file->path = path;
    file->has_stat = false;
}

struct cork_file *
cork_file_new(const char *path)
{
    return cork_file_new_from_path(cork_path_new(path));
}

struct cork_file *
cork_file_new_from_path(struct cork_path *path)
{
    struct cork_file  *file = cork_new(struct cork_file);
    cork_file_init(file, path);
    return file;
}

static void
cork_file_reset(struct cork_file *file)
{
    file->has_stat = false;
}

static void
cork_file_done(struct cork_file *file)
{
    cork_path_free(file->path);
}

void
cork_file_free(struct cork_file *file)
{
    cork_file_done(file);
    free(file);
}

const struct cork_path *
cork_file_path(struct cork_file *file)
{
    return file->path;
}

static int
cork_file_stat(struct cork_file *file)
{
    if (file->has_stat) {
        return 0;
    } else {
        int  rc;
        rc = stat(cork_path_get(file->path), &file->stat);

        if (rc == -1) {
            if (errno == ENOENT || errno == ENOTDIR) {
                file->type = CORK_FILE_MISSING;
                file->has_stat = true;
                return 0;
            } else {
                cork_system_error_set();
                return -1;
            }
        }

        if (S_ISREG(file->stat.st_mode)) {
            file->type = CORK_FILE_REGULAR;
        } else if (S_ISDIR(file->stat.st_mode)) {
            file->type = CORK_FILE_DIRECTORY;
        } else if (S_ISLNK(file->stat.st_mode)) {
            file->type = CORK_FILE_SYMLINK;
        } else {
            file->type = CORK_FILE_UNKNOWN;
        }

        file->has_stat = true;
        return 0;
    }
}

int
cork_file_exists(struct cork_file *file, bool *exists)
{
    rii_check(cork_file_stat(file));
    *exists = (file->type != CORK_FILE_MISSING);
    return 0;
}

int
cork_file_type(struct cork_file *file, enum cork_file_type *type)
{
    rii_check(cork_file_stat(file));
    *type = file->type;
    return 0;
}


/*-----------------------------------------------------------------------
 * Directories
 */

int
cork_file_iterate_directory(struct cork_file *file,
                            cork_file_directory_iterator iterator,
                            void *user_data)
{
    DIR  *dir = NULL;
    struct dirent  *entry;
    size_t  dir_path_size;
    struct cork_path  *child_path;
    struct cork_file  child_file;

    rip_check_posix(dir = opendir(cork_path_get(file->path)));
    child_path = cork_path_clone(file->path);
    cork_file_init(&child_file, child_path);
    dir_path_size = cork_path_size(child_path);

    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip the "." and ".." entries */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        cork_path_append(child_path, entry->d_name);
        ei_check(cork_file_stat(&child_file));

        /* If the entry is a subdirectory, recurse into it. */
        ei_check(iterator(&child_file, entry->d_name, user_data));

        /* Remove this entry name from the path buffer. */
        cork_path_truncate(child_path, dir_path_size);
        cork_file_reset(&child_file);

        /* We have to reset errno to 0 because of the ambiguous way readdir uses
         * a return value of NULL.  Other functions may return normally yet set
         * errno to a non-zero value.  dlopen on Mac OS X is an ogreish example.
         * Since an error readdir is indicated by returning NULL and setting
         * errno to indicate the error, then we need to reset it to zero before
         * each call.  We shall assume, perhaps to our great misery, that
         * functions within this loop do proper error checking and act
         * accordingly. */
        errno = 0;
    }

    /* Check errno immediately after the while loop terminates */
    if (CORK_UNLIKELY(errno != 0)) {
        cork_system_error_set();
        goto error;
    }

    cork_file_done(&child_file);
    rii_check_posix(closedir(dir));
    return 0;

error:
    cork_file_done(&child_file);
    rii_check_posix(closedir(dir));
    return -1;
}

static int
cork_file_mkdir_one(struct cork_file *file, cork_file_mode mode,
                    unsigned int flags)
{
    DEBUG("mkdir %s\n", cork_path_get(file->path));

    /* First check if the directory already exists. */
    rii_check(cork_file_stat(file));
    if (file->type == CORK_FILE_DIRECTORY) {
        DEBUG("  Already exists!\n");
        if (!(flags & CORK_FILE_PERMISSIVE)) {
            cork_system_error_set_explicit(EEXIST);
            return -1;
        } else {
            return 0;
        }
    } else if (file->type != CORK_FILE_MISSING) {
        DEBUG("  Exists and not a directory!\n");
        cork_system_error_set_explicit(EEXIST);
        return -1;
    }

    /* If the caller asked for a recursive mkdir, then make sure the parent
     * directory exists. */
    if (flags & CORK_FILE_RECURSIVE) {
        struct cork_path  *parent = cork_path_dirname(file->path);
        DEBUG("  Checking parent %s\n", cork_path_get(parent));
        if (parent->given.size == 0) {
            /* There is no parent; we're either at the filesystem root (for an
             * absolute path) or the current directory (for a relative one).
             * Either way, we can assume it already exists. */
            cork_path_free(parent);
        } else {
            int  rc;
            struct cork_file  parent_file;
            cork_file_init(&parent_file, parent);
            rc = cork_file_mkdir_one
                (&parent_file, mode, flags | CORK_FILE_PERMISSIVE);
            cork_file_done(&parent_file);
            rii_check(rc);
        }
    }

    /* Create the directory already! */
    DEBUG("  Creating %s\n", cork_path_get(file->path));
    rii_check_posix(mkdir(cork_path_get(file->path), mode));
    return 0;
}

int
cork_file_mkdir(struct cork_file *file, mode_t mode, unsigned int flags)
{
    return cork_file_mkdir_one(file, mode, flags);
}

static int
cork_file_remove_iterator(struct cork_file *file, const char *rel_name,
                          void *user_data)
{
    unsigned int  *flags = user_data;
    return cork_file_remove(file, *flags);
}

int
cork_file_remove(struct cork_file *file, unsigned int flags)
{
    DEBUG("rm %s\n", cork_path_get(file->path));
    rii_check(cork_file_stat(file));

    if (file->type == CORK_FILE_MISSING) {
        if (flags & CORK_FILE_PERMISSIVE) {
            return 0;
        } else {
            cork_system_error_set_explicit(ENOENT);
            return -1;
        }
    } else if (file->type == CORK_FILE_DIRECTORY) {
        if (flags & CORK_FILE_RECURSIVE) {
            /* The user asked that we delete the contents of the directory
             * first. */
            rii_check(cork_file_iterate_directory
                      (file, cork_file_remove_iterator, &flags));
        }

        rii_check_posix(rmdir(cork_path_get(file->path)));
        return 0;
    } else {
        rii_check(unlink(cork_path_get(file->path)));
        return 0;
    }
}
