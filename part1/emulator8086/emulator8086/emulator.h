#pragma once

#include "instructions.h"

#include <vector>

namespace emu8086 {

void emulate(const std::vector<Instruction> &instructions);

} // namespace emu8086
