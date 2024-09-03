#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef uint64_t u64;

#define MAX_U64 18446744073709551615ULL

#define BIT(x, i) (((x) >> (i)) & 1)

typedef struct {
    u64 a;
    u64 b;
    u64 c;
} u192;

#define MAX_U192 (u192) {.a=MAX_U64,.b=MAX_U64,.c=MAX_U64}

typedef struct {
    char *data;
    u64 len;
} str;

void str_free(str s) {
    free(s.data);
}

#define str_lit(x) (str) { .data = x, .len = sizeof(x) - 1 }

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
    u64_carry c = adc_u64(m.c, n.c, 0);
    u64_carry b = adc_u64(m.b, n.b, c.carry);
    u64_carry a = adc_u64(m.a, n.a, b.carry);

    return (u192) {
        .c = c.res,
        .b = b.res,
        .a = a.res,
    };
}

u192 sub_u192(u192 m, u192 n) {
    u64_carry c = sbb_u64(m.c, n.c, 0);
    u64_carry b = sbb_u64(m.b, n.b, c.carry);
    u64_carry a = sbb_u64(m.a, n.a, b.carry);

    return (u192) {
        .c = c.res,
        .b = b.res,
        .a = a.res,
    };
}

bool eq_u192(u192 m, u192 n) {
    bool a = m.a == n.a;
    bool b = m.b == n.b;
    bool c = m.c == n.c;
    return a && b && c;
}

bool gt_u192(u192 m, u192 n) {
    if      (m.a > n.a) return true;
    else if (m.b > n.b) return true;
    else if (m.c > n.c) return true;
    else                return false;
}

bool lt_u192(u192 m, u192 n) {
    if      (m.a < n.a) return true;
    else if (m.b < n.b) return true;
    else if (m.c < n.c) return true;
    else                return false;
}

bool ge_u192(u192 m, u192 n) { return !lt_u192(m, n); }
bool le_u192(u192 m, u192 n) { return !gt_u192(m, n); }
bool ne_u192(u192 m, u192 n) { return !eq_u192(m, n); }

u192 mul_naive_u192(u192 m, u192 n) {
    u192 result;

    result.c = m.c*n.c;
    result.b = m.b*n.c + m.c*n.b;
    result.a = m.a*n.c + m.b*n.b + m.c*n.a;


    // what to do with these inner products...
    // [255,192]
    /* u64 x = m.a*n.b + m.b*n.a; */

    // [319:256]
    /* u64 y = m.a*n.a; */

    return result;
}

u192 pow_naive_u192(u192 base, u64 power) {
    u192 result = {.c=1};
    for (u64 i = 0; i < power; ++i) {
        result = mul_naive_u192(base, result);
    }

    return result;
}

void div_full_u192(u192 m, u192 n, u192 *div, u192 *rem) {
    if (eq_u192((u192){.c=1}, n)) {
        if (div) *div = m;
        if (rem) *rem = (u192){0};
        return;
    }
    
    if (eq_u192((u192){0}, m)) {
        u192 result = {0};
        if (div) *div = result;
        if (rem) *rem = result;
        return;
    }
    
    if (eq_u192((u192){0}, n)) {
        printf("DIVISION BY ZERO HAHAHAHAH\n");
        u192 result = {0};
        if (div) *div = result;
        if (rem) *rem = result;
        return;
    }
    
    if (eq_u192(m, n)) {
        if (div) *div = (u192){.c=1};
        if (rem) *rem = (u192){.c=0};
        return;
    }

    u192 result = m;
    for (u192 i = {0}; lt_u192(i, MAX_U192); i = add_u192(i, (u192){.c=1})) {
        result = sub_u192(result, n);
        if (le_u192(result, n)) {
            if (div) *div = i;
            if (rem) *rem = result;
            return;
        }
    }
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
        u192 x = pow_naive_u192((u192){.c=10}, s.len - i - 1);
        u192 y = {.c=(u64)(s.data[i] - '0')};
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
        u192 x = pow_naive_u192((u192){.c=10}, i);
        u192 y = div_naive_u192(m, x);
        u192 z = mod_naive_u192(y, (u192){.c=10});
        s.data[s.len - 1 - i] = '0' + (char)z.c;
    }

    return s;
}

int main(void) {
    /* u192 x = make_u192(str_lit("6277101735386680763835789423207666416102355444464034512895")); */
    u192 x = make_u192(str_lit("18446744073709551617"));
    str y = tostr_u192(x);

    /* u192 x = make_u192(str_lit("0")); */
    /* u192 y = make_u192(str_lit("1")); */
    /* u192 z = sub_u192(x, y); */
    /* str s = tostr_u192(x); */

    printf("%s\n", y.data);
    str_free(y);
    return 0;
}
