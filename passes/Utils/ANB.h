#ifndef ANB_H
#define ANB_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include <cstdint>
#include <map>
#include <unordered_map>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>

namespace llvm {

/// Default ANB modulus (prime number).
static constexpr uint64_t ANB_DEFAULT_A = 58321ULL;

/// Compute the modular inverse of a modulo 2^64 at compile time.
/// Uses Hensel's lemma: each iteration x *= (2 - a*x) doubles the
/// number of correct bits: 1 → 2 → 4 → 8 → 16 → 32 → 64 bits.
/// Requires a to be odd (guaranteed for ANB_DEFAULT_A = 58321).
static constexpr uint64_t modInverse64(uint64_t a) {
    uint64_t x = 1;     // a*1 ≡ 1 (mod 2) for any odd a
    x *= 2 - a * x;     // mod 2^2
    x *= 2 - a * x;     // mod 2^4
    x *= 2 - a * x;     // mod 2^8
    x *= 2 - a * x;     // mod 2^16
    x *= 2 - a * x;     // mod 2^32
    x *= 2 - a * x;     // mod 2^64
    return x;
}

/// Modular inverse of ANB_DEFAULT_A mod 2^64, computed at compile time.
/// Replaces udiv by A in createANBMul with a single MUL instruction,
/// avoiding any dependency on __udivdi3 / __udivti3 on RV32 bare-metal.
static constexpr uint64_t ANB_A_INV = modInverse64(ANB_DEFAULT_A);

/// Holds an ANB-encoded value together with its bias B.
/// The invariant is: encoded ≡ real_value + B  (mod A)
struct ANBValue {
    Value    *encoded; ///< IR value representing (real_value + B) in i128
    uint64_t  B;       ///< compile-time bias
    ANBValue &operator=(uint64_t uint64);
};

/**
 * Emits IR that computes the ANB-encoded addition of two values.
 *
 * Given real values a (bias Ba) and b (bias Bb), emits:
 *   z_enc  = (a + Ba) + (b + Bb)          [i128 to avoid overflow]
 *   Bz     = (Ba + Bb) % A
 *
 * The caller can verify correctness by checking z_enc % A == Bz.
 *
 * @param Bld  IRBuilder positioned at the insertion point.
 * @param a    Raw (un-encoded) i64 operand a.
 * @param Ba   Compile-time bias for a.
 * @param b    Raw (un-encoded) i64 operand b.
 * @param Bb   Compile-time bias for b.
 * @param A    ANB modulus.
 * @return     ANBValue{z_enc (i128), Bz}.
 */
ANBValue createANBadd(IRBuilder<> &Bld,
                      Value *a, uint64_t Ba,
                      Value *b, uint64_t Bb,
                      uint64_t A = ANB_DEFAULT_A);

/**
 * Emits IR that returns an ANB-encoded equality check.
 *
 * Computes diff = (a + Ba) - (b + Bb).
 * When a == b: diff % A == (Ba - Bb) % A   [the "positive" signature].
 * When a != b: diff % A != (Ba - Bb) % A   [signature mismatch].
 *
 * The returned i64 value can be added to the RACFED runtime signature;
 * the compile-time contribution to subtract is (Ba - Bb) % A.
 *
 * @param Bld  IRBuilder positioned at the insertion point.
 * @param a    Raw i64 operand a.
 * @param Ba   Compile-time bias for a.
 * @param b    Raw i64 operand b.
 * @param Bb   Compile-time bias for b.
 * @param A    ANB modulus.
 * @return     IR value holding diff % A  (i64).
 */
Value *createANBSetEq(IRBuilder<> &Bld,
                      Value *a, uint64_t Ba,
                      Value *b, uint64_t Bb,
                      uint64_t A = ANB_DEFAULT_A);

/**
 *
    * @param Bld  IRBuilder positioned at the insertion point.
     * @param a    Raw i64 operand a.
     * @param Ba   Compile-time bias for a.
     * @param b    Raw i64 operand b.
     * @param Bb   Compile-time bias for b.
     * @param A    ANB modulus.
     * @return     IR value holding diff % A  (i64).
 */
ANBValue createANBMul(IRBuilder<> &Bld,
                       Value *a, uint64_t Ba,
                       Value *b, uint64_t Bb,
                       uint64_t A = ANB_DEFAULT_A);

/**
 * Emits IR that computes the ANB-encoded subtraction of two values.
 */
ANBValue createANBSub(IRBuilder<> &Bld,
                      Value *a, uint64_t Ba,
                      Value *b, uint64_t Bb,
                      uint64_t A = ANB_DEFAULT_A);

/**
 * Emits IR that computes the ANB-encoded XOR of two values using bitwise decomposition.
 */
ANBValue createANBXOR(IRBuilder<> &Bld,
                      Value *a, uint64_t Ba,
                      Value *b, uint64_t Bb,
                      uint64_t A = ANB_DEFAULT_A);

} // namespace llvm

// ── Pass declaration ──────────────────────────────────────────────────────────

/// ANB pass: applies arithmetic ANB checks to the original instructions
/// identified by RACFED, creating a data-flow dependency between
/// computation results and the RACFED runtime signature.
///
/// Must be run AFTER racfed-verify in the opt pipeline:
///   opt -load-pass-plugin=libRACFED.so \
///       -load-pass-plugin=libANB.so    \
///       -passes="racfed-verify,anb-encode" ...
class ANBPass : public llvm::PassInfoMixin<ANBPass> {
private:
    std::map<llvm::Value *, llvm::StringRef> FuncAnnotations;
    std::unordered_map<llvm::BasicBlock *, uint32_t> compileTimeSig;

    /// Returns true if the instruction was injected by RACFED
    /// (loads/stores of runtime_sig, sig_add adds, etc.).
    bool isRACFEDInstruction(llvm::Instruction *I,
                             llvm::GlobalVariable *RuntimeSig) const;

    /// Returns true if the BB contains at least one ANB-protectable instruction
    /// (integer add or icmp eq).
    bool hasANBInstructions(llvm::BasicBlock &BB,
                            llvm::GlobalVariable *RuntimeSig) const;

    /// Creates a compile-time signature for the basic blocks.
    void createSignature(llvm::Function &F);

    /// Inter-BB check: verifies runtime_sig was updated (+expectedDiff) in the BB.
    /// Uses branchless memory trap to crash (SIGSEGV) if diff != expectedDiff.
    void checkJumpSig(llvm::BasicBlock &BB,
                      llvm::GlobalVariable *RuntimeSig,
                      llvm::GlobalVariable *PrevSig,
                      uint64_t expectedDiff);

    void checkLoopCount(llvm::Loop *L, llvm::ScalarEvolution &SE, uint64_t expectedRounds,
                        llvm::GlobalVariable *RuntimeSig,
                        llvm::GlobalVariable *PrevSig, llvm::Module &Md);

    void saveSnapshot(llvm::BasicBlock &BB, llvm::GlobalVariable *RuntimeSig, llvm::GlobalVariable *PrevSig);

    llvm::Value *checkSig(llvm::Module &Md, llvm::ANBValue av, llvm::IRBuilder<> &B);
    void checkOnReturn(llvm::BasicBlock &BB,
                       llvm::GlobalVariable *RuntimeSig,
                       llvm::GlobalVariable *PrevSig);


    /// Saves runtime_sig snapshot into PrevSig before the BB terminator.
    void saveSignature(llvm::BasicBlock &BB,
                      llvm::GlobalVariable *RuntimeSig,
                      llvm::GlobalVariable *PrevSig);

public:
    llvm::PreservedAnalyses run(llvm::Module &Md,
                                llvm::ModuleAnalysisManager &AM);
    static bool isRequired() { return true; }
};

#endif // ANB_H