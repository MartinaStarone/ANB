
#define DISTR_START 1
#define DISTR_END 0x7fffffff
#include "ANB.h"
#include "Utils.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include <random>
#include <string>

std::uniform_int_distribution<uint32_t> dist32(DISTR_START, DISTR_END);
using namespace llvm;

// ── ANB helper functions ──────────────────────────────────────────────────────

static void printSig(llvm::Module &Md, llvm::IRBuilder<> &B, llvm::Value *SigVal, const char *Msg) {
    llvm::LLVMContext &Ctx = Md.getContext();
    llvm::FunctionCallee Printf = Md.getOrInsertFunction(
        "printf",
        llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(Ctx),
                                llvm::PointerType::getUnqual(Ctx),
                                true));

    std::string Fmt = std::string("[ANB] ") + Msg + ": %ld\n";
    llvm::Value *FmtStr = B.CreateGlobalString(Fmt);

    if (SigVal->getType()->isIntegerTy(32)) {
        SigVal = B.CreateZExt(SigVal, llvm::Type::getInt64Ty(Ctx));
    }

    B.CreateCall(Printf, {FmtStr, SigVal});
}

ANBValue llvm::createANBadd(IRBuilder<> &Bld,
                            Value *a, uint64_t Ba,
                            Value *b, uint64_t Bb,
                            uint64_t A)
{
    LLVMContext &Ctx = Bld.getContext();
    Type *I128 = Type::getInt128Ty(Ctx);
    Type *I64  = Type::getInt64Ty(Ctx);

    // Extend operands to i128 to avoid overflow during encoding.
    Value *a128 = Bld.CreateZExt(a, I128, "anb.a128");
    Value *b128 = Bld.CreateZExt(b, I128, "anb.b128");
    Value *A128 = ConstantInt::get(I128, A);

    // Encode: a_enc = a*A + Ba,  b_enc = b*A  + Bb
    Value *a_mul = Bld.CreateMul(a128, A128, "anb.a_mul");
    Value *a_enc = Bld.CreateAdd(a_mul, ConstantInt::get(I128, Ba), "anb.a_enc");
    // Encode corretto: b_enc = b * A + Bb
    Value *b_mul = Bld.CreateMul(b128, A128, "anb.b_mul");
    Value *b_enc = Bld.CreateAdd(b_mul, ConstantInt::get(I128, Bb), "anb.b_enc");


    // z_enc = a_enc + b_enc  (= (a+b)*A + (Ba+Bb))
    Value *z_enc = Bld.CreateAdd(a_enc, b_enc, "anb.z_enc");

    uint64_t Bz = (Ba + Bb);
    return ANBValue{z_enc, Bz};
}




Value *llvm::createANBSetEq(IRBuilder<> &B, Value *a, uint64_t Ba, Value *b, uint64_t Bb, uint64_t A) {
    Type *I64 = B.getInt64Ty();

    if (!a->getType()->isIntegerTy(64))
        a = B.CreateZExt(a, I64, "anb.seteq_a64");
    if (!b->getType()->isIntegerTy(64))
        b = B.CreateZExt(b, I64, "anb.seteq_b64");

    Value *diff = B.CreateSub(a, b, "anb.diff");
    
    // sigPos = (Ba - Bb) mod A
    uint64_t sigPos = (A + (Ba % A) - (Bb % A)) % A;
    
    // sigCond = diff + sigPos
    // If a == b, diff = 0, so sigCond = sigPos.
    Value *sigCond = B.CreateAdd(diff, ConstantInt::get(I64, sigPos), "anb.sigCond");

    return sigCond;
}


//Returns true when the instruction is a RACFED injected one
bool ANBPass::isRACFEDInstruction(Instruction *I,
                                  GlobalVariable *RuntimeSig) const
{
    // Skip loads/stores that access the runtime signature global.
    if (auto *LI = dyn_cast<LoadInst>(I))
        if (LI->getPointerOperand() == RuntimeSig)
            return true;
    if (auto *SI = dyn_cast<StoreInst>(I))
        if (SI->getPointerOperand() == RuntimeSig)
            return true;

    // Skip instructions with RACFED-specific names.
    StringRef name = I->getName();
    if (name.contains("sig_add")        ||
        name.contains("racfed_newsig")  ||
        name.contains("backup_run_sig") ||
        name.contains("checking_sign")  ||
        name.contains("checking_value") ||
        name.contains("current")        ||
        name.starts_with("anb."))
        return true;
    return false;
    //collects original instruction same filter as racfed


}

/*
 * Returns true if the BB contains at least one ANB-protectable instruction
 * (integer add or icmp eq on integers).
 */
bool ANBPass::hasANBInstructions(BasicBlock &BB,
                                 GlobalVariable *RuntimeSig) const {
    for (Instruction &I : BB) {
        if (isa<PHINode>(&I))           continue;
        if (I.isTerminator())           continue;
        if (isa<DbgInfoIntrinsic>(&I))  continue;

        if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
            if (BO->getOpcode() == Instruction::Add &&
                BO->getType()->isIntegerTy())
                return true;
        }
        if (auto *CI = dyn_cast<ICmpInst>(&I)) {
            if (CI->getPredicate() == ICmpInst::ICMP_EQ &&
                CI->getOperand(0)->getType()->isIntegerTy())
                return true;
        }
    }
    return false;
}

void ANBPass::createSignature(Function &F) {
    std::random_device rd;
    std::mt19937 rng(rd());
    uint32_t randomBB;

    for (BasicBlock &BB : F) {
        if (BB.isEntryBlock())
            randomBB = dist32(rng);
        compileTimeSig.insert(std::pair(&BB, randomBB));
    }

}
/*
 * Inter-BB check (RACFED-style, but arithmetic / non-skippable).
 *
 * When entering a new BB the signature should be different from the
 * snapshot saved at the start of the previous BB.
 * The idea: each ANB check adds +1 to runtime_sig when the
 * instruction executed correctly.  If the +1 was skipped,
 * runtime_sig == anb_prev_sig  →  diff == 0  →  udiv 1/0  →  SIGFPE.
 */
void ANBPass::checkJumpSig(BasicBlock &BB,
                           GlobalVariable *RuntimeSig,
                           GlobalVariable *PrevSig) {
    if (BB.isEntryBlock()) return;
//------no more RACFED-----------//
    // Skip RACFED-injected blocks
    StringRef bbName = BB.getName();
    if (bbName.contains_insensitive("racfed") ||
        bbName.contains_insensitive("rafced") ||
        bbName.contains_insensitive("errbb")  ||
        bbName.contains_insensitive("verification"))
        return;

    // Insert at the very beginning of the BB (after PHIs)
    IRBuilder<> B(&*BB.getFirstInsertionPt());
    Type *I64 = B.getInt64Ty();

    Value *prev = B.CreateLoad(I64, PrevSig,    "anb.prev_sig");
    Value *cur  = B.CreateLoad(I64, RuntimeSig,  "anb.cur_sig");
    
    printSig(*BB.getModule(), B, prev, "checkJumpSig - anb_prev_sig");
    printSig(*BB.getModule(), B, cur,  "checkJumpSig - runtime_sig (cur)");

    Value *diff = B.CreateSub(cur, prev,          "anb.sig_diff");

    // udiv 1 / diff  →  SIGFPE when diff == 0 (instruction was skipped)
    // When diff > 0 (normal), result is 0 (integer division) → no effect
    Value *check  = B.CreateUDiv(ConstantInt::get(I64, 1), diff,
                                 "anb.div_check");
    Value *newSig = B.CreateAdd(cur, check, "anb.sig_checked");
    B.CreateStore(newSig, RuntimeSig);
    
    printSig(*BB.getModule(), B, newSig, "checkJumpSig - runtime_sig (after check)");
}

/*
 * Saves the current runtime_sig into anb_prev_sig before the BB terminator.
 * This snapshot will be compared at the start of the next BB.
 */
void ANBPass::saveSnapshot(BasicBlock &BB,
                           GlobalVariable *RuntimeSig,
                           GlobalVariable *PrevSig) {
    Instruction *Term = BB.getTerminator();
    if (!Term) return;

    IRBuilder<> B(Term);
    Type *I64 = B.getInt64Ty();

    Value *snap = B.CreateLoad(I64, RuntimeSig, "anb.snap");
    B.CreateStore(snap, PrevSig);
}


PreservedAnalyses ANBPass::run(llvm::Module &Md, ModuleAnalysisManager &AM) {
    getFuncAnnotations(Md, FuncAnnotations);
    Type *I64 = Type::getInt64Ty(Md.getContext());
    
    GlobalVariable *RuntimeSig = Md.getGlobalVariable("runtime_sig");
    if (!RuntimeSig) {
        RuntimeSig = new GlobalVariable(
            Md, I64,
            /*isConstant=*/false,
            GlobalValue::ExternalLinkage,
            ConstantInt::get(I64, 0),
            "runtime_sig");
    }

    // ── Create (or retrieve) anb_prev_sig global ──────────────────────────
    GlobalVariable *PrevSig = Md.getGlobalVariable("anb_prev_sig");
    if (!PrevSig) {
        PrevSig = new GlobalVariable(
            Md, I64,
            /*isConstant=*/false,
            GlobalValue::ExternalLinkage,
            ConstantInt::get(I64, 0),
            "anb_prev_sig");
    }

    std::mt19937_64 rng(std::random_device{}());
    // Biases are drawn from [1, A-1] so they are always non-zero.
    std::uniform_int_distribution<uint64_t> biasDist(1, ANB_DEFAULT_A - 1);

    for (Function &Fn : Md) {
        if (!shouldCompile(Fn, FuncAnnotations)) continue;

        // Initialize compile-time signatures for this function
        createSignature(Fn);

        // ── Pass 1: identify BBs with ANB-protectable instructions ────────
        SmallVector<BasicBlock *, 16> anbBBs;
        for (BasicBlock &BB : Fn) {
            StringRef bbName = BB.getName();
            if (bbName.contains_insensitive("racfed") ||
                bbName.contains_insensitive("rafced") ||
                bbName.contains_insensitive("errbb")  ||
                bbName.contains_insensitive("verification"))
                continue;
            if (hasANBInstructions(BB, RuntimeSig))
                anbBBs.push_back(&BB);
        }

        // ── Pass 2: insert inter-BB checks and ANB instrumentation ────────
        for (BasicBlock &BB : Fn) {
//----------------------No more racfed---------------------------------//
            // Skip basic blocks injected by RACFED.
            StringRef bbName = BB.getName();
            if (bbName.contains_insensitive("racfed") ||
                bbName.contains_insensitive("rafced") ||
                bbName.contains_insensitive("errbb")  ||
                bbName.contains_insensitive("verification"))
                continue;
//-----------------------------------------------------------------------//

            bool bbHasANB = std::find(anbBBs.begin(), anbBBs.end(), &BB)
                            != anbBBs.end();

            // ── Entry block: initialise runtime_sig and anb_prev_sig ────────
            if (BB.isEntryBlock()) {
                IRBuilder<> BEntry(&*BB.getFirstInsertionPt());
                uint32_t initSig = compileTimeSig[&BB];
                Value *sigVal = ConstantInt::get(I64, initSig);
                
                // Initialize the runtime signature with the compile-time value
                BEntry.CreateStore(sigVal, RuntimeSig);
                
                // Also save it as the initial snapshot for the next BB
                BEntry.CreateStore(sigVal, PrevSig);
                
                printSig(Md, BEntry, sigVal, "Entry BB Init Signature");
            }

            // ── Inter-BB check (only if this BB has ANB instructions) ──────
            if (bbHasANB) {
                checkJumpSig(BB, RuntimeSig, PrevSig);
            }

            // ── Collect original instructions ─────────────────────────────
            SmallVector<Instruction *, 16> origInstrs;
            for (Instruction &I : BB) {
                if (isa<PHINode>(&I))           continue;
                if (I.isTerminator())           continue;
                if (isa<DbgInfoIntrinsic>(&I))  continue;
                //if (isRACFEDInstruction(&I, RuntimeSig)) continue;
                origInstrs.push_back(&I);
            }

            // ── ANB-encode each protectable instruction ───────────────────
            for (Instruction *I : origInstrs) {
                auto *BO = dyn_cast<BinaryOperator>(I);
                if (BO && BO->getOpcode() == Instruction::Add &&
                    BO->getType()->isIntegerTy()) {  // i32 e i64
                    Instruction *insertPt = I->getNextNode();
                    if (!insertPt) continue;
                    IRBuilder<> B(insertPt);
                    uint64_t Ba = biasDist(rng);
                    uint64_t Bb = biasDist(rng);

                    // ZExt a i64 se necessario (es. operandi i32)
                    Value *op0 = BO->getOperand(0);
                    Value *op1 = BO->getOperand(1);
                    if (!op0->getType()->isIntegerTy(64))
                        op0 = B.CreateZExt(op0, I64, "anb.op0_64");
                    if (!op1->getType()->isIntegerTy(64))
                        op1 = B.CreateZExt(op1, I64, "anb.op1_64");

                    ANBValue av = createANBadd(B, op0, Ba, op1, Bb, ANB_DEFAULT_A);
                    // error64 = (z_enc % A) - Bz
                    // == 0 when the add executed correctly,
                    // != 0 when a or b was wrong/skipped.
                    Type   *I128   = Type::getInt128Ty(Md.getContext());
                    Value  *A128   = ConstantInt::get(I128, ANB_DEFAULT_A);
                    Value  *Bz128  = ConstantInt::get(I128, av.B);
                    // z encoded modulo A == Bz128
                    Value  *zModA  = B.CreateURem(av.encoded, A128, "anb.z_mod_A");
                    
                    // (true = 1 se ok, false = 0 se alterato)
                    Value  *is_correct_i1 = B.CreateICmpEQ(zModA, Bz128, "anb.is_correct_i1");

                    Value  *increment_i64 = B.CreateZExt(is_correct_i1, I64, "anb.increment_i64");

                    // runtime_sig += increment_i64 (1 se ok, 0 se manomesso)
                    Value *sig    = B.CreateLoad(I64, RuntimeSig, "anb.sig_load");
                    printSig(Md, B, sig, "Before Add Instr - runtime_sig");
                    
                    Value *newSig = B.CreateAdd(sig, increment_i64, "anb.sig_upd");
                    B.CreateStore(newSig, RuntimeSig);
                    printSig(Md, B, newSig, "After Add Instr - runtime_sig");
                    continue;
                }
                //------compare------------//
                auto *CI = dyn_cast<ICmpInst>(I);
                if (CI && CI->getPredicate() == ICmpInst::ICMP_EQ &&
                    CI->getOperand(0)->getType()->isIntegerTy())  // i32 e i64
                {
                    Instruction *insertPt = I->getNextNode();
                    if (!insertPt) continue;
                    IRBuilder<> B(insertPt);

                    uint64_t Ba = biasDist(rng);
                    uint64_t Bb = biasDist(rng);

                    // sigPos = (Ba - Bb) % A   [compile-time expected value]
                    uint64_t sigPos = (ANB_DEFAULT_A + (Ba % ANB_DEFAULT_A)
                                      - (Bb % ANB_DEFAULT_A)) % ANB_DEFAULT_A;

                    // sigCond = ((a+Ba) - (b+Bb)) % A
                    // When a==b: sigCond == sigPos -> error == 0
                    // When a!=b: sigCond != sigPos -> error != 0
                    Value *sigCond = createANBSetEq(B,
                                                    CI->getOperand(0), Ba,
                                                    CI->getOperand(1), Bb,
                                                    ANB_DEFAULT_A);

                    Value *sigPosV = ConstantInt::get(I64, sigPos);
                    Value *err64   = B.CreateSub(sigCond, sigPosV, "anb.eq_err");

                    // runtime_sig += error
                    Value *sig    = B.CreateLoad(I64, RuntimeSig, "anb.sig_load");
                    printSig(Md, B, sig, "Before Cmp Instr - runtime_sig");
                    Value *newSig = B.CreateAdd(sig, err64, "anb.sig_upd");
                    B.CreateStore(newSig, RuntimeSig);
                    printSig(Md, B, newSig, "After Cmp Instr - runtime_sig");
                    continue;
                }

            } // for each original instruction

            // ── Snapshot: save runtime_sig before leaving this BB ──────────
            if (bbHasANB) {
                saveSnapshot(BB, RuntimeSig, PrevSig);
            }
        }     // for each BB
    }         // for each function

    return PreservedAnalyses::none();
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "ANBPass", "v0.1",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "anb-encode") {
                        MPM.addPass(ANBPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}
