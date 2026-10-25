#pragma once
#include <array>
#include <cstdint>

namespace mb::fm
{
/** One algorithm: for each operator (index 0 = OP1) a bitmask of the operators that modulate it,
    a carrier mask and the operator that carries the feedback loop. Higher operators always
    modulate lower ones, so evaluating OP6 → OP1 resolves every dependency. */
struct Algorithm
{
    std::array<uint8_t, 6> mods {};
    uint8_t carriers = 0;
    int feedbackOp = 5;   // 0-based
};

const std::array<Algorithm, 32>& algorithms();

/** Column layout for the algorithm graph: stacks of operators, top to bottom, carrier last. */
struct AlgoLayout { int stacks[6][6]; int stackLen[6]; int numStacks; };
AlgoLayout layoutFor (int algorithm);
} // namespace mb::fm
