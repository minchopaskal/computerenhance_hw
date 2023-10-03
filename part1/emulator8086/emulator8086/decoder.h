#pragma once

#include "emu8086.h"
#include "instructions.h"

#include <vector>

namespace emu8086 {

/**
 * @breif Output disassembled binary instructions to std output as asm
 */
void decode(const std::uint8_t *source, std::size_t source_size);

std::vector<Instruction> &get_decoded_instructions();
void print_instr(const Instruction &instr, std::size_t idx = -1);
void print_asm();

}