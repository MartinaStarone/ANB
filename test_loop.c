// test_loop.c – test per verificare la protezione dei loop con ANB
// Annotato con trip_count_10 per indicare che il loop fa esattamente 10 iterazioni.
#include <stdio.h>
#include <stdlib.h>

__attribute__((annotate("trip_count_10")))
int sum_10(void) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum = sum + i;  // istruzione ADD protetta da ANB
    }
    return sum;
}

__attribute__((annotate("exclude")))
int main(void) {
    int r = sum_10();
    printf("sum_10() = %d (expected 45)\n", r);
    return 0;
}
