/* NOT BUILT — experiment, never shipped; vli_mult_comba_m68k.s is used.
 * Kept as reference only. */

/* unused_vli_karatsuba.c — one-level Karatsuba multiply for uECC P-256
 *
 * Replaces unused_vli_mult_m68k.s.  Compile with -m68020 -O3.
 * asm_mult=1 must still be set so uECC.c does not emit its own version.
 *
 * ── Algorithm ────────────────────────────────────────────────────────────
 *
 *   Split each 256-bit (8-word) operand into two 128-bit (4-word) halves:
 *
 *     left  = A·2^128 + B    (A = left[4..7],  B = left[0..3])
 *     right = C·2^128 + D    (C = right[4..7], D = right[0..3])
 *
 *   left × right = AC·2^256 + (AD+BC)·2^128 + BD
 *
 *   Karatsuba identity:  AD+BC = (A+B)(C+D) − AC − BD
 *
 *   Three 4×4 multiplies instead of one 8×8:
 *     lo  = BD
 *     hi  = AC
 *     mid = (A+B)(C+D) − lo − hi   =   AD+BC
 *
 *   result = hi·2^256 + mid·2^128 + lo
 *
 *   48 MULU.L vs 64 in the schoolbook → ~25% fewer multiplies.
 *
 * ── Carry handling ───────────────────────────────────────────────────────
 *
 *   A+B and C+D may overflow 4 words (carry ∈ {0,1}).  If carry_l = 1:
 *
 *     (A+B)(C+D) = sum_l_base · sum_r_base
 *                + carry_l · sum_r_base · 2^128
 *                + carry_r · sum_l_base · 2^128
 *                + carry_l · carry_r · 2^256
 *
 *   We compute the base 4×4 product, then fold in the carry terms.
 *   The final mid = AD+BC is non-negative and fits in 9 words (< 2^257).
 *   The 9th word (extra) is 0 or 1.
 */

#include <stdint.h>

/* ── 3-register (96-bit) accumulator muladd ─────────────────────────── */
/* With -O3 -m68020 this compiles to MULU.L + ADD.L + ADDX.L + ADDX.L. */

static void muladd3(uint32_t *r0, uint32_t *r1, uint32_t *r2,
                    uint32_t a, uint32_t b)
{
    uint64_t t = (uint64_t)a * b;
    uint64_t s = (uint64_t)*r0 + (uint32_t)t;
    *r0 = (uint32_t)s;
    s   = (uint64_t)*r1 + (uint32_t)(t >> 32) + (s >> 32);
    *r1 = (uint32_t)s;
    *r2 += (uint32_t)(s >> 32);
}

/* ── 4×4 word Comba schoolbook multiply → 8 words ───────────────────── */
/* 16 MULU.L, fully unrolled per diagonal.                               */

static void mul4(uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint32_t r0 = 0, r1 = 0, r2 = 0;

    /* k=0 */
    muladd3(&r0,&r1,&r2, a[0],b[0]);
    r[0]=r0; r0=r1; r1=r2; r2=0;

    /* k=1 */
    muladd3(&r0,&r1,&r2, a[0],b[1]);
    muladd3(&r0,&r1,&r2, a[1],b[0]);
    r[1]=r0; r0=r1; r1=r2; r2=0;

    /* k=2 */
    muladd3(&r0,&r1,&r2, a[0],b[2]);
    muladd3(&r0,&r1,&r2, a[1],b[1]);
    muladd3(&r0,&r1,&r2, a[2],b[0]);
    r[2]=r0; r0=r1; r1=r2; r2=0;

    /* k=3 */
    muladd3(&r0,&r1,&r2, a[0],b[3]);
    muladd3(&r0,&r1,&r2, a[1],b[2]);
    muladd3(&r0,&r1,&r2, a[2],b[1]);
    muladd3(&r0,&r1,&r2, a[3],b[0]);
    r[3]=r0; r0=r1; r1=r2; r2=0;

    /* k=4 */
    muladd3(&r0,&r1,&r2, a[1],b[3]);
    muladd3(&r0,&r1,&r2, a[2],b[2]);
    muladd3(&r0,&r1,&r2, a[3],b[1]);
    r[4]=r0; r0=r1; r1=r2; r2=0;

    /* k=5 */
    muladd3(&r0,&r1,&r2, a[2],b[3]);
    muladd3(&r0,&r1,&r2, a[3],b[2]);
    r[5]=r0; r0=r1; r1=r2; r2=0;

    /* k=6 */
    muladd3(&r0,&r1,&r2, a[3],b[3]);
    r[6] = r0;
    r[7] = r1;
    /* r2 == 0 guaranteed: 4×4 product < 2^256 */
}

/* ── 4-word add: r = a + b; return carry ∈ {0,1} ───────────────────── */

static uint32_t add4(uint32_t *r, const uint32_t *a, const uint32_t *b)
{
    uint64_t c = 0;
    int i;
    for (i = 0; i < 4; i++) {
        c += (uint64_t)a[i] + b[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

/* ── add src[0..n-1] into result[off..off+n-1], carry up to word 15 ── */

static void addinto(uint32_t *r, int off, const uint32_t *src, int n)
{
    uint64_t c = 0;
    int i;
    for (i = 0; i < n; i++) {
        c += (uint64_t)r[off + i] + src[i];
        r[off + i] = (uint32_t)c;
        c >>= 32;
    }
    for (i = off + n; c && i < 16; i++) {
        c += r[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
}

/* ── subtract src[0..7] from mid[0..7]; return signed borrow (0 or -1) */

static int32_t sub8(uint32_t *mid, const uint32_t *src)
{
    int64_t c = 0;
    int i;
    for (i = 0; i < 8; i++) {
        c += (int64_t)mid[i] - (int64_t)src[i];
        mid[i] = (uint32_t)c;
        c >>= 32;
    }
    return (int32_t)c;  /* 0 or -1 */
}

/* ── main entry point ────────────────────────────────────────────────── */

void uECC_vli_mult(uint32_t *result,
                   const uint32_t *left,
                   const uint32_t *right,
                   int8_t num_words)
{
    uint32_t lo[8], hi[8], mid[8];
    uint32_t sum_l[4], sum_r[4];
    uint32_t carry_l, carry_r;
    int32_t  extra;   /* 9th word of mid; always 0 or 1 when final */
    int i;

    (void)num_words;  /* always 8 for P-256 */

    /* ── 1. BD (low halves) ── */
    mul4(lo, left, right);

    /* ── 2. AC (high halves) ── */
    mul4(hi, left + 4, right + 4);

    /* ── 3. A+B and C+D (may carry into bit 128) ── */
    carry_l = add4(sum_l, left, left + 4);
    carry_r = add4(sum_r, right, right + 4);

    /* ── 4. (A+B)(C+D) — base 4×4 product, carry words ignored for now ── */
    mul4(mid, sum_l, sum_r);

    /* ── 5. Fold in overflow carries ──────────────────────────────────────
     *
     *  full (A+B)(C+D) = mid
     *                  + carry_l * sum_r * 2^128
     *                  + carry_r * sum_l * 2^128
     *                  + carry_l * carry_r * 2^256
     *
     *  The 2^128 terms extend mid[4..7]; any further carry becomes extra.
     *  The 2^256 term increments extra directly.
     */
    extra = 0;
    if (carry_l) {
        uint64_t c = 0;
        for (i = 0; i < 4; i++) {
            c += (uint64_t)mid[4 + i] + sum_r[i];
            mid[4 + i] = (uint32_t)c;
            c >>= 32;
        }
        extra += (int32_t)c;
    }
    if (carry_r) {
        uint64_t c = 0;
        for (i = 0; i < 4; i++) {
            c += (uint64_t)mid[4 + i] + sum_l[i];
            mid[4 + i] = (uint32_t)c;
            c >>= 32;
        }
        extra += (int32_t)c;
    }
    if (carry_l & carry_r) {
        extra++;  /* carry_l * carry_r * 2^256 term */
    }

    /* ── 6. mid = (A+B)(C+D) − BD − AC = AD+BC ─────────────────────────
     *
     *  Mathematically guaranteed: AD+BC ≥ 0 and < 2^257 (fits in 9 words).
     *  extra ends up 0 or 1.
     */
    extra += sub8(mid, lo);
    extra += sub8(mid, hi);

    /* ── 7. Assemble result ───────────────────────────────────────────────
     *
     *  result[0..7]  = lo   (BD)
     *  result[8..15] = hi   (AC)
     *  result[4..11] += mid (AD+BC)
     *  result[12]    += extra (the rare 9th word of mid)
     */
    for (i = 0; i < 8; i++) result[i]     = lo[i];
    for (i = 0; i < 8; i++) result[i + 8] = hi[i];

    addinto(result, 4, mid, 8);

    if (extra) {
        uint64_t c = (uint64_t)result[12] + (uint32_t)extra;
        result[12] = (uint32_t)c;
        for (i = 13; (c >> 32) && i < 16; i++) {
            c = (uint64_t)result[i] + 1;
            result[i] = (uint32_t)c;
        }
    }
}
