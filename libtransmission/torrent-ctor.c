/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h> /* EINVAL */

#include "transmission.h"
#include "file.h"
#include "magnet.h"
#include "session.h" /* tr_sessionFindTorrentFile() */
#include "torrent.h" /* tr_ctorGetSave() */
#include "tr-assert.h"
#include "utils.h" /* tr_new0 */
#include "variant.h"

struct optional_args
{
    bool isSet_paused;
    bool isSet_connected;
    bool isSet_downloadDir;

    bool isPaused;
    uint16_t peerLimit;
    char* downloadDir;
};

/** Opaque class used when instantiating torrents.
 * @ingroup tr_ctor */
struct tr_ctor
{
    tr_session const* session;
    bool saveInOurTorrentsDir;
    bool doDelete;

    tr_priority_t bandwidthPriority;
    bool isSet_metainfo;
    bool isSet_delete;
    tr_variant metainfo;
    char* sourceFile;
    tr_ptrArray labels;

    struct optional_args optionalArgs[2];

    char* cookies;
    char* incompleteDir;

    tr_file_index_t* want;
    tr_file_index_t wantSize;
    tr_file_index_t* notWant;
    tr_file_index_t notWantSize;
    tr_file_index_t* low;
    tr_file_index_t lowSize;
    tr_file_index_t* normal;
    tr_file_index_t normalSize;
    tr_file_index_t* high;
    tr_file_index_t highSize;
};

/***
****
***/

static void setSourceFile(tr_ctor* ctor, char const* sourceFile)
{
    tr_free(ctor->sourceFile);
    ctor->sourceFile = tr_strdup(sourceFile);
}

static void clearMetainfo(tr_ctor* ctor)
{
    if (ctor->isSet_metainfo)
    {
        ctor->isSet_metainfo = false;
        tr_variantFree(&ctor->metainfo);
    }

    setSourceFile(ctor, NULL);
}

int tr_ctorSetMetainfo(tr_ctor* ctor, uint8_t const* metainfo, size_t len)
{
    int err;

    clearMetainfo(ctor);
    err = tr_variantFromBenc(&ctor->metainfo, metainfo, len);
    ctor->isSet_metainfo = err == 0;
    return err;
}

char const* tr_ctorGetSourceFile(tr_ctor const* ctor)
{
    return ctor->sourceFile;
}

int tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, char const* magnet_link)
{
    int err;
    tr_magnet_info* magnet_info = tr_magnetParse(magnet_link);

    if (magnet_info == NULL)
    {
        err = -1;
    }
    else
    {
        size_t len;
        tr_variant tmp;
        char* str;

        tr_magnetCreateMetainfo(magnet_info, &tmp);
        str = tr_variantToStr(&tmp, TR_VARIANT_FMT_BENC, &len);
        err = tr_ctorSetMetainfo(ctor, (uint8_t const*)str, len);

        tr_free(str);
        tr_variantFree(&tmp);
        tr_magnetFree(magnet_info);
    }

    return err;
}

int tr_ctorSetMetainfoFromFile(tr_ctor* ctor, char const* filename)
{
    uint8_t* metainfo;
    size_t len;
    int err;

    metainfo = tr_loadFile(filename, &len, NULL);

    if (metainfo != NULL && len != 0)
    {
        err = tr_ctorSetMetainfo(ctor, metainfo, len);
    }
    else
    {
        clearMetainfo(ctor);
        err = 1;
    }

    setSourceFile(ctor, filename);

    /* if no `name' field was set, then set it from the filename */
    if (ctor->isSet_metainfo)
    {
        tr_variant* info;

        if (tr_variantDictFindDict(&ctor->metainfo, TR_KEY_info, &info))
        {
            char const* name;

            if (!tr_variantDictFindStr(info, TR_KEY_name_utf_8, &name, NULL))
            {
                if (!tr_variantDictFindStr(info, TR_KEY_name, &name, NULL))
                {
                    name = NULL;
                }
            }

            if (tr_str_is_empty(name))
            {
                char* base = tr_sys_path_basename(filename, NULL);

                if (base != NULL)
                {
                    tr_variantDictAddStr(info, TR_KEY_name, base);
                    tr_free(base);
                }
            }
        }
    }

    tr_free(metainfo);
    return err;
}

int tr_ctorSetMetainfoFromHash(tr_ctor* ctor, char const* hashString)
{
    int err;
    char const* filename;

    filename = tr_sessionFindTorrentFile(ctor->session, hashString);

    if (filename == NULL)
    {
        err = EINVAL;
    }
    else
    {
        err = tr_ctorSetMetainfoFromFile(ctor, filename);
    }

    return err;
}

/***
****
***/

void tr_ctorSetFilePriorities(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t fileCount, tr_priority_t priority)
{
    tr_file_index_t** myfiles;
    tr_file_index_t* mycount;

    switch (priority)
    {
    case TR_PRI_LOW:
        myfiles = &ctor->low;
        mycount = &ctor->lowSize;
        break;

    case TR_PRI_HIGH:
        myfiles = &ctor->high;
        mycount = &ctor->highSize;
        break;

    default /*TR_PRI_NORMAL*/:
        myfiles = &ctor->normal;
        mycount = &ctor->normalSize;
        break;
    }

    tr_free(*myfiles);
    *myfiles = tr_memdup(files, sizeof(tr_file_index_t) * fileCount);
    *mycount = fileCount;
}

void tr_ctorInitTorrentPriorities(tr_ctor const* ctor, tr_torrent* tor)
{
    for (tr_file_index_t i = 0; i < ctor->lowSize; ++i)
    {
        tr_torrentInitFilePriority(tor, ctor->low[i], TR_PRI_LOW);
    }

    for (tr_file_index_t i = 0; i < ctor->normalSize; ++i)
    {
        tr_torrentInitFilePriority(tor, ctor->normal[i], TR_PRI_NORMAL);
    }

    for (tr_file_index_t i = 0; i < ctor->highSize; ++i)
    {
        tr_torrentInitFilePriority(tor, ctor->high[i], TR_PRI_HIGH);
    }
}

void tr_ctorSetFilesWanted(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t fileCount, bool wanted)
{
    tr_file_index_t** myfiles = wanted ? &ctor->want : &ctor->notWant;
    tr_file_index_t* mycount = wanted ? &ctor->wantSize : &ctor->notWantSize;

    tr_free(*myfiles);
    *myfiles = tr_memdup(files, sizeof(tr_file_index_t) * fileCount);
    *mycount = fileCount;
}

void tr_ctorInitTorrentWanted(tr_ctor const* ctor, tr_torrent* tor)
{
    if (ctor->notWantSize != 0)
    {
        tr_torrentInitFileDLs(tor, ctor->notWant, ctor->notWantSize, false);
    }

    if (ctor->wantSize != 0)
    {
        tr_torrentInitFileDLs(tor, ctor->want, ctor->wantSize, true);
    }
}

/***
****
***/

void tr_ctorSetDeleteSource(tr_ctor* ctor, bool deleteSource)
{
    ctor->doDelete = deleteSource;
    ctor->isSet_delete = true;
}

bool tr_ctorGetDeleteSource(tr_ctor const* ctor, bool* setme)
{
    bool ret = true;

    if (!ctor->isSet_delete)
    {
        ret = false;
    }
    else if (setme != NULL)
    {
        *setme = ctor->doDelete;
    }

    return ret;
}

/***
****
***/

void tr_ctorSetSave(tr_ctor* ctor, bool saveInOurTorrentsDir)
{
    ctor->saveInOurTorrentsDir = saveInOurTorrentsDir;
}

bool tr_ctorGetSave(tr_ctor const* ctor)
{
    return ctor != NULL && ctor->saveInOurTorrentsDir;
}

void tr_ctorSetPaused(tr_ctor* ctor, tr_ctorMode mode, bool isPaused)
{
    TR_ASSERT(ctor != NULL);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    struct optional_args* args = &ctor->optionalArgs[mode];
    args->isSet_paused = true;
    args->isPaused = isPaused;
}

void tr_ctorSetPeerLimit(tr_ctor* ctor, tr_ctorMode mode, uint16_t peerLimit)
{
    TR_ASSERT(ctor != NULL);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    struct optional_args* args = &ctor->optionalArgs[mode];
    args->isSet_connected = true;
    args->peerLimit = peerLimit;
}

void tr_ctorSetDownloadDir(tr_ctor* ctor, tr_ctorMode mode, char const* directory)
{
    TR_ASSERT(ctor != NULL);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    struct optional_args* args = &ctor->optionalArgs[mode];
    tr_free(args->downloadDir);
    args->downloadDir = NULL;
    args->isSet_downloadDir = false;

    if (!tr_str_is_empty(directory))
    {
        args->isSet_downloadDir = true;
        args->downloadDir = tr_strdup(directory);
    }
}

void tr_ctorSetIncompleteDir(tr_ctor* ctor, char const* directory)
{
    tr_free(ctor->incompleteDir);
    ctor->incompleteDir = tr_strdup(directory);
}

bool tr_ctorGetPeerLimit(tr_ctor const* ctor, tr_ctorMode mode, uint16_t* setmeCount)
{
    bool ret = true;
    struct optional_args const* args = &ctor->optionalArgs[mode];

    if (!args->isSet_connected)
    {
        ret = false;
    }
    else if (setmeCount != NULL)
    {
        *setmeCount = args->peerLimit;
    }

    return ret;
}

bool tr_ctorGetPaused(tr_ctor const* ctor, tr_ctorMode mode, bool* setmeIsPaused)
{
    bool ret = true;
    struct optional_args const* args = &ctor->optionalArgs[mode];

    if (!args->isSet_paused)
    {
        ret = false;
    }
    else if (setmeIsPaused != NULL)
    {
        *setmeIsPaused = args->isPaused;
    }

    return ret;
}

bool tr_ctorGetDownloadDir(tr_ctor const* ctor, tr_ctorMode mode, char const** setmeDownloadDir)
{
    bool ret = true;
    struct optional_args const* args = &ctor->optionalArgs[mode];

    if (!args->isSet_downloadDir)
    {
        ret = false;
    }
    else if (setmeDownloadDir != NULL)
    {
        *setmeDownloadDir = args->downloadDir;
    }

    return ret;
}

bool tr_ctorGetIncompleteDir(tr_ctor const* ctor, char const** setmeIncompleteDir)
{
    bool ret = true;

    if (ctor->incompleteDir == NULL)
    {
        ret = false;
    }
    else
    {
        *setmeIncompleteDir = ctor->incompleteDir;
    }

    return ret;
}

bool tr_ctorGetMetainfo(tr_ctor const* ctor, tr_variant const** setme)
{
    bool ret = true;

    if (!ctor->isSet_metainfo)
    {
        ret = false;
    }
    else if (setme != NULL)
    {
        *setme = &ctor->metainfo;
    }

    return ret;
}

tr_session* tr_ctorGetSession(tr_ctor const* ctor)
{
    return (tr_session*)ctor->session;
}

/***
****
***/

static bool isPriority(int i)
{
    return i == TR_PRI_LOW || i == TR_PRI_NORMAL || i == TR_PRI_HIGH;
}

void tr_ctorSetBandwidthPriority(tr_ctor* ctor, tr_priority_t priority)
{
    if (isPriority(priority))
    {
        ctor->bandwidthPriority = priority;
    }
}

tr_priority_t tr_ctorGetBandwidthPriority(tr_ctor const* ctor)
{
    return ctor->bandwidthPriority;
}

/***
****
***/

void tr_ctorSetLabels(tr_ctor* ctor, struct tr_ptrArray const* labels)
{
    TR_ASSERT(ctor != NULL);

    tr_ptrArrayDestruct(&ctor->labels, tr_free);
    ctor->labels = TR_PTR_ARRAY_INIT;
    char** l = (char**)tr_ptrArrayBase(labels);
    int const n = tr_ptrArraySize(labels);
    for (int i = 0; i < n; i++)
    {
        tr_ptrArrayAppend(&ctor->labels, tr_strdup(l[i]));
    }
}

struct tr_ptrArray const* tr_ctorGetLabels(tr_ctor const* ctor)
{
    TR_ASSERT(ctor != NULL);

    return &ctor->labels;
}

/***
****
***/

tr_ctor* tr_ctorNew(tr_session const* session)
{
    tr_ctor* ctor = tr_new0(struct tr_ctor, 1);

    ctor->session = session;
    ctor->bandwidthPriority = TR_PRI_NORMAL;

    if (session != NULL)
    {
        tr_ctorSetDeleteSource(ctor, tr_sessionGetDeleteSource(session));
        tr_ctorSetPaused(ctor, TR_FALLBACK, tr_sessionGetPaused(session));
        tr_ctorSetPeerLimit(ctor, TR_FALLBACK, session->peerLimitPerTorrent);
        tr_ctorSetDownloadDir(ctor, TR_FALLBACK, tr_sessionGetDownloadDir(session));
    }

    tr_ctorSetSave(ctor, true);
    return ctor;
}

void tr_ctorFree(tr_ctor* ctor)
{
    clearMetainfo(ctor);
    tr_free(ctor->optionalArgs[1].downloadDir);
    tr_free(ctor->optionalArgs[0].downloadDir);
    tr_free(ctor->incompleteDir);
    tr_free(ctor->want);
    tr_free(ctor->notWant);
    tr_free(ctor->low);
    tr_free(ctor->high);
    tr_free(ctor->normal);
    tr_ptrArrayDestruct(&ctor->labels, tr_free);
    tr_free(ctor);
}
