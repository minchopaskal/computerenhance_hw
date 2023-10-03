#pragma once

#include "emu8086.h"

#include <cstdint>
#include <vector>

namespace emu8086 {

enum class RegisterName {
	AL = 0, CL, DL, BL, AH, CH, DH, BH,
	AX, CX, DX, BX, SP, BP, SI, DI,
};

enum class SegmentRegisterName {
	ES = 0,
	CS,
	SS,
	DS,
};

enum class EffectiveAddress {
	BX_SI = 0,
	BX_DI,
	BP_SI,
	BP_DI,
	SI,
	DI,
	BP,
	BX,
};

enum class Flag : uint16_t {
	EMPTY = 0,

	CF = (1 << 0),
	PF = (1 << 2),
	AF = (1 << 4),
	ZF = (1 << 6),
	SF = (1 << 7),
	TF = (1 << 8),
	IF = (1 << 9),
	DF = (1 << 10),
	OF = (1 << 11),
};

Flag operator|(Flag f1, Flag f2);

static const char *flag_name[16] = {
	"CF",
	"UnknownFlag1",
	"PF",
	"UnknownFlag3",
	"AF",
	"UnknownFlag5",
	"ZF",
	"SF",
	"TF",
	"IF",
	"DF",
	"OF",
	"UnknownFlag12",
	"UnknownFlag13",
	"UnknownFlag14",
	"UnknownFlag15",
};

static const char *reg_to_str[16] = {
	"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh",
	"ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
};

static const char *eff_addr_to_str[8] = {
	"bx + si",
	"bx + di",
	"bp + si",
	"bp + di",
	"si",
	"di",
	"bp",
	"bx",
};

static const char *sr_to_str[4] = {
	"es", "cs", "ss", "ds"
};

struct Register {
	union {
		uint16_t data = 0;

		// little-endian
		struct {
			int8_t low;
			uint8_t high;
		};
	};
};

using SegmentRegister = uint16_t;

Register *get_registers();
/**
 * short registers's data is returned in the LSB
 */
uint16_t get_register_data(RegisterName reg);
void set_register(RegisterName reg, uint16_t data);

SegmentRegister *get_srs();
SegmentRegister get_sr(SegmentRegisterName sr);
void set_sr(SegmentRegisterName sr, uint16_t data);

bool flags_set(Flag flag);
void set_flags(Flag flags);
std::vector<Flag> get_flags(Flag flag);
void set_flag(Flag flag, bool set);

void print_flags();
void print_state();

} // namespace emu8086
