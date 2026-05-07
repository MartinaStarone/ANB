//
// Created by martina on 05/05/26.
//

#include "ANB.h"

#include <cstdint>
#include <llvm/IR/InlineAsm.h>

const uint64_t TWO_TO_64_MOD_A = 2^64 % A;

uint64_t seteq(uint64_t a, uint64_t Ba, uint64_t b, uint64_t Bb) {
    uint64_t result = (a-Ba)<=(b-Bb);
    uint64_t diff = a-b;

    uint64_t sigCond = diff %A;
    uint64_t sigPos = Ba-Bb;
    uint64_t sigNeg = (TWO_TO_64_MOD_A + sigPos)%A;

    if (result) {
        result += (A-1);
    }else {
        result += (sigPos-sigNeg);
    }
    result +=sigCond;
    return result;
}
