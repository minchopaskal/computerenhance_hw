#include "emulator.h"

#include "memory.h"

#include "decoder.h"

#include <concepts>

namespace emu8086 {

void handle_mov(const Instruction &instr) {
	uint16_t src_data = 0;

	switch (instr.operands[1].type) {
	case OperandType::Immediate:
		src_data = instr.operands[1].imm_value;
		break;
	case OperandType::Register:
		src_data = get_register_data(instr.operands[1].reg);
		break;
	case OperandType::SegmentRegister:
		src_data = get_sr(instr.operands[1].seg_reg);
		break;
	default:
		break;
	}

	switch (instr.operands[0].type) {
	case OperandType::Register:
		set_register(instr.operands[0].reg, src_data);
		break;
	case OperandType::SegmentRegister:
		set_sr(instr.operands[0].seg_reg, src_data);
		break;
	default:
		break;
	}
}

template <std::integral T>
int popcount(T x) {
#ifdef _MSC_VER
	if constexpr (sizeof(x) <= 2) {
		return __popcnt16(uint16_t(x));
	} else if constexpr (sizeof(x) <= 4) {
		return __popcnt(uint32_t(x));
	} else {
		return __popcnt64(uint64_t(x));
	}
#elif __GNUC__
	return __builtin_popcount(uint32_t(res));
#endif
}

template <class T>
concept BinaryOp = requires(T x, uint16_t v1, uint16_t v2) {
	{ x(v1, v2) } -> std::same_as<uint16_t>;
};

bool check_parity(uint16_t res) {
	int cnt = popcount(res);

	return cnt % 2 == 0;
}

template <BinaryOp F>
void manage_common_artm_flags(F op, uint16_t dest, uint16_t src, bool wide) {
	uint16_t res = op(dest, src);

	set_flag(Flag::ZF, res == 0);
	uint16_t sign_mask = wide ? 0x8000 : 0x80;
	set_flag(Flag::SF, res & sign_mask);

	// 8086 only checks parity of lowest byte
	set_flag(Flag::PF, check_parity(res & 0xFF));
}

struct BinaryOpRes {
	uint16_t dest;
	uint16_t src;
};

template <BinaryOp F>
BinaryOpRes handle_artm_instr(const Instruction &instr, F op) {
	uint16_t src_data = 0;
	switch (instr.operands[1].type) {
	case OperandType::Immediate:
		src_data = instr.operands[1].imm_value;
		break;
	case OperandType::Register:
		src_data = get_register_data(instr.operands[1].reg);
		break;
	default:
		break;
	}

	uint16_t dest_data = 0;
	switch (instr.operands[0].type) {
	case OperandType::Register:
	{
		dest_data = get_register_data(instr.operands[0].reg);
		break;
	}
	default:
		break;
	}

	manage_common_artm_flags(op, dest_data, src_data, instr.flags.wide);
	return { dest_data, src_data };
}

void handle_add(const Instruction &instr) {
	auto op = [](uint16_t x, uint16_t y) { return uint16_t(x + y); };

	auto data = handle_artm_instr(instr, op);

	auto res = op(data.dest, data.src);
	set_register(instr.operands[0].reg, res);

	int32_t width_mask = instr.flags.wide ? 0xFFFF : 0xFF;
	bool carry = data.dest + data.src > width_mask;
	set_flag(Flag::CF, carry);
	
	uint16_t sign_bit = instr.flags.wide ? 0x8000 : 0x80;
	uint16_t max_val = instr.flags.wide ? 0x7FFF : 0x7F;
	uint16_t min_val = instr.flags.wide ? 0x8000 : 0x80;
	bool overflow = (!(data.dest & sign_bit) && data.dest > uint16_t(max_val - data.src)) ||
		((data.dest & sign_bit) && data.dest < uint16_t(min_val - data.src));
	set_flag(Flag::OF, overflow);
	
	uint8_t dest_nimble = data.dest & 0xF;
	uint8_t src_nimble = data.src & 0xF;
	bool aux_carry = dest_nimble + src_nimble > 0xF;
	set_flag(Flag::AF, aux_carry);
}

void handle_sub(const Instruction &instr, bool is_cmp = false) {
	auto op = [](uint16_t x, uint16_t y) { return uint16_t(x - y); };

	auto data = handle_artm_instr(instr, op);

	uint32_t dest = data.dest;
	uint32_t src = data.src;

	auto res = op(data.dest, data.src);
	if (!is_cmp) {
		set_register(instr.operands[0].reg, res);
	}
	 
	bool carry = data.src > data.dest;
	set_flag(Flag::CF, carry);
	
	uint16_t sign_bit = instr.flags.wide ? 0x8000 : 0x80;
	uint16_t max_val = instr.flags.wide ? 0x7FFF : 0x7F;
	uint16_t min_val = instr.flags.wide ? 0x8000 : 0x80;
	bool overflow = (!(data.dest & sign_bit) && data.dest > uint16_t(min_val + data.src)) ||
		((data.dest & sign_bit) && data.dest < uint16_t(max_val + data.src));
	set_flag(Flag::OF, overflow);
	
	uint8_t aux_carry = 0xF;
	uint8_t dest_nimble = data.dest & aux_carry;
	uint8_t src_nimble = data.src & aux_carry;
	set_flag(Flag::AF, src_nimble > dest_nimble);
}

void handle_cmp(const Instruction &instr) {
	handle_sub(instr, true);
}

void emulate(const std::vector<Instruction> &instructions) {
	Register *regs = get_registers();
	SegmentRegister *srs = get_srs();

	for (auto &instr : instructions) {
		switch (instr.opcode) {
		case InstructionOpcode::mov:
			handle_mov(instr);
			break;
		case InstructionOpcode::add:
			handle_add(instr);
			break;
		case InstructionOpcode::sub:
			handle_sub(instr);
			break;
		case InstructionOpcode::cmp:
			handle_cmp(instr);
			break;
		default:
			fprintf(STREAM_OUT, "Ignoring instruction %s\n", instr.name.c_str());
			break;
		}

		print_instr(instr);
		fprintf(STREAM_OUT, " ; ");
		print_flags();
		fprintf(STREAM_OUT, "\n");
	}
}

} // namespace emu8086
