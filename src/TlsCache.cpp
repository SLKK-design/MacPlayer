/* TlsCache.cpp — on-disk storage for the TLS acceleration caches in
   tls_cache.h (DNS, ECDSA verify, EC midpoint, session ticket).

   Adapted from Claude-86's core/helpers/prefs_io.cpp, minus the app
   settings block: MacPlayer keeps its tokens in "MacPlayer Preferences"
   (Prefs.cpp), and these caches live in their own file so they can be
   thrown away without logging the user out.

   File layout ("MacPlayer TLS Cache" in the Preferences folder):
     [0]                CacheHeader           (magic + layout word)
     [kDNSOffset]       CachedDNS
     [kECDSAOffset]     PersistentECDSACacheEntry × PERSIST_ECDSA_SLOTS
     [kMidpointOffset]  PersistedECMidpoint
     [kTLSOffset]       PersistedTLSSession × kTLSSessionSlots
     [kSeedOffset]      PersistedRandomSeed   (not in the layout word)

   The layout word is derived from the struct sizes, so any struct change
   discards every block at once instead of leaving a half-valid file.
   Session slot 1 was added later without changing it: in older files it
   lies past the end, where reads fail like an empty slot.
   Everything here rebuilds itself on the next full handshake.          */

#include "tls_cache.h"

#include <Files.h>
#include <Folders.h>
#include <Script.h>
#include <DateTimeUtils.h>
#include <string.h>
#include <stdlib.h>

static const unsigned char kCacheFileName[] = "\pMacPlayer TLS Cache";

/* ------------------------------------------------------------------ */
/* GetCacheSpec                                                         */
/* ------------------------------------------------------------------ */
static FSSpec sCachedSpec;
static bool   sCachedSpecValid = false;

static OSErr GetCacheSpec(FSSpec &spec)
{
    if (sCachedSpecValid) { spec = sCachedSpec; return noErr; }
    short vRefNum; long dirID;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                           kCreateFolder, &vRefNum, &dirID);
    if (err != noErr) return err;
    err = FSMakeFSSpec(vRefNum, dirID, kCacheFileName, &spec);
    if (err == noErr || err == fnfErr) {
        sCachedSpec = spec; sCachedSpecValid = true; return noErr;
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* ReadFileAt / WriteFileAt                                            */
/* ------------------------------------------------------------------ */
static bool ReadFileAt(const FSSpec *spec, long offset, void *buf, long size)
{
    short ref;
    if (FSpOpenDF(spec, fsRdPerm, &ref) != noErr) return false;
    bool ok = false;
    if (SetFPos(ref, fsFromStart, offset) == noErr) {
        long count = size;
        OSErr err  = FSRead(ref, &count, buf);
        ok = (err == noErr || err == eofErr) && count == size;
    }
    FSClose(ref);
    return ok;
}

static void ExtendFileWithZeros(short ref, long needed)
{
    char zero[256];
    memset(zero, 0, sizeof(zero));
    SetFPos(ref, fsFromLEOF, 0);
    while (needed > 0) {
        long chunk = needed < (long)sizeof(zero) ? needed : (long)sizeof(zero);
        if (FSWrite(ref, &chunk, zero) != noErr) break;
        needed -= chunk;
    }
}

static bool WriteFileAt(const FSSpec *spec, long offset, const void *buf, long size)
{
    short ref;
    if (FSpOpenDF(spec, fsWrPerm, &ref) != noErr) {
        if (FSpCreate(spec, 'MPLY', 'pref', smSystemScript) != noErr) return false;
        if (FSpOpenDF(spec, fsWrPerm, &ref) != noErr) return false;
    }
    long fileSize = 0;
    GetEOF(ref, &fileSize);
    long needed = offset + size - fileSize;
    if (needed > 0) ExtendFileWithZeros(ref, needed);

    bool ok = false;
    if (SetFPos(ref, fsFromStart, offset) == noErr) {
        long count = size;
        ok = (FSWrite(ref, &count, buf) == noErr);
    }
    FSClose(ref);
    return ok;
}

/* ------------------------------------------------------------------ */
/* File layout + version header                                        */
/* ------------------------------------------------------------------ */
struct CacheHeader {
    unsigned long magic;    /* kCacheMagic */
    unsigned long layout;   /* CacheLayoutWord() — struct-size fingerprint */
};

enum {
    kDNSOffset      = (long)sizeof(CacheHeader),
    kECDSAOffset    = kDNSOffset      + (long)sizeof(CachedDNS),
    kMidpointOffset = kECDSAOffset    + (long)(sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS),
    kTLSOffset      = kMidpointOffset + (long)sizeof(PersistedECMidpoint),
    kSeedOffset     = kTLSOffset      + (long)(sizeof(PersistedTLSSession) * kTLSSessionSlots)
};

static const unsigned long kCacheMagic = 0x4D50544CUL;  /* 'MPTL' */

static unsigned long CacheLayoutWord()
{
    return ((unsigned long)sizeof(CachedDNS)          << 14)
         ^ ((unsigned long)(sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS) << 4)
         ^ ((unsigned long)sizeof(PersistedECMidpoint)         << 9)
         ^  (unsigned long)sizeof(PersistedTLSSession);
}

/* EnsureCacheHeader — checked once per launch. On magic/layout mismatch
   (no file yet, or a struct changed) truncate and write a fresh header. */
static bool sHeaderChecked = false;
static bool sHeaderOK      = false;

static bool EnsureCacheHeader(const FSSpec *spec)
{
    if (sHeaderChecked) return sHeaderOK;
    sHeaderChecked = true;

    short ref;
    if (FSpOpenDF(spec, fsRdWrPerm, &ref) != noErr) {
        if (FSpCreate(spec, 'MPLY', 'pref', smSystemScript) != noErr)
            return false;
        if (FSpOpenDF(spec, fsRdWrPerm, &ref) != noErr)
            return false;
    }

    CacheHeader h;
    memset(&h, 0, sizeof(h));   /* failed read must not leave garbage magic */
    long count = (long)sizeof(h);
    bool good = SetFPos(ref, fsFromStart, 0) == noErr &&
                FSRead(ref, &count, &h) == noErr &&
                count == (long)sizeof(h) &&
                h.magic == kCacheMagic && h.layout == CacheLayoutWord();

    if (!good) {
        SetEOF(ref, 0);
        h.magic  = kCacheMagic;
        h.layout = CacheLayoutWord();
        count = (long)sizeof(h);
        good = SetFPos(ref, fsFromStart, 0) == noErr &&
               FSWrite(ref, &count, &h) == noErr;
    }

    FSClose(ref);
    sHeaderOK = good;
    return good;
}

static OSErr GetCacheSpecChecked(FSSpec &spec)
{
    OSErr err = GetCacheSpec(spec);
    if (err != noErr) return err;
    if (!EnsureCacheHeader(&spec)) return ioErr;
    return noErr;
}

/* ------------------------------------------------------------------ */
/* DNS cache                                                           */
/* ------------------------------------------------------------------ */
void LoadDNSCache(CachedDNS &out)
{
    out.ip = 0; out.secs_written = 0;
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    if (!ReadFileAt(&spec, kDNSOffset, &out, (long)sizeof(CachedDNS)))
        out.ip = 0;
}

void SaveDNSCache(const CachedDNS &in)
{
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    WriteFileAt(&spec, kDNSOffset, &in, (long)sizeof(CachedDNS));
}

/* ------------------------------------------------------------------ */
/* Persistent ECDSA verify cache                                       */
/* ------------------------------------------------------------------ */
void LoadPersistECDSACache(PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS])
{
    memset(slots, 0, sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS);
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    long ecdsaSz = (long)sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS;
    if (!ReadFileAt(&spec, kECDSAOffset, slots, ecdsaSz))
        memset(slots, 0, (size_t)ecdsaSz);
}

void SavePersistECDSACache(const PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS])
{
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    WriteFileAt(&spec, kECDSAOffset, slots,
                (long)sizeof(PersistentECDSACacheEntry) * PERSIST_ECDSA_SLOTS);
}

/* ------------------------------------------------------------------ */
/* EC midpoint cache (OPT #7)                                          */
/* ------------------------------------------------------------------ */
void LoadECMidpoint(PersistedECMidpoint &out)
{
    memset(&out, 0, sizeof(out));
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    if (!ReadFileAt(&spec, kMidpointOffset, &out, (long)sizeof(PersistedECMidpoint)))
        memset(&out, 0, sizeof(out));
}

void SaveECMidpoint(const PersistedECMidpoint &in)
{
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    WriteFileAt(&spec, kMidpointOffset, &in, (long)sizeof(PersistedECMidpoint));
}

/* ------------------------------------------------------------------ */
/* TLS session ticket persistence                                      */
/* ------------------------------------------------------------------ */
static long TLSSlotOffset(int slot)
{
    return kTLSOffset + (long)slot * (long)sizeof(PersistedTLSSession);
}

void SaveTLSSession(const PersistedTLSSession& sess, int slot)
{
    if (slot < 0 || slot >= kTLSSessionSlots) return;
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    WriteFileAt(&spec, TLSSlotOffset(slot), &sess, (long)sizeof(PersistedTLSSession));
}

bool LoadTLSSession(PersistedTLSSession& sess, int slot)
{
    if (slot < 0 || slot >= kTLSSessionSlots) return false;
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return false;
    memset(&sess, 0, sizeof(sess));
    if (!ReadFileAt(&spec, TLSSlotOffset(slot), &sess, (long)sizeof(PersistedTLSSession)))
        return false;
    if (sess.magic != 0x544C5353UL) return false;
    if (sess.blob_len == 0 || sess.blob_len > kTLSBlobMax) return false;
    unsigned long now = 0;
    GetDateTime(&now);
    if (sess.saved_datetime == 0 || now - sess.saved_datetime > 23UL * 3600UL) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Random seed                                                         */
/* ------------------------------------------------------------------ */
static const unsigned long kSeedMagic = 0x53656564UL;  /* 'Seed' */

bool LoadRandomSeed(unsigned char seed[kRandomSeedLen])
{
    PersistedRandomSeed s;
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return false;
    if (!ReadFileAt(&spec, kSeedOffset, &s, (long)sizeof(s))) return false;
    if (s.magic != kSeedMagic) return false;
    memcpy(seed, s.seed, kRandomSeedLen);
    memset(&s, 0, sizeof(s));
    return true;
}

void SaveRandomSeed(const unsigned char seed[kRandomSeedLen])
{
    PersistedRandomSeed s;
    FSSpec spec;
    if (GetCacheSpecChecked(spec) != noErr) return;
    s.magic = kSeedMagic;
    memcpy(s.seed, seed, kRandomSeedLen);
    WriteFileAt(&spec, kSeedOffset, &s, (long)sizeof(s));
    memset(&s, 0, sizeof(s));
}

/* ------------------------------------------------------------------ */
/* C-callable wrappers for uecc_alt.c                                  */
/* ------------------------------------------------------------------ */
extern "C" void SaveAllPersistECDSASlots_c(const unsigned char *q_pub_flat,
                                            const unsigned char *hash_flat,
                                            const unsigned char *hash_lens,
                                            const unsigned char *valids,
                                            int num_slots)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    int n = num_slots < PERSIST_ECDSA_SLOTS ? num_slots : PERSIST_ECDSA_SLOTS;
    memset(slots, 0, sizeof(slots));
    for (int i = 0; i < n; i++) {
        memcpy(slots[i].q_pub,    q_pub_flat + i * 64, 64);
        memcpy(slots[i].msg_hash, hash_flat  + i * 64, 32);  /* uecc_alt hash rows are 64 wide */
        slots[i].hash_len = hash_lens[i];
        slots[i].valid    = valids[i];
    }
    SavePersistECDSACache(slots);
}

extern "C" void LoadAllPersistECDSASlots_c(unsigned char *q_pub_flat,
                                            unsigned char *hash_flat,
                                            unsigned char *hash_lens,
                                            unsigned char *valids,
                                            int num_slots)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    LoadPersistECDSACache(slots);
    int n = num_slots < PERSIST_ECDSA_SLOTS ? num_slots : PERSIST_ECDSA_SLOTS;
    for (int i = 0; i < n; i++) {
        memcpy(q_pub_flat + i * 64, slots[i].q_pub,    64);
        memcpy(hash_flat  + i * 64, slots[i].msg_hash, 32);
        hash_lens[i] = slots[i].hash_len;
        valids[i]    = slots[i].valid;
    }
}

extern "C" void LoadECMidpoint_c(unsigned char q_pub_out[64],
                                  unsigned char q2_native_out[64],
                                  unsigned char *valid_out)
{
    PersistedECMidpoint mid;
    LoadECMidpoint(mid);
    memcpy(q_pub_out,     mid.q_pub,     64);
    memcpy(q2_native_out, mid.q2_native, 64);
    *valid_out = mid.valid;
}

extern "C" void SaveECMidpoint_c(const unsigned char q_pub_in[64],
                                  const unsigned char q2_native_in[64])
{
    PersistedECMidpoint mid;
    memset(&mid, 0, sizeof(mid));
    memcpy(mid.q_pub,     q_pub_in,     64);
    memcpy(mid.q2_native, q2_native_in, 64);
    mid.valid = 1;
    SaveECMidpoint(mid);
}

extern "C" void LoadPersistECDSASlot_c(int slot,
                                        unsigned char q_pub_out[64],
                                        unsigned char hash_out[32],
                                        unsigned char *hash_len_out,
                                        unsigned char *valid_out)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    LoadPersistECDSACache(slots);
    if (slot >= 0 && slot < PERSIST_ECDSA_SLOTS) {
        memcpy(q_pub_out, slots[slot].q_pub, 64);
        memcpy(hash_out,  slots[slot].msg_hash, 32);
        *hash_len_out = slots[slot].hash_len;
        *valid_out    = slots[slot].valid;
    } else {
        memset(q_pub_out, 0, 64); memset(hash_out, 0, 32);
        *hash_len_out = 0; *valid_out = 0;
    }
}

extern "C" void SavePersistECDSASlot_c(int slot,
                                        const unsigned char q_pub_in[64],
                                        const unsigned char hash_in[32],
                                        unsigned char hash_len_in)
{
    PersistentECDSACacheEntry slots[PERSIST_ECDSA_SLOTS];
    LoadPersistECDSACache(slots);
    if (slot >= 0 && slot < PERSIST_ECDSA_SLOTS) {
        memcpy(slots[slot].q_pub,    q_pub_in, 64);
        memcpy(slots[slot].msg_hash, hash_in,  32);
        slots[slot].hash_len = hash_len_in;
        slots[slot].valid    = 1;
    }
    SavePersistECDSACache(slots);
}
