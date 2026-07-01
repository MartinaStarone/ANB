// add.c  – test minimo per la pipeline RACFED + ANB
// Annotato con __attribute__((annotate("to_protect"))) così ASPIS lo processa.
#include <stdio.h>
#include <stdlib.h>

__attribute__((annotate("to_protect")))
int add(int a, int b) {
    int result = a + b;
    return result;
}

__attribute__((annotate("to_protect")))
int check_equal(int x, int y) {
    if (x == y) {
        return 1;
    }
    return 0;
}

int main(void) {
    int r = add(3, 4);
    printf("add(3,4) = %d\n", r);
    printf("check_equal(7,7) = %d\n", check_equal(7, 7));
    printf("check_equal(7,8) = %d\n", check_equal(7, 8));
    return 0;
}
