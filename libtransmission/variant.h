/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <inttypes.h> /* for int64_t */
#include "quark.h"

struct evbuffer;

struct tr_error;

/**
 * @addtogroup tr_variant Variant
 *
 * An object that acts like a union for
 * integers, strings, lists, dictionaries, booleans, and floating-point numbers.
 * The structure is named tr_variant due to the historical reason that it was
 * originally tightly coupled with bencoded data. It currently supports
 * being parsed from, and serialized to, both bencoded notation and json notation.
 *
 * @{
 */

typedef enum
{
    TR_STRING_TYPE_QUARK,
    TR_STRING_TYPE_HEAP,
    TR_STRING_TYPE_BUF
}
tr_string_type;

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
struct tr_variant_string
{
    tr_string_type type;
    tr_quark quark;
    size_t len;
    union
    {
        char buf[16];
        char const* str;
    }
    str;
};

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
enum
{
    TR_VARIANT_TYPE_INT = 1,
    TR_VARIANT_TYPE_STR = 2,
    TR_VARIANT_TYPE_LIST = 4,
    TR_VARIANT_TYPE_DICT = 8,
    TR_VARIANT_TYPE_BOOL = 16,
    TR_VARIANT_TYPE_REAL = 32
};

/* These are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
typedef struct tr_variant
{
    char type;

    tr_quark key;

    union
    {
        bool b;

        double d;

        int64_t i;

        struct tr_variant_string s;

        struct
        {
            size_t alloc;
            size_t count;
            struct tr_variant* vals;
        } l;
    }
    val;
}
tr_variant;

void tr_variantFree(tr_variant*);

/***
****  Serialization / Deserialization
***/

typedef enum
{
    TR_VARIANT_FMT_BENC,
    TR_VARIANT_FMT_JSON,
    TR_VARIANT_FMT_JSON_LEAN /* saves bandwidth by omitting all whitespace. */
}
tr_variant_fmt;

int tr_variantToFile(tr_variant const* variant, tr_variant_fmt fmt, char const* filename);

char* tr_variantToStr(tr_variant const* variant, tr_variant_fmt fmt, size_t* len);

struct evbuffer* tr_variantToBuf(tr_variant const* variant, tr_variant_fmt fmt);

/* TR_VARIANT_FMT_JSON_LEAN and TR_VARIANT_FMT_JSON are equivalent here. */
bool tr_variantFromFile(tr_variant* setme, tr_variant_fmt fmt, char const* filename, struct tr_error** error);

/* TR_VARIANT_FMT_JSON_LEAN and TR_VARIANT_FMT_JSON are equivalent here. */
int tr_variantFromBuf(tr_variant* setme, tr_variant_fmt fmt, void const* buf, size_t buflen, char const* optional_source,
    char const** setme_end);

static inline int tr_variantFromBenc(tr_variant* setme, void const* buf, size_t buflen)
{
    return tr_variantFromBuf(setme, TR_VARIANT_FMT_BENC, buf, buflen, NULL, NULL);
}

static inline int tr_variantFromBencFull(tr_variant* setme, void const* buf, size_t buflen, char const* source,
    char const** setme_end)
{
    return tr_variantFromBuf(setme, TR_VARIANT_FMT_BENC, buf, buflen, source, setme_end);
}

static inline int tr_variantFromJsonFull(tr_variant* setme, void const* buf, size_t buflen, char const* source,
    char const** setme_end)
{
    return tr_variantFromBuf(setme, TR_VARIANT_FMT_JSON, buf, buflen, source, setme_end);
}

static inline int tr_variantFromJson(tr_variant* setme, void const* buf, size_t buflen)
{
    return tr_variantFromBuf(setme, TR_VARIANT_FMT_JSON, buf, buflen, NULL, NULL);
}

static inline bool tr_variantIsType(tr_variant const* b, int type)
{
    return b != NULL && b->type == type;
}

/***
****  Strings
***/

static inline bool tr_variantIsString(tr_variant const* b)
{
    return b != NULL && b->type == TR_VARIANT_TYPE_STR;
}

bool tr_variantGetStr(tr_variant const* variant, char const** setme_str, size_t* setme_len);

void tr_variantInitStr(tr_variant* initme, void const* str, size_t str_len);
void tr_variantInitQuark(tr_variant* initme, tr_quark const quark);
void tr_variantInitRaw(tr_variant* initme, void const* raw, size_t raw_len);

bool tr_variantGetRaw(tr_variant const* variant, uint8_t const** raw_setme, size_t* len_setme);

/***
****  Real Numbers
***/

static inline bool tr_variantIsReal(tr_variant const* v)
{
    return v != NULL && v->type == TR_VARIANT_TYPE_REAL;
}

void tr_variantInitReal(tr_variant* initme, double value);
bool tr_variantGetReal(tr_variant const* variant, double* value_setme);

/***
****  Booleans
***/

static inline bool tr_variantIsBool(tr_variant const* v)
{
    return v != NULL && v->type == TR_VARIANT_TYPE_BOOL;
}

void tr_variantInitBool(tr_variant* initme, bool value);
bool tr_variantGetBool(tr_variant const* variant, bool* setme);

/***
****  Ints
***/

static inline bool tr_variantIsInt(tr_variant const* v)
{
    return v != NULL && v->type == TR_VARIANT_TYPE_INT;
}

void tr_variantInitInt(tr_variant* variant, int64_t value);
bool tr_variantGetInt(tr_variant const* val, int64_t* setme);

/***
****  Lists
***/

static inline bool tr_variantIsList(tr_variant const* v)
{
    return v != NULL && v->type == TR_VARIANT_TYPE_LIST;
}

void tr_variantInitList(tr_variant* list, size_t reserve_count);
void tr_variantListReserve(tr_variant* list, size_t reserve_count);

tr_variant* tr_variantListAdd(tr_variant* list);
tr_variant* tr_variantListAddBool(tr_variant* list, bool addme);
tr_variant* tr_variantListAddInt(tr_variant* list, int64_t addme);
tr_variant* tr_variantListAddReal(tr_variant* list, double addme);
tr_variant* tr_variantListAddStr(tr_variant* list, char const* addme);
tr_variant* tr_variantListAddQuark(tr_variant* list, tr_quark const addme);
tr_variant* tr_variantListAddRaw(tr_variant* list, void const* addme_value, size_t addme_len);
tr_variant* tr_variantListAddList(tr_variant* list, size_t reserve_count);
tr_variant* tr_variantListAddDict(tr_variant* list, size_t reserve_count);
tr_variant* tr_variantListChild(tr_variant const* list, size_t pos);

bool tr_variantListRemove(tr_variant* list, size_t pos);
size_t tr_variantListSize(tr_variant const* list);

/***
****  Dictionaries
***/

static inline bool tr_variantIsDict(tr_variant const* v)
{
    return v != NULL && v->type == TR_VARIANT_TYPE_DICT;
}

void tr_variantInitDict(tr_variant* initme, size_t reserve_count);
void tr_variantDictReserve(tr_variant* dict, size_t reserve_count);
bool tr_variantDictRemove(tr_variant* dict, tr_quark const key);

tr_variant* tr_variantDictAdd(tr_variant* dict, tr_quark const key);
tr_variant* tr_variantDictAddReal(tr_variant* dict, tr_quark const key, double value);
tr_variant* tr_variantDictAddInt(tr_variant* dict, tr_quark const key, int64_t value);
tr_variant* tr_variantDictAddBool(tr_variant* dict, tr_quark const key, bool value);
tr_variant* tr_variantDictAddStr(tr_variant* dict, tr_quark const key, char const* value);
tr_variant* tr_variantDictAddQuark(tr_variant* dict, tr_quark const key, tr_quark const val);
tr_variant* tr_variantDictAddList(tr_variant* dict, tr_quark const key, size_t reserve_count);
tr_variant* tr_variantDictAddDict(tr_variant* dict, tr_quark const key, size_t reserve_count);
tr_variant* tr_variantDictSteal(tr_variant* dict, tr_quark const key, tr_variant* value);
tr_variant* tr_variantDictAddRaw(tr_variant* dict, tr_quark const key, void const* value, size_t len);

bool tr_variantDictChild(tr_variant* dict, size_t pos, tr_quark* setme_key, tr_variant** setme_value);
tr_variant* tr_variantDictFind(tr_variant* dict, tr_quark const key);
bool tr_variantDictFindList(tr_variant* dict, tr_quark const key, tr_variant** setme);
bool tr_variantDictFindDict(tr_variant* dict, tr_quark const key, tr_variant** setme_value);
bool tr_variantDictFindInt(tr_variant* dict, tr_quark const key, int64_t* setme);
bool tr_variantDictFindReal(tr_variant* dict, tr_quark const key, double* setme);
bool tr_variantDictFindBool(tr_variant* dict, tr_quark const key, bool* setme);
bool tr_variantDictFindStr(tr_variant* dict, tr_quark const key, char const** setme, size_t* len);
bool tr_variantDictFindRaw(tr_variant* dict, tr_quark const key, uint8_t const** setme_raw, size_t* setme_len);

/* this is only quasi-supported. don't rely on it too heavily outside of libT */
void tr_variantMergeDicts(tr_variant* dict_target, tr_variant const* dict_source);

/***
****
****
***/

/**
***
**/

/* @} */

#ifdef __cplusplus
}
#endif
