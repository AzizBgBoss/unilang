#include <stdio.h>

int main(void)
{
    printf("|%8d| |%-6.4u| |%#08x| |%.2f| |%+8.2e| %.3g %c\n",
           -42, 23u, 42u, 3.14159, 12.5, 1.2345, 'Q');
    printf("%hhd %hu\n", 255, 65535);
    return 0;
}
