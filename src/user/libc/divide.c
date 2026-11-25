#include <stdint.h>

uint64_t __udivmoddi4(uint64_t numerator, uint64_t denominator, uint64_t *remainder) {
    uint64_t quotient = 0, rest = 0;
    for (int bit = 63; bit >= 0; bit--) {
        rest = (rest << 1) | ((numerator >> bit) & 1);
        if (rest >= denominator) {
            rest -= denominator;
            quotient |= 1ULL << bit;
        }
    }
    if (remainder != 0)
        *remainder = rest;
    return quotient;
}

uint64_t __udivdi3(uint64_t numerator, uint64_t denominator) {
    return __udivmoddi4(numerator, denominator, 0);
}

uint64_t __umoddi3(uint64_t numerator, uint64_t denominator) {
    uint64_t remainder;
    __udivmoddi4(numerator, denominator, &remainder);
    return remainder;
}

int64_t __divdi3(int64_t numerator, int64_t denominator) {
    uint64_t a = numerator < 0 ? -(uint64_t) numerator : (uint64_t) numerator;
    uint64_t b = denominator < 0 ? -(uint64_t) denominator : (uint64_t) denominator;
    uint64_t quotient = __udivmoddi4(a, b, 0);
    return (numerator < 0) != (denominator < 0) ? (int64_t) (-quotient) : (int64_t) quotient;
}

int64_t __moddi3(int64_t numerator, int64_t denominator) {
    uint64_t a = numerator < 0 ? -(uint64_t) numerator : (uint64_t) numerator;
    uint64_t b = denominator < 0 ? -(uint64_t) denominator : (uint64_t) denominator;
    uint64_t remainder;
    __udivmoddi4(a, b, &remainder);
    return numerator < 0 ? (int64_t) (-remainder) : (int64_t) remainder;
}
