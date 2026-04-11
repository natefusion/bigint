#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdbit.h>
#include <assert.h>
#include <math.h>
#include "randq.h"
#include "hr_timer.h"

typedef int64_t i64;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

typedef union {
    // Big end first
    u64 d[3];
    struct {u64 d0; u64 d1; u64 d2;};
    struct {u64 hi; u64 mi; u64 lo;};
} u192;

typedef struct {
    char *data;
    u64 len;
} str;

#define MAX_U64 0xFFFFFFFFFFFFFFFFULL
#define MAX_U192 (u192) {.d0=MAX_U64,.d1=MAX_U64,.d2=MAX_U64}
#define MAX_I192 (u192) {.d0=0x7FFFFFFFFFFFFFFFULL,.d1=MAX_U64,.d2=MAX_U64}
#define MIN_I192 (u192) {.d0=0x8000000000000000ULL,.d1=0ULL,.d2=0ULL}
#define str_lit(x) (str) { .data = x, .len = sizeof(x) - 1 }

#define U192(x) ((u192){.d2=(u64)(x)})
#define I192(x) ((i64)(x) < 0 ? neg_u192((u192){.d2=(u64)(i64)(-x)}) : (u192){.d2=(u64)(x)})
#define ZERO ((u192){0})

void print_u192(u192 x) {
    printf(".d0 = %lX\n.d1 = %lX\n.d2 = %lX\n", x.d0, x.d1, x.d2);
}

void print_big_u192(u192 x) {
    printf("%016lX %016lX %016lX", x.d0, x.d1, x.d2);
}

typedef struct {
    u64 res;
    u64 carry;
} u64_carry;

u64_carry adc_u64(u64 x, u64 y, bool c) {
    u64 result = x + y + c;
    bool carry = result <= x;
    return (u64_carry) { .res = result, .carry = carry };
}

u64_carry sbb_u64(u64 x, u64 y, bool b) {
    u64 result = x - (y + b);
    bool borrow = result > x;
    return (u64_carry) { .res = result, .carry = borrow };
}

u192 add_u192(u192 m, u192 n) {
    u64_carry d = adc_u64(m.d2, n.d2, 0);
    u64_carry c = adc_u64(m.d1, n.d1, d.carry);
    u64_carry b = adc_u64(m.d0, n.d0, c.carry);

    return (u192) {
        .d2 = d.res,
        .d1 = c.res,
        .d0 = b.res,
    };
}

u192 sub_u192(u192 m, u192 n) {
    u64_carry d = sbb_u64(m.d2, n.d2, 0);
    u64_carry c = sbb_u64(m.d1, n.d1, d.carry);
    u64_carry b = sbb_u64(m.d0, n.d0, c.carry);

    return (u192) {
        .d2 = d.res,
        .d1 = c.res,
        .d0 = b.res,
    };
}

u192 neg_u192(u192 x) {
    return sub_u192(U192(0), x);
}

u192 logshl_u192(u192 x, u64 s) {
    if (s == 0) return x;
    if (s >= 192) return (u192){0};

    u192 out = (u192){0};

    u64 right_part_len = 0;
    int a = 0;
    if (s < 64) {
        right_part_len = 64 - s;
        a = 0;
    } else if (s < 128) {
        right_part_len = 128 - s;
        a = 1;
    } else if (s < 192) {
        right_part_len = 192 - s;
        a = 2;
    } else {
        // dead path
    }

    u64 bm = (right_part_len==64 ? ~0ULL : ((1ULL<<right_part_len)-1));

    for (int i = 0; i < 3; ++i) {
        int bit_idx = 3 - i - 1;
        u64 right_part = (x.d[bit_idx] & bm) << (s % 64);
        u64 left_part = (x.d[bit_idx] & (~bm)) >> (right_part_len % 64);
        if (bit_idx-a >= 0) {
            out.d[bit_idx-a] |= right_part;
            if (bit_idx-a-1 >= 0) out.d[bit_idx-a-1] |= left_part;
        }
    }

    return out;
}

u192 logshr_u192(u192 x, u64 s) {
    if (s == 0) return x;
    if (s >= 192) return (u192){0};

    u192 out = (u192){0};

    u64 left_part_len = 0;
    int a = 0;
    if (s < 64) {
        left_part_len = 64 - s;
        a = 0;
    } else if (s < 128) {
        left_part_len = 128 - s;
        a = 1;
    } else if (s < 192) {
        left_part_len = 192 - s;
        a = 2;
    } else {
        // dead path
    }

    u64 bm = (left_part_len==64 ? ~0ULL : ((1ULL<<left_part_len)-1)<<s);

    for (int i = 0; i < 3; ++i) {
        int bit_idx = i;
        u64 left_part = (x.d[bit_idx] & bm) >> (s % 64);
        u64 right_part = (x.d[bit_idx] & (~bm)) << (left_part_len % 64);
        if (bit_idx+a < 3) {
            out.d[bit_idx+a] |= left_part;
            if (bit_idx+a+1 < 3) out.d[bit_idx+a+1] |= right_part;
        }
    }

    return out;
}

u192 bitand_u192(u192 m, u192 n) {
    return (u192) {
        .d0 = m.d0 & n.d0,
        .d1 = m.d1 & n.d1,
        .d2 = m.d2 & n.d2,
    };
}

bool get_bit(u64 x, u64 idx) {
    return (x >> idx) & 1ULL;
}

bool set_bit(u64 x, u64 idx) {
    return x | (1ULL << idx);
}

bool reset_bit(u64 x, u64 idx) {
    return x & ~(1ULL << idx);
}

bool sign_i192(u192 x) {
    return get_bit(x.d0, 63);
}

u192 set_sign_literally_i192(u192 x, bool on) {
    x.d0 = on ? set_bit(x.d0, 63) : reset_bit(x.d0, 63);
    return x;
}


bool eq_u192(u192 m, u192 n) {
    bool a = m.d0 == n.d0;
    bool b = m.d1 == n.d1;
    bool c = m.d2 == n.d2;
    return a && b && c;
}

bool gt_u192(u192 m, u192 n) {
    if (m.d0 > n.d0) {
        return true;
    } else if (m.d0 == n.d0) {
        if (m.d1 > n.d1) {
            return true;
        } else if (m.d1 == n.d1) {
            if (m.d2 > n.d2) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }
}


bool lt_u192(u192 m, u192 n) {
    if (m.d0 < n.d0) {
        return true;
    } else if (m.d0 == n.d0) {
        if (m.d1 < n.d1) {
            return true;
        } else if (m.d1 == n.d1) {
            if (m.d2 < n.d2) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }
}

bool ge_u192(u192 m, u192 n) { return !lt_u192(m, n); }
bool le_u192(u192 m, u192 n) { return !gt_u192(m, n); }
bool ne_u192(u192 m, u192 n) { return !eq_u192(m, n); }

bool gt_i192(u192 m, u192 n) {
    bool sm = sign_i192(m);
    bool sn = sign_i192(n);
    if (!sm && sn) {
        return true;
    } else if (sm && !sn) {
        return false;
    } else if (!sm && !sn) {
        return gt_u192(m, n);
    } else {
        return gt_u192(set_sign_literally_i192(m, false), set_sign_literally_i192(n, false));
    }
}

bool lt_i192(u192 m, u192 n) {
    bool sm = sign_i192(m);
    bool sn = sign_i192(n);
    if (!sm && sn) {
        return false;
    } else if (sm && !sn) {
        return true;
    } else if (!sm && !sn) {
        return lt_u192(m, n);
    } else {
        return lt_u192(set_sign_literally_i192(m, false), set_sign_literally_i192(n, false));
    }
}

bool ge_i192(u192 m, u192 n) { return !lt_i192(m, n); }
bool le_i192(u192 m, u192 n) { return !gt_i192(m, n); }


static inline void mulq(u64 *high, u64 *low, u64 x, u64 y) {
    asm ("mulq %[y]" 
          : "=d" (*high), "=a" (*low)
          : "a" (x), [y] "rm" (y)    
        );
}

u192 mul_naive_u192(u192 m, u192 n) {
    u192 result = {0};

    u64 carry = 0;
    u64 mul = 0;

    mulq(&carry, &mul, m.d2, n.d2);
    result.d2 = mul;
    result.d1 = carry;


    mulq(&carry, &mul, m.d1, n.d2);
    {
        u64_carry d1 = adc_u64(result.d1, mul, 0);
        result.d1 = d1.res;
        result.d0 += d1.carry + carry;
    }

    mulq(&carry, &mul, m.d0, n.d2);
    result.d0 += mul;

    /* ----------------------- */

    mulq(&carry, &mul, m.d2, n.d1);
    {
        u64_carry d1 = adc_u64(result.d1, mul, 0);
        result.d1 = d1.res;
        result.d0 += d1.carry + carry;
    }

    mulq(&carry, &mul, m.d1, n.d1);
    result.d0 += mul;

    /* ----------------------- */

    mulq(&carry, &mul, m.d2, n.d0);
    result.d0 += mul;

    return result;
}

u192 pow_naive_u192(u192 base, u64 power) {
    u192 result = {.d2=1};

    for (u64 i = 0; i < power; ++i) {
        result = mul_naive_u192(base, result);
    }

    return result;
}

void div_full_unsigned_u192(u192 m, u192 n, u192 *q, u192 *r) {
    if (eq_u192((u192){0}, n)) {
        printf("DIVISION BY ZERO HAHAHAHAH\n");
        u192 result = {0};
        if (q) *q = result;
        if (r) *r = result;
        return;
    }
    
    u192 num = m;
    u192 dem = n;
    u192 quot = {0};
    u192 rem = {0};
    for (u64 i = 0; i < 192; ++i) {
        u64 j = 191 - i;
        rem = mul_naive_u192(rem, (u192){.d2=2});
        rem.d2 |= (num.d[i/64] >> (j % 64)) & 1ULL;
        if (ge_u192(rem, dem)) {
            rem = sub_u192(rem, dem);
            quot.d[i/64] |= 1ULL << (j % 64);
        }
    }
    
    if (q) *q = quot;
    if (r) *r = rem;
    
    return;
}

void div_full_signed_u192(u192 m, u192 n, u192 *q, u192 *r) {
    if (eq_u192((u192){0}, n)) {
        printf("DIVISION BY ZERO HAHAHAHAH\n");
        u192 result = {0};
        if (q) *q = result;
        if (r) *r = result;
        return;
    }

    // C-like signed division: quotient truncates toward zero, remainder has the
    // same sign as the numerator (or is zero).
    bool sm = sign_i192(m);
    bool sn = sign_i192(n);

    // Work on unsigned magnitudes.
    u192 am = sm ? neg_u192(m) : m;
    u192 an = sn ? neg_u192(n) : n;

    u192 q0 = {0};
    u192 r0 = {0};
    div_full_unsigned_u192(am, an, &q0, &r0);

    if (sm ^ sn) q0 = neg_u192(q0);
    if (sm) r0 = neg_u192(r0);

    if (q) *q = q0;
    if (r) *r = r0;
    return;
}


u192 div_naive_u192(u192 m, u192 n) {
    u192 result = {0};
    u192 other = {0};
    div_full_unsigned_u192(m, n, &result, &other);
    return result;
}

u192 mod_naive_u192(u192 m, u192 n) {
    u192 result = {0};
    u192 other = {0};
    div_full_unsigned_u192(m, n, &other, &result);
    return result;
}

u192 div_naive_signed_u192(u192 m, u192 n) {
    u192 result = {0};
    u192 other = {0};
    div_full_signed_u192(m, n, &result, &other);
    return result;
}

u192 mod_naive_signed_u192(u192 m, u192 n) {
    u192 result = {0};
    u192 other = {0};
    div_full_signed_u192(m, n, &other, &result);
    return result;
}

u64 log2_u192(u192 x) {
    if (x.d0 != 0) {
        return __builtin_stdc_bit_width(x.d0) + 128;
    } else if (x.d1 != 0) {
        return __builtin_stdc_bit_width(x.d1) + 64;
    } else {
        return __builtin_stdc_bit_width(x.d2);
    }
}


u192 make_u192(str s) {
    u192 result = {0};
    for (u64 i = 0; i < s.len; ++i) {
        u192 x = pow_naive_u192((u192){.d2=10}, s.len - i - 1);
        u192 y = {.d2=(u64)(s.data[i] - '0')};
        u192 z = mul_naive_u192(x, y);
        result = add_u192(result, z);
    }
    return result;
}

str tostr_u192(u192 m) {
    str s;
    s.len = 58;
    s.data = (char*)calloc(s.len+1, sizeof(char));

    for (u64 i = 0; i < s.len; ++i) {
        u192 x = pow_naive_u192(U192(10), i);
        u192 y = div_naive_u192(m, x);
        u192 z = mod_naive_u192(y, U192(10));
        s.data[s.len - 2 - i] = '0' + (char)z.d2;
    }

    s.data[s.len-1]=0;

    return s;
}

str tostr_i192(u192 m) {
    bool sign = sign_i192(m);
    if (sign) {
        m = neg_u192(m);
    }

    u64 digits = 58;

    str s;
    s.len = digits + 1;
    s.data = (char*)calloc(s.len+2, sizeof(char));

    for (u64 i = 0; i < digits; ++i) {
        u192 x = pow_naive_u192(U192(10), i);
        u192 y = div_naive_u192(m, x);
        u192 z = mod_naive_u192(y, U192(10));
        s.data[s.len - 2 - i] = '0' + (char)z.d2;
    }

    s.data[s.len-1]=0;
    s.data[0] = sign ? '-' : '+';

    return s;
}

u192 DT3_u192(u192 m, u192 n) {
    constexpr u64 b = 32;
    // poles 0, 1, -1, 2, inf
    //b = 2^32
    //B = b^i = 2^32
    //The largest number that can be multiplied properly has 96 set bits
    u192 m2 = (u192){.d2=m.d1 & 0x00000000FFFFFFFFULL};
    u192 m1 = (u192){.d2=(m.d2 & 0xFFFFFFFF00000000ULL) >> 32ULL};
    u192 m0 = (u192){.d2=m.d2 & 0x00000000FFFFFFFFULL};

    u192 n2 = (u192){.d2=n.d1 & 0x00000000FFFFFFFFULL};
    u192 n1 = (u192){.d2=(n.d2 & 0xFFFFFFFF00000000ULL) >> 32ULL};
    u192 n0 = (u192){.d2=n.d2 & 0x00000000FFFFFFFFULL};

    u192 t1, t2;
    
    // p
    u192 p0 = m0;
    
    t1 = add_u192(m0, m1);
    u192 p1 = add_u192(t1, m2);

    t1 = sub_u192(m0, m1);
    u192 p_1 = add_u192(t1, m2);

    t1 = logshr_u192(m1, 1);
    t1 = add_u192(m0, t1);
    t2 = logshr_u192(m2, 2);
    u192 p2 = add_u192(t2, t1);

    u192 pinf = m2;
    // End p

    // q
    u192 q0 = n0;
    
    t1 = add_u192(n0, n1);
    u192 q1 = add_u192(t1, n2);

    t1 = sub_u192(n0, n1);
    u192 q_1 = add_u192(t1, n2);

    t1 = logshr_u192(n1, 1);
    t1 = add_u192(n0, t1);
    t2 = logshr_u192(n2, 2);
    u192 q2 = add_u192(t2, t1);

    u192 qinf = n2;
    // End q

    // r
    u192 r0 = mul_naive_u192(p0,q0);
    u192 r1 = mul_naive_u192(p1,q1);
    u192 r_1 = mul_naive_u192(p_1,q_1);
    u192 r2 = mul_naive_u192(p2,q2);
    u192 rinf = mul_naive_u192(pinf,qinf);
    // end r

    // R
    u192 R0 = mul_naive_u192(U192(6), r0);

    {
        t1 = mul_naive_u192(I192(-3), r0);
        t2 = mul_naive_u192(U192(6), r1);
        t2 = add_u192(t1, t2);
    
        t1 = mul_naive_u192(I192(-2), r_1);
        t2 = add_u192(t2, t1);
    
        t1 = neg_u192(r2);
        t2 = add_u192(t2, t1);
    
        t1 = mul_naive_u192(U192(12), rinf);
    }
    u192 R1 = add_u192(t2, t1);

    {    
        t1 = mul_naive_u192(I192(-6), r0);
        t2 = mul_naive_u192(U192(3), r1);
        t2 = add_u192(t1, t2);
    
        t1 = mul_naive_u192(U192(3), r_1);
        t2 = add_u192(t2, t1);
    
        t1 = mul_naive_u192(I192(-6), rinf);
    }
    u192 R2 = add_u192(t2, t1);

    {
        t1 = mul_naive_u192(U192(3), r0);
        t2 = mul_naive_u192(I192(-3), r1);
        t2 = add_u192(t1, t2);
    
        t1 = neg_u192(r_1);
        t2 = add_u192(t2, t1);
    
        t2 = add_u192(t2, r2);
    
        t1 = mul_naive_u192(I192(-12), rinf);
    }
    u192 R3 = add_u192(t2, t1);

    u192 R4 = mul_naive_u192(U192(6), rinf);

    R1 = logshl_u192(R1, b*1);
    R2 = logshl_u192(R2, b*2);
    R3 = logshl_u192(R3, b*3);
    R4 = logshl_u192(R4, b*4);
    // end R

    u192 out = add_u192(R0, R1);
    out = add_u192(out, R2);
    out = add_u192(out, R3);
    out = add_u192(out, R4);

    return out;
}

u192 DMMM_u192(u192 m, u192 n) {
    // R = 2^102
    // N = 2^96/6
    u64 logR = 102;
    u192 R1 = U192((1ULL << logR) - 1);
    u192 N = (u192){.d1=0x000000002AAAAAAA,
                    .d2=0xAAAAAAAAAAAAAAAB};
    u192 N_ = U192(511);
    
    u192 T = DT3_u192(m, n);
    u192 s = DT3_u192(bitand_u192(T, R1), N_);
    u192 t = DT3_u192(bitand_u192(s, R1), N);
    u192 z = mul_naive_u192(U192(9), T);
    z = add_u192(z, t);
    z = logshr_u192(z, logR);

    return z;
}

u192 mul_toomcook_u192(u192 m, u192 n) {
    u64 log2b = 1;
    // poles 0, 1, -1, 2, inf
    // b = 2^16
    // B = b^i
    // The largest number that can be multiplied properly has 96 set bits
    u64 dm = (log2_u192(m) /log2b) / 3;
    u64 dn = (log2_u192(n) /log2b) / 3;
    u64 i = (dm > dn ? dm : dn) + 1;

    u64 bits_per_digit = i*log2b;
    
    // Split into 3 base-(2^bits_per_digit) digits:
    // m = m0 + m1*B + m2*B^2, n = n0 + n1*B + n2*B^2, where B = 2^bits_per_digit.
    u64 bpd = bits_per_digit;
    if (bpd > 192) bpd = 192;

    u192 digit_mask = ZERO;
    if (bpd == 192) {
        digit_mask = MAX_U192;
    } else if (bpd != 0) {
        digit_mask = sub_u192(logshl_u192(U192(1), bpd), U192(1));
    }

    u192 m0 = bitand_u192(m, digit_mask);
    u192 m1 = bitand_u192(logshr_u192(m, bpd), digit_mask);
    u192 m2 = bitand_u192(logshr_u192(m, 2 * bpd), digit_mask);

    u192 n0 = bitand_u192(n, digit_mask);
    u192 n1 = bitand_u192(logshr_u192(n, bpd), digit_mask);
    u192 n2 = bitand_u192(logshr_u192(n, 2 * bpd), digit_mask);

    u192 t1, t2;
    
    // p
    u192 p0 = m0;

    t1 = add_u192(m0, m1);
    u192 p1 = add_u192(t1, m2);

    t1 = sub_u192(m0, m1);
    u192 p_1 = add_u192(t1, m2);

    t1 = mul_naive_u192(m1, U192(2));
    t1 = add_u192(m0, t1);
    t2 = mul_naive_u192(m2, U192(4));
    u192 p2 = add_u192(t2, t1);

    u192 pinf = m2;
    // End p

    // q
    u192 q0 = n0;
    
    t1 = add_u192(n0, n1);
    u192 q1 = add_u192(t1, n2);

    t1 = sub_u192(n0, n1);
    u192 q_1 = add_u192(t1, n2);

    t1 = mul_naive_u192(n1, U192(2));
    t1 = add_u192(n0, t1);
    t2 = mul_naive_u192(n2, U192(4));
    u192 q2 = add_u192(t2, t1);

    u192 qinf = n2;
    // End q

    // r
    u192 r0 = mul_naive_u192(p0,q0);
    u192 r1 = mul_naive_u192(p1,q1);
    u192 r_1 = mul_naive_u192(p_1,q_1);
    u192 r2 = mul_naive_u192(p2,q2);
    u192 rinf = mul_naive_u192(pinf,qinf);
    // end r

    // R
    u192 R0 = r0;

    {// R1 = (-1/2)*r0 + (1)*r1 + (-1/3)*r_1 + (-1/6)*r2 + (2)*rinf
        t1 = neg_u192(div_naive_signed_u192(r0, U192(2)));
        t2 = add_u192(t1, r1);

        t1 = neg_u192(div_naive_signed_u192(r_1, U192(3)));
        t2 = add_u192(t2, t1);
    
        t1 = neg_u192(div_naive_signed_u192(r2, U192(6)));
        t2 = add_u192(t2, t1);
    
        t1 = mul_naive_u192(rinf, U192(2));
    }
    u192 R1 = add_u192(t2, t1);

    {// R2 = (-1)*r0 + (1/2)*r1 + (1/2)*r_1 + (0)*r2 + (-1)*rinf
        t1 = neg_u192(r0);
        
        t2 = div_naive_signed_u192(r1, U192(2));
        t2 = add_u192(t1, t2);
    
        t1 = div_naive_signed_u192(r_1, U192(2));
        t2 = add_u192(t2, t1);
    
        t1 = neg_u192(rinf);
    }
    u192 R2 = add_u192(t2, t1);

    {// R3 = (1/2)*r0 + (-1/2)*r1 + (-1/6)*r_1 + (1/6)*r2 + (-2)*inf 
        t1 = div_naive_signed_u192(r0, U192(2));
        t2 = neg_u192(div_naive_signed_u192(r1, U192(2)));
        t2 = add_u192(t1, t2);

        t1 = neg_u192(div_naive_signed_u192(r_1, U192(6)));
        t2 = add_u192(t2, t1);

        t1 = div_naive_signed_u192(r2, U192(6));
        t2 = add_u192(t2, t1);
    
        t1 = neg_u192(div_naive_signed_u192(rinf, U192(2)));
    }
    u192 R3 = add_u192(t2, t1);

    u192 R4 = rinf;

    R1 = logshl_u192(R1, log2b*1);
    R2 = logshl_u192(R2, log2b*2);
    R3 = logshl_u192(R3, log2b*3);
    R4 = logshl_u192(R4, log2b*4);
    // end R

    u192 out = add_u192(R0, R1);
    out = add_u192(out, R2);
    out = add_u192(out, R3);
    out = add_u192(out, R4);

    return out;
}

struct unit_test_type {
    u192 m;
    u192 n;
    u192 result;
};

typedef struct {
    struct unit_test_type *data;
    u64 len;

} unit_test_array_type;


void unit_test(str which, u192 (*func)(u192, u192), unit_test_array_type tests) {
    printf("%s tests\n", which.data);
    printf("---------------\n");

    u64 correct = 0;
    for (u64 i = 0; i < tests.len; ++i) {
        u192 result = func(tests.data[i].m, tests.data[i].n);
        printf("Test %lu\n------\n", i+1);
        printf("   d0               d1               d2\n");
        printf("m: "); print_big_u192(tests.data[i].m); putchar('\n');
        printf("n: "); print_big_u192(tests.data[i].n); putchar('\n');
        printf("   --------------------------------------------------\n");
        printf("   "); print_big_u192(result);
        printf(" (Got)\n");
        printf("   "); print_big_u192(tests.data[i].result);
        printf(" (Expected)\n");
        printf("   d0               d1               d2\n");

        bool is_correct = eq_u192(result, tests.data[i].result);
        if (is_correct)
            printf("Passed!\n");
        else
            printf("Did not pass!\n");
        printf("\n");
        correct += is_correct;
    }

    printf("%lu / %lu tests passed for %s\n", correct, tests.len, which.data);
}

void profile(str which, u192 (*func)(u192, u192)) {
    printf("Profiling %s...\n", which.data);
    u64 times_to_run = 0xFFFFFFFULL;

    u192 total_time = {0};
    for (u64 i = 0; i < times_to_run; ++i) {
        u192 m = {
            .d0 = randq64_uint64(),
            .d1 = randq64_uint64(),
            .d2 = randq64_uint64(),
        };
        
        u192 n = {
            .d0 = randq64_uint64(),
            .d1 = randq64_uint64(),
            .d2 = randq64_uint64(),
        };


        u64 start = ns();
        func(m, n);
        u64 end = ns();

        total_time = add_u192(total_time, (u192){.d2 = end - start});

        if (i % 100000ULL == 0) {
            printf("%s has run %lu times\n", which.data, i+1);
        }
    }

    u192 avg_time = div_naive_u192(total_time, (u192){.d2=times_to_run});
    str total_time_str = tostr_u192(total_time);
    str avg_time_str = tostr_u192(avg_time);
    printf("It took %s ns to run %s %lu times for an average of %s ns per call\n", total_time_str.data, which.data, times_to_run, avg_time_str.data);
    
    free(total_time_str.data);
    free(avg_time_str.data);
}

void unit_test_mul_u192(void) {
    unit_test_array_type mul_naive_u192_tests;
    {
        static struct unit_test_type d[] = {
            {
                .m = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0,
                },
                .n = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0,
                },
                .result = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0,
                },
            },
            {
                .m = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = MAX_U64,
                },
                .n = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = MAX_U64,
                },
                .result = {
                    .d0 = 0,
                    .d1 = MAX_U64 - 1,
                    .d2 = 1,
                },
            },
            {
                .m = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = MAX_U64,
                },
                .n = {
                    .d0 = 0,
                    .d1 = MAX_U64,
                    .d2 = MAX_U64,
                },
                .result = {
                    .d0 = MAX_U64 - 1,
                    .d1 = MAX_U64,
                    .d2 = 1,
                },
            },
            {
                .m = {
                    .d0 = MAX_U64,
                    .d1 = MAX_U64,
                    .d2 = MAX_U64,
                },
                .n = {
                    .d0 = MAX_U64,
                    .d1 = MAX_U64,
                    .d2 = MAX_U64,
                },
                .result = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 1,
                },
            },
            {
                .m = {
                    .d0 = MAX_U64,
                    .d1 = MAX_U64,
                    .d2 = MAX_U64-3,
                },
                .n = {
                    .d0 = MAX_U64,
                    .d1 = MAX_U64,
                    .d2 = MAX_U64,
                },
                .result = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 4,
                },
            },
            {
                .m = {
                    .d0 = MAX_U64-3,
                    .d1 = MAX_U64,
                    .d2 = MAX_U64,
                },
                .n = {
                    .d0 = MAX_U64,
                    .d1 = MAX_U64,
                    .d2 = MAX_U64,
                },
                .result = {
                    .d0 = 3,
                    .d1 = 0,
                    .d2 = 1,
                },
            },
            {
                .m = {
                    .d0 = 0x289043,
                    .d1 = 0x1291ab,
                    .d2 = 0x895903,
                },
                .n = {
                    .d0 = 0xabcd2f,
                    .d1 = 0xcd2249,
                    .d2 = 0xbed710,
                },
                .result = {
                    .d0 = 0x0000894AD27E4780,
                    .d1 = 0x00007BE662CD7F8B,
                    .d2 = 0x0000666372911530,
                },
            },
            {
                .m = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0x895903,
                },
                .n = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0xbed710,
                },
                .result = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0x0000666372911530,
                },
            },
            {
                .m = {
                    .d0 = 0,
                    .d1 = 1,
                    .d2 = 0xFFFFFFFFFFFFFFFF,
                },
                .n = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0xFFFFFFFFFFFFFFFF,
                },
                .result = {
                    .d0 = 1,
                    .d1 = 0xFFFFFFFFFFFFFFFD,
                    .d2 = 0x0000000000000001,
                },
            },
        };
        mul_naive_u192_tests.data = d;
        mul_naive_u192_tests.len = sizeof(d) / sizeof(struct unit_test_type);
    }
    unit_test(str_lit("mul_naive_u192"), mul_naive_u192, mul_naive_u192_tests);
}

void unit_test_div_u192(void) {
    unit_test_array_type div_naive_u192_tests;
    {
        static struct unit_test_type d[] = {
            {
                .m = {
                    .d0 = MAX_U64,
                    .d1 = 0,
                    .d2 = 0,
                },
                .n = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 2, 
                },
                .result = {
                    .d0 = 0x7FFFFFFFFFFFFFFF,
                    .d1 = 0x8000000000000000,
                    .d2 = 0x0000000000000000,
                },
            },
            {
                .m = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = MAX_U64,
                },
                .n = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 4,
                },
                .result = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0x3FFFFFFFFFFFFFFF,
                },
            },
            {
                .m = {
                    .d0 = 0xabcd2f,
                    .d1 = 0xcd2249,
                    .d2 = 0xbed710,
                },
                .n = {
                    .d0 = 0x289043,
                    .d1 = 0x1291ab,
                    .d2 = 0x895903,
                },
                .result = {
                    .d0 = 0,
                    .d1 = 0,
                    .d2 = 0x04,
                },
            }
        };
        div_naive_u192_tests.data = d;
        div_naive_u192_tests.len = sizeof(d) / sizeof(struct unit_test_type);
    };
    unit_test(str_lit("div_naive_u192"), div_naive_u192, div_naive_u192_tests);
}

int main() {
    /* u192 x = make_u192(str_lit("6277101735386680763835789423207666416102355444464034512895")); */
    /* if (argc < 2) return 1; */
    /* str s = {.data = argv[1], .len = strlen(argv[1])}; */
    /* u192 x = make_u192(s); */
    /* str y = tostr_u192(x); */

    /* u192 x = make_u192(str_lit("0")); */
    /* u192 y = make_u192(str_lit("1")); */
    /* u192 z = sub_u192(x, y); */
    /* str s = tostr_u192(x); */

    /* printf(".a = %u\n.d0 = %u\n.c = %u\n\n", x.a, x.d0, x.c); */
    /* printf("%s\n", y.data); */
    /* free(y.data); */

    /* profile(str_lit("mul_naive_u192"), mul_naive_u192); */

    /* u192 m = make_u192(str_lit("1234567890123456789012")); */
    /* u192 n = make_u192(str_lit("987654321987654321098")); */
    u192 m = make_u192(str_lit("123"));
    u192 n = make_u192(str_lit("123"));
    u192 out = mul_toomcook_u192(m, n);
    str s = tostr_u192(out);
    printf("%s == \n0000000000000001219326312467611632493760095208585886175176\n", s.data);
    printf("%lu %lu %lu\n", out.d0, out.d1, out.d2);

    /* u192 c = make_u192(str_lit("79228162514264337593543950335")); */
    /* u192 a = make_u192(str_lit("39614081257132168796771975168")); */
    /* u192 b = make_u192(str_lit("39614081257132168796771975163")); */
    /* u192 ab_real = mul_naive_u192(a, b); */
    /* u192 ab_toom = mul_toomcook_u192(a, b); */
    /* printf("IS IT REAL?: %d\n", eq_u192(ab_real, ab_toom)); */
    /* str ab_reals = tostr_u192(ab_real); */
    /* str ab_tooms = tostr_u192(ab_toom); */
    /* printf("%s REAL\n%s TOOM\n", ab_reals.data, ab_tooms.data); */
    

    /* u192 m = (u192) { */
    /*     .d0 = 0, */
    /*     .d1 = 0x1234, */
    /*     .d2 = 0xABCDF0000000000E, */
    /* }; */

    /* u192 out = logshr_u192(m, 4); */
    /* printf("%064lb%064lb%064lb\n", m.d0, m.d1, m.d2); */
    /* printf("%064lb%064lb%064lb\n", out.d0, out.d1, out.d2); */
    /* str s = tostr_u192(out); */
    /* printf("%s\n", s.data); */

    u192 a = U192(117);
    u192 bn = I192(-3);
    u192 bp = U192(3);

    u192 c1 = add_u192(a, bn);
    u192 c2 = sub_u192(a, bp);
    printf("%lu %lu %lu\n", a.d0, a.d1, a.d2);
    printf("%lu %lu %lu\n", bn.d0, bn.d1, bn.d2);
    printf("%lu %lu %lu\n", bp.d0, bp.d1, bp.d2);
    printf("%lu %lu %lu\n", c1.d0, c1.d1, c1.d2);
    printf("%lu %lu %lu\n", c2.d0, c2.d1, c2.d2);

    return 0;
}
