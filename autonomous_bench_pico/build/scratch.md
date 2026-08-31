# Piano di Implementazione per ANB Standalone

## Analisi del Crash Precedente
Il crash che hai visto (`isr_riscv_machine_timer`) in realtà **non è stato causato da ANB**! 
Avendo tu messo il breakpoint dentro `expo` e poi saltato nel `main`, hai corrotto lo stack pointer (perché non hai eseguito il prologo/epilogo della funzione `expo`). Quando il `main` ha provato a fare `return`, ha letto un indirizzo di ritorno sballato ed è crashato per colpa dello stack.
Inoltre, ho scoperto che il core RISC-V Hazard3 della Pico 2 **ignora silenziosamente la lettura da `0xFFFFFFFF`**, quindi il trucco branchless che avevamo usato non funziona su questo specifico hardware.

## Il problema di ANB "Standalone"
Come descritto nel paper originale di ASPIS, **ANB non è progettato per funzionare da solo**. ANB si limita ad "avvelenare" (modificare) la `runtime_sig` se c'è un errore matematico o di round. Ma **è compito di RACFED** controllare che la firma sia corretta alla fine del blocco o del programma!
Siccome stiamo eseguendo `--anb-only` (che disabilita RACFED), la firma viene avvelenata, ma nessuno controlla se è giusta alla fine, quindi il programma esce con `status=0` senza accorgersi di nulla!

## Soluzione Proposta
Per raggiungere il tuo obiettivo (testare ANB in modo completamente standalone contro il Round Skipping e l'Instruction Skipping), dobbiamo dotare ANB di "denti" propri, inserendo dei controlli espliciti (tramite branch al `SigMismatch_Handler`) che altrimenti farebbe RACFED.

1. **Round Skipping (SCEV)**: In `ANB.cpp` (nella funzione `checkLoopCount`), ANB calcola l'errore `err = finalCnt - expectedRounds`. Modificherò il codice per fare in modo che se `err != 0`, il programma salti immediatamente al `SigMismatch_Handler`.
2. **Instruction Skipping**: Dato che il trucco branchless con la memoria non è supportato dall'hardware della Pico, ripristinerò i controlli in `checkJumpSig` usando un branch esplicito al `SigMismatch_Handler` in caso di anomalia, in modo da darti un output chiaro per i tuoi test sui voltage glitches.

Se sei d'accordo con questo piano, procedo subito a modificare il compilatore e generare il nuovo file `.uf2` robusto e pronto per i tuoi test definitivi!
