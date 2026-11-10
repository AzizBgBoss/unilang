#include <stdint.h>

uint64_t shift_left64(uint64_t value, uint64_t amount) { return value << amount; }
uint64_t shift_right64(uint64_t value, uint64_t amount) { return value >> amount; }
int64_t shift_signed64(int64_t value, uint64_t amount) { return value >> amount; }
int64_t divide_signed64(int64_t value, int64_t divisor) { return value / divisor; }
int64_t remainder_signed64(int64_t value, int64_t divisor) { return value % divisor; }
uint64_t divide_unsigned64(uint64_t value, uint64_t divisor) { return value / divisor; }
int64_t extend_signed8(int8_t value) { return value; }
int64_t extend_signed16(int16_t value) { return value; }
int64_t extend_signed32(int32_t value) { return value; }
uint64_t extend_unsigned32(uint32_t value) { return value; }

int main(void)
{
    uint64_t now = gettime();
    uint64_t high = 5000000000ULL;
    uint64_t sum = high + 7ULL;
    uint64_t difference = sum - high;
    uint64_t product = high * 3ULL;
    uint64_t quotient = product / 3ULL;
    uint64_t remainder = product % 7ULL;
    uint64_t bits = (high & 0xFFFFFFFFULL) | 0x100000000ULL;
    uint64_t xored = bits ^ high;
    uint64_t inverted = ~high;
    uint64_t one = 1;
    uint64_t zero_shift = 0, one_shift = 1, shift63 = 63, shift64 = 64, shift65 = 65;
    uint64_t left0 = shift_left64(high, zero_shift);
    uint64_t left1 = shift_left64(one, one_shift);
    uint64_t left63 = shift_left64(one, shift63);
    uint64_t left64 = shift_left64(one, shift64);
    uint64_t left65 = shift_left64(one, shift65);
    uint64_t right63 = shift_right64(left63, shift63);
    int64_t signed_value = -9223372036854775807LL - 1LL;
    int64_t signed_div = divide_signed64(signed_value, -1LL);
    int64_t signed_mod = remainder_signed64(signed_value, -1LL);
    int64_t signed_shift = shift_signed64(signed_value, shift63);
    int64_t signed_small = extend_signed8(-1);
    int64_t signed_medium = extend_signed16(-2);
    int64_t signed_word = extend_signed32(-3);
    uint64_t zero_extended = extend_unsigned32((uint32_t)high);
    uint64_t slot = 0;
    uint64_t *pointer = &slot;


    *pointer = high;
    if (now < 5000000000ULL) return 1;
    if (difference != 7 || product != 15000000000ULL || quotient != high) return 2;
    if (remainder != 6 || bits != high || xored != 0) return 3;
    if (inverted != (~high) || left0 != high || left1 != 2) return 4;
    if (left63 != 0x8000000000000000ULL || left64 != 0 || left65 != 0 || right63 != 1) return 5;
    if (signed_div != signed_value || signed_mod != 0 || signed_shift != -1 || signed_small != -1) return 6;
    if (signed_medium != -2 || signed_word != -3) return 11;
    if (zero_extended != 705032704ULL || slot != high) return 7;
    if (divide_unsigned64(high, 0) != high) return 10;
    if (!(high > 1 && high >= 5000000000ULL && high != 0)) return 8;
    if (!(signed_value < 0 && signed_value <= -1 && signed_value != 0)) return 9;
    if (!(signed_value < -1 && signed_value <= -1 && signed_value != 0 && -1 >= signed_value)) return 12;
    printf("%llu %lld %llx\n", now, signed_shift, (unsigned long long)left63);
    return 0;
}
