#!/usr/bin/env bash
set -e  # esci subito se un comando fallisce

# ── Percorsi ────────────────────────────────────────────────────────────────
PROJ=/home/martina/CLionProjects/ASPIS2
BUILD=$PROJ/build/passes
TEST=/home/martina/Scrivania/ASPIS2/testing/tests/c/autonomous_bench/multiple_functions.c
OUT=$PROJ/out_racfed_anb   # directory output

CLANG=clang-21
OPT=opt-21

# ── Preparazione ─────────────────────────────────────────────────────────────
mkdir -p $OUT
echo "Output directory: $OUT"
echo ""

# ── STEP 1: C → LLVM IR (ORIGINALE, prima di qualsiasi pass) ─────────────────
echo "=== [1/5] C → LLVM IR (originale) ==="
$CLANG "$TEST" \
    -S -emit-llvm -O0 \
    -Xclang -disable-O0-optnone \
    -o $OUT/01_original.ll
echo "    → $OUT/01_original.ll"

# ── STEP 2: lower-switch (richiesto da ASPIS prima delle pass) ───────────────
echo "=== [2/5] lower-switch ==="
$OPT --passes="lower-switch" \
    $OUT/01_original.ll -o $OUT/02_lowered.ll -S
echo "    → $OUT/02_lowered.ll"

# ── STEP 3: Applica RACFED ───────────────────────────────────────────────────
echo "=== [3/5] RACFED ==="
$OPT -load-pass-plugin=$BUILD/libRACFED.so \
    --passes="racfed-verify" \
    $OUT/02_lowered.ll -o $OUT/03_racfed.ll -S
echo "    → $OUT/03_racfed.ll"

# ── STEP 4: Applica ANB (dopo RACFED) ────────────────────────────────────────
echo "=== [4/5] ANB ==="
$OPT -load-pass-plugin=$BUILD/Utils/libUtils.so \
    -load-pass-plugin=$BUILD/libANB.so \
    --passes="anb-encode" \
    $OUT/03_racfed.ll -o $OUT/04_racfed_anb.ll -S
echo "    → $OUT/04_racfed_anb.ll"

# ── STEP 5: Compila binario finale ───────────────────────────────────────────
echo "=== [5/5] Compilazione binario ==="
$CLANG $OUT/04_racfed_anb.ll -o $OUT/hardened_bin
echo "    → $OUT/hardened_bin"

# ── Statistiche rapide ───────────────────────────────────────────────────────
echo ""
echo "=== Dimensioni file .ll ==="
wc -l $OUT/*.ll

echo ""
echo "=== Istruzioni ANB iniettate ==="
grep -c "anb\." $OUT/04_racfed_anb.ll || true

echo ""
echo "=== Test esecuzione binario ==="
$OUT/hardened_bin
echo ""
echo "=== DONE ==="
