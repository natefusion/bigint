#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;

#define MAX_U64 0xFFFFFFFFFFFFFFFFULL
#define MAX_U32 0xFFFFFFFFUL

#define BIT(x, i) (((x) >> (i)) & 1)

#define MIN(x, y) ((x) < (y) ? (x) : (y))

typedef union {
    // Big end first
    u64 d[3];
    struct {u64 d0; u64 d1; u64 d2;};
} u192;

#define MAX_U192 (u192) {.d0=MAX_U64,.d1=MAX_U64,.d2=MAX_U64}

typedef struct {
    char *data;
    u64 len;
} str;

void str_free(str s) {
    free(s.data);
}

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

u64_carry adc_u64(u64 x, u64 y, u64 c) {
    u64 result = x + y + c;
    u64 carry = result < x;
    return (u64_carry) { .res = result, .carry = carry };
}

u64_carry sbb_u64(u64 x, u64 y, u64 b) {
    u64 result = x - (y + b);
    u64 borrow = result > x;
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
    {
        u64_carry d2 = adc_u64(result.d2, mul, 0);
        result.d2 = d2.res;
        if (d2.carry != 0) {
            result.d1 += d2.carry;
        }
        result.d1 += carry;
    }

    mulq(&carry, &mul, m.d1, n.d2);
    {
        u64_carry d1 = adc_u64(result.d1, mul, 0);
        result.d1 = d1.res;
        if (d1.carry != 0) {
            result.d0 += d1.carry;
        }
        result.d0 += carry;
    }

    mulq(&carry, &mul, m.d0, n.d2);
    result.d0 += mul;

    /* ----------------------- */

    mulq(&carry, &mul, m.d2, n.d1);
    {
        u64_carry d1 = adc_u64(result.d1, mul, 0);
        result.d1 = d1.res;
        if (d1.carry != 0) {
            result.d0 += d1.carry;
        }
        result.d0 += carry;
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

int main() {
    u192 x = make_u192(str_lit("6277101735386680763835789423207666416102355444464034512895"));
    /* if (argc < 2) return 1; */
    /* str s = {.data = argv[1], .len = strlen(argv[1])}; */
    /* u192 x = make_u192(s); */
    str y = tostr_u192(x);

    /* u192 x = make_u192(str_lit("0")); */
    /* u192 y = make_u192(str_lit("1")); */
    /* u192 z = sub_u192(x, y); */
    /* str s = tostr_u192(x); */

    /* printf(".a = %u\n.d0 = %u\n.c = %u\n\n", x.a, x.d0, x.c); */
    printf("%s\n", y.data);
    /* str_free(y); */

    unit_test_array_type mul_naive_u192_tests;
    {
        struct unit_test_type d[] = {
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
            }
        };
        mul_naive_u192_tests.data = d;
        mul_naive_u192_tests.len = sizeof(d) / sizeof(struct unit_test_type);
    }
    unit_test(str_lit("mul_naive_u192"), mul_naive_u192, mul_naive_u192_tests);

    unit_test_array_type div_naive_u192_tests;
    {
        struct unit_test_type d[] = {
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

    return 0;
}
