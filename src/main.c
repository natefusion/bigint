#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdbit.h>
#include <assert.h>
#include "randq.h"
#include "hr_timer.h"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

typedef union {
    // Big end first
    u64 d[3];
    struct {u64 d0; u64 d1; u64 d2;};
} u192;

typedef struct {
    char *data;
    u64 len;
} str;

#define MAX_U64 0xFFFFFFFFFFFFFFFFULL
#define MAX_U192 (u192) {.d0=MAX_U64,.d1=MAX_U64,.d2=MAX_U64}
#define str_lit(x) (str) { .data = x, .len = sizeof(x) - 1 }

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
    bool carry = result < x;
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
    /* u64_carry a = adc_u64(m.a, n.a, b.carry); */

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
    /* u64_carry a = sbb_u64(m.a, n.a, b.carry); */

    return (u192) {
        .d2 = d.res,
        .d1 = c.res,
        .d0 = b.res,
    };
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

void div_full_u192(u192 m, u192 n, u192 *q, u192 *r) {
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

u192 div_naive_u192(u192 m, u192 n) {
    u192 result = {0};
    div_full_u192(m, n, &result, NULL);
    return result;
}

u192 mod_naive_u192(u192 m, u192 n) {
    u192 result = {0};
    div_full_u192(m, n, NULL, &result);
    return result;
}

u192 mul_toomcook_u192(u192 m, u192 n) {
    // these all better not overflow ...
    // these results can be negative ...
    // can I ignore it?

    constexpr u64 b = 32;
    // b = 2^32
    // B = b^i = 2^32
    // The largest number that can be multiplied properly has 96 set bits
    u192 m2 = (u192){.d2=m.d1 & 0x00000000FFFFFFFFULL};
    u192 m1 = (u192){.d2=(m.d2 & 0xFFFFFFFF00000000ULL) >> 32ULL};
    u192 m0 = (u192){.d2=m.d2 & 0x00000000FFFFFFFFULL};

    u192 n2 = (u192){.d2=n.d1 & 0x00000000FFFFFFFFULL};
    u192 n1 = (u192){.d2=(n.d2 & 0xFFFFFFFF00000000ULL) >> 32ULL};
    u192 n0 = (u192){.d2=n.d2 & 0x00000000FFFFFFFFULL};

    /* u192 m2 = (u192){.d2=m.d0}; */
    /* u192 m1 = (u192){.d2=m.d1}; */
    /* u192 m0 = (u192){.d2=m.d2}; */

    /* u192 n2 = (u192){.d2=n.d0}; */
    /* u192 n1 = (u192){.d2=n.d1}; */
    /* u192 n0 = (u192){.d2=n.d2}; */
    
    u192 p_0 = add_u192(m0, m2);
    u192 p0 = m0;
    u192 p1 = add_u192(p_0, m1);
    u192 p_1 = sub_u192(p_0, m1);
    u192 p_2 = add_u192(p_1, m2);
    p_2 = mul_naive_u192(p_2, (u192){.d2=2});
    p_2 = sub_u192(p_2, m0);
    u192 pinf = m2;

    u192 q_0 = add_u192(n0, n2);
    u192 q0 = n0;
    u192 q1 = add_u192(q_0, n1);
    u192 q_1 = sub_u192(q_0, n1);
    u192 q_2 = add_u192(q_1, n2);
    q_2 = mul_naive_u192(q_2, (u192){.d2=2});
    q_2 = sub_u192(q_2, n0);
    u192 qinf = n2;

    u192 r0 = mul_naive_u192(p0,q0);
    u192 r1 = mul_naive_u192(p1,q1);
    u192 r_1 = mul_naive_u192(p_1,q_1);
    u192 r_2 = mul_naive_u192(p_2,q_2);
    u192 rinf = mul_naive_u192(pinf,qinf);

    u192 R0 = r0;
    u192 R4 = rinf;
    u192 R3 = sub_u192(r_2, r1);
    R3 = div_naive_u192(R3, (u192){.d2=3});
    u192 R1 = sub_u192(r1, r_1);
    R1 = div_naive_u192(R1, (u192){.d2=2});
    u192 R2 = sub_u192(r_1, r0);
    
    R3 = sub_u192(R2, R3);
    R3 = div_naive_u192(R3, (u192){.d2=2}); // division don't work right here, because unsigned. make a signed div
    u192 temp = mul_naive_u192((u192){.d2=2}, rinf);
    R3 = add_u192(R3, temp);

    R2 = add_u192(R2, R1);
    R2 = sub_u192(R2, R4);
    R1 = sub_u192(R1, R3);

    /* printf("m0=%lu\nm1=%lu\nm2=%lu\n", m0, m1, m2); */
    /* printf("n0=%lu\nn1=%lu\nn2=%lu\n", n0, n1, n2); */
    /* printf("p0=%lu\np1=%lu\np_1=%lu\np_2=%lu\np_2=%lu\npinf\n", p0, p1, p_1, p_2, pinf); */
    /* printf("q0=%lu\nq1=%lu\nq_1=%lu\nq_2=%lu\nq_2=%lu\nqinf\n", q0, q1, q_1, q_2, qinf); */
    /* printf("R0=%lu\nR1=%lu\nR2=%lu\nR3=%lu\nR4=%lu\n", R0, R1, R2, R3, R4); */
    
    R1 = logshl_u192(R1, b*1);
    R2 = logshl_u192(R2, b*2);
    R3 = logshl_u192(R3, b*3);
    R4 = logshl_u192(R4, b*4);
    
    R0 = add_u192(R0, R1);
    R0 = add_u192(R0, R2);
    R0 = add_u192(R0, R3);
    R0 = add_u192(R0, R4);
    
    return R0;
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
        u192 x = pow_naive_u192((u192){.d2=10}, i);
        u192 y = div_naive_u192(m, x);
        u192 z = mod_naive_u192(y, (u192){.d2=10});
        s.data[s.len - 1 - i] = '0' + (char)z.d2;
    }

    return s;
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

    u192 m = make_u192(str_lit("1234567890123456789012"));
    u192 n = make_u192(str_lit("987654321987654321098"));
    u192 out = mul_toomcook_u192(m, n);
    str s = tostr_u192(out);
    printf("%s", s.data);

    /* u192 m = (u192) { */
    /*     .d0 = 0, */
    /*     .d1 = 0, */
    /*     .d2 = 0xABCDF0000000000E, */
    /* }; */

    /* u192 out = logshl_u192(m, 1); */
    /* printf("%064lb%064lb%064lb\n", m.d0, m.d1, m.d2); */
    /* printf("%064lb%064lb%064lb\n", out.d0, out.d1, out.d2); */
    /* str s = tostr_u192(out); */
    /* printf("%s\n", s.data); */

    return 0;
}
