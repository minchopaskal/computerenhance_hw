#pragma once

#include "emu8086.h"
#include "memory.h"
#include "scripts/instr_opcodes.h"

#include <string>

namespace emu8086 {

constexpr uint8_t SR_MASK = 0x1C;

// First byte(FB)
constexpr uint8_t FB_REG_MASK = 0x07;
constexpr uint8_t IMM_W_MASK = 0x08;
constexpr uint8_t W_MASK = 0x01;
constexpr uint8_t Z_MASK = 0x01;
constexpr uint8_t D_MASK = 0x02;
constexpr uint8_t S_MASK = 0x02;
constexpr uint8_t V_MASK = 0x02;

// Second byte(SB)
constexpr uint8_t MOD_MASK = 0xC0;
constexpr uint8_t SB_REG_MASK = 0x38;
constexpr uint8_t REGMEM_MASK = 0x07;

enum class OperandType {
	None,

	Label,
	Immediate,
	Accumulator,
	Register,
	SegmentRegister,
	EffectiveAddress,
	DirectAccess,
	FarProc,
};

struct Operand {
	OperandType type;
	union {
		struct {
			int16_t far_proc_ip;
		};
		int8_t jmp_offset;
		int16_t imm_value;
		RegisterName reg;
		EffectiveAddress eff_addr;
		SegmentRegisterName seg_reg;
	};
	union {
		struct {
			int16_t far_proc_cs;
		};
		int16_t displacement;
		uint16_t direct_access;
	};
	uint8_t seg_prefix = 0xff;
};

struct Instruction {
	std::string name;
	InstructionType type;
	InstructionOpcode opcode;
	int __special_instr_idx; // For internal use only

	// Populated by decoder
	Operand operands[2];
	struct {
		bool wide = false;
		bool dest = false;
		bool locked = false;
		bool repeated = false;
		bool string_op = false;
		bool far = false;
	} flags;
};

Instruction get_instruction(uint8_t opcode);
Instruction get_special_instruction(const Instruction& ins, uint8_t snd_byte);

bool is_seg_prefix(uint8_t b);

RegisterName get_register(uint8_t idx, bool wide);
EffectiveAddress get_eff_addr(uint8_t idx);
SegmentRegisterName get_seg_reg(uint8_t idx);

}