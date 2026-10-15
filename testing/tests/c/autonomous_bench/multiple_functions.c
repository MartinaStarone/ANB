#include <stdio.h>
#include <stdlib.h>

/* Harness funzionale: non sostituisce l'hardening ANB. */
void DataCorruption_Handler(void) {
    fputs("FAULT_DETECTED: DataCorruption\n", stderr);
    exit(2);
}

/*
 * noinline serve soltanto a rendere semplice l'individuazione del PC
 * bersaglio durante il debug. Se il pass seleziona le funzioni tramite
 * annotazioni, anche questa funzione deve essere inclusa nel perimetro.
 */
__attribute__((noinline))
int expo(int x, int y) {
#ifdef EMULATE_XOR_SKIP
    (void)y;
    return x;                 /* Modello IR di XOR non eseguito. */
#else
    return x ^ y;
#endif
}

__attribute__((annotate("trip_count_10")))
int main(int argc, char **argv) {
    const int j = argc > 1 ? atoi(argv[1]) : 2;
    const int w = argc > 2 ? atoi(argv[2]) : 3;
    int z[10] = {0};

    /* Esattamente dieci iterazioni e dieci risultati inizializzati. */
    for (int i = 0; i < 10; ++i) {
        const int input = (i & 1) ? w : j;
        z[i] = expo(input, i);
    }

    /* Oracolo indipendente per il solo test di fault injection. */
    for (int i = 0; i < 10; ++i) {
        volatile int input = (i & 1) ? w : j;
        const int expected = input ^ i;
        if (z[i] != expected) {
            fprintf(stderr, "Mismatch: i=%d, got=%d, expected=%d\n",
                    i, z[i], expected);
            DataCorruption_Handler();
        }
        printf("z[%d] = %d\n", i, z[i]);
    }

    return 0;
}
