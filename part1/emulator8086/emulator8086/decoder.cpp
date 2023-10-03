#include "decoder.h"
#include "instructions.h"

#include <bit>
#include <cassert>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace emu8086 {

std::vector<Instruction> decoded;
std::unordered_map<std::size_t, int> labels;

enum MemoryMode {
	NO_DISPLACEMENT = 0b00,
	SHORT = 0b01,
	WIDE = 0b10, 
	REGISTER = 0b11,
};

int16_t bitwise_abs(int16_t num) {
	int16_t mask = num >> 15;
	num = num ^ mask;

	return num - mask;
}

struct RegMemLike {
	uint8_t reg;
	uint8_t seg_reg;
	bool wide;
	bool sign_extended;
};

[[nodiscard]] RegMemLike handle_regmemlike(const uint8_t *&source, Instruction &instr, bool force_wide = false) {
	const uint8_t opcode = *source++;
	const uint8_t mem = *source++;

	const bool dest = (opcode & D_MASK) != 0;
	const bool wide = (opcode & W_MASK) || force_wide;
	const bool sign_extended = (opcode & S_MASK) != 0;

	const uint8_t mode = (mem & MOD_MASK) >> 6;
	const uint8_t reg = (mem & SB_REG_MASK) >> 3;
	const uint8_t regmem = (mem & REGMEM_MASK);
	const uint8_t seg_reg = (mem & SR_MASK) >> 3;

	instr.flags.dest = dest;
	instr.flags.wide = wide;

	if (mode == MemoryMode::REGISTER) {
		instr.operands[0].type = OperandType::Register;
		instr.operands[0].reg = get_register(regmem, wide);
	} else {
		instr.operands[0].type = OperandType::EffectiveAddress;
		instr.operands[0].eff_addr = get_eff_addr(regmem);

		if (mode == MemoryMode::SHORT) {
			int8_t disp = *source++;
			if (is_seg_prefix(static_cast<uint8_t>(disp))) {
				instr.operands[0].seg_prefix = disp;
				disp = *source++;
			}
			instr.operands[0].displacement = disp;
		} else if (mode == MemoryMode::WIDE || regmem == 0x6) {
			uint8_t disp_l = *source++;
			if (is_seg_prefix(disp_l)) {
				instr.operands[0].seg_prefix = disp_l;
				disp_l = *source++;
			}
			uint8_t disp_h = *source++;
			int16_t disp = disp_l | (int16_t(bool(disp_h)) * (disp_h << 8) + int16_t(!bool(disp_h)) * (disp_l & 0x8000));
			instr.operands[0].displacement = disp;

			if (mode != MemoryMode::WIDE) { // Direct acess
				instr.operands[0].type = OperandType::DirectAccess;
			}
		}
	}

	return { reg, seg_reg, wide, sign_extended };
}

void handle_regmem_reg(const uint8_t *&source, Instruction &instr) {
	RegMemLike res = handle_regmemlike(source, instr);

	instr.operands[1].type = OperandType::Register;
	instr.operands[1].reg = get_register(res.reg, res.wide);
}

int16_t get_imm_data(const uint8_t *&source, bool wide, bool sign_extended = false) {
	int8_t data_l = *source++;
	uint8_t data_h = wide && !sign_extended ? *source++ : 0;
	int16_t data = *reinterpret_cast<uint8_t *>(&data_l) | (data_h << 8);

	return wide && !sign_extended ? data : data_l;
}

void handle_imm_regmem(const uint8_t *&source, Instruction &instr, bool sign_ext = false) {
	RegMemLike res = handle_regmemlike(source, instr);

	// TODO: this is actually a hack since we don't handle
	// the case when the instruction does not have a destination bit
	// this must be in the codegen.
	instr.flags.dest = false;
	res.sign_extended = sign_ext ? res.sign_extended : false;

	instr.operands[1].type = OperandType::Immediate;
	instr.operands[1].imm_value = get_imm_data(source, res.wide, res.sign_extended);
}

void handle_imm(const uint8_t *&source, Instruction &instr, bool wide) {
	++source;

	instr.operands[0].type = OperandType::Immediate;
	instr.operands[0].imm_value = get_imm_data(source, wide);
}

void handle_imm_reg(const uint8_t *&source, Instruction &instr) {
	
	const uint8_t opcode = *source++;
	const uint8_t reg = (opcode & FB_REG_MASK);

	instr.flags.wide = (opcode & IMM_W_MASK);

	instr.operands[0].type = OperandType::Register;
	instr.operands[0].reg = get_register(reg, instr.flags.wide);

	instr.operands[1].type = OperandType::Immediate;
	instr.operands[1].imm_value = get_imm_data(source, instr.flags.wide);
}

void handle_mem_acc(const uint8_t *&source, Instruction &instr) {
	
	const uint8_t opcode = *source++;

	instr.flags.wide = (opcode & W_MASK);
	uint16_t addr = *source++;
	addr = addr | (*source++ << 8);

	instr.operands[1].type = OperandType::DirectAccess;
	instr.operands[1].direct_access = addr;

	instr.operands[0].type = OperandType::Accumulator;
}

void handle_acc_mem(const uint8_t *&source, Instruction &instr) {
	
	const uint8_t opcode = *source++;

	instr.flags.wide = (opcode & W_MASK);
	uint16_t addr = *source++;
	addr = addr | (*source++ << 8);
	
	instr.operands[0].type = OperandType::DirectAccess;
	instr.operands[0].direct_access = addr;

	instr.operands[1].type = OperandType::Accumulator;
}

void handle_imm_acc(const uint8_t *&source, Instruction &instr) {
	
	const uint8_t opcode = *source++;

	instr.flags.wide = (opcode & W_MASK);
	int16_t data = get_imm_data(source, instr.flags.wide);
	
	instr.operands[0].type = OperandType::Accumulator;

	instr.operands[1].type = OperandType::Immediate;
	instr.operands[1].imm_value = data;
}

void handle_jmp(const uint8_t *&source, Instruction &instr) {
	
	const uint8_t opcode = *source++;

	int8_t offset = *source++;

	// Each jmp instruction is 2 bytes and offset is given in bytes
	// so if we want to count offset by instructions we divide by 2
	int instr_offset = (offset >> 1);
	std::size_t line = decoded.size() + instr_offset;
	if (!labels.contains(line)) {
		labels.insert({ line, static_cast<int>(labels.size()) });
	}
	
	instr.operands[0].type = OperandType::Label;
	instr.operands[0].jmp_offset = offset;
}

void handle_regmem(const uint8_t *&source, Instruction &instr) {
	std::ignore = handle_regmemlike(source, instr);
}

void handle_regmem_1(const uint8_t *&source, Instruction &instr) {
	std::ignore = handle_regmemlike(source, instr);
	instr.operands[1].type = OperandType::Immediate;
	instr.operands[1].imm_value = 1;
}

void handle_regmem_CL(const uint8_t *&source, Instruction &instr) {
	std::ignore = handle_regmemlike(source, instr);
	instr.operands[1].type = OperandType::Register;
	instr.operands[1].reg = RegisterName::CL;

	instr.flags.dest = false;
}

void handle_mem_reg(const uint8_t *&source, Instruction &instr) {
	auto res = handle_regmemlike(source, instr);
	instr.operands[1] = instr.operands[0];

	instr.operands[0].type = OperandType::Register;
	instr.operands[0].reg = get_register(res.reg, res.wide);
}

void handle_esc(const uint8_t *&source, Instruction &instr) {
	auto fb = *source;
	auto sb = *(source + 1);

	int16_t data = 0;
	data = data | (fb & 0x07);
	data = data | ((sb & 0x38));

	auto res = handle_regmemlike(source, instr);
	instr.operands[1] = instr.operands[0];

	instr.operands[0].type = OperandType::Immediate;
	instr.operands[0].imm_value = data;
}

void handle_reg(const uint8_t *&source, Instruction &instr) {
	const uint8_t opcode = *source++;
	const uint8_t reg = (opcode & FB_REG_MASK);
	instr.operands[0].type = OperandType::Register;
	instr.operands[0].reg = get_register(reg, true);
}

void handle_seg_reg(const uint8_t *&source, Instruction &instr) {
	const uint8_t opcode = *source++;
	const uint8_t seg_reg = (opcode & SR_MASK) >> 3;

	instr.operands[0].type = OperandType::SegmentRegister;
	instr.operands[0].seg_reg = get_seg_reg(seg_reg);
}

void handle_sr_regmem(const uint8_t *&source, Instruction &instr) {
	auto res = handle_regmemlike(source, instr, true);

	instr.operands[1].type = OperandType::SegmentRegister;
	instr.operands[1].seg_reg = get_seg_reg(res.seg_reg);
}

void handle_reg_acc(const uint8_t *&source, Instruction &instr) {
	const uint8_t opcode = *source++;
	const uint8_t reg = (opcode & FB_REG_MASK);

	instr.operands[0].type = OperandType::Accumulator;

	instr.operands[1].type = OperandType::Register;
	instr.operands[1].reg = get_register(reg, true);

	instr.flags.wide = true;
}

void handle_fixed_port(const uint8_t *&source, Instruction &instr) {
	auto opcode = *source++;

	instr.flags.wide = (opcode & W_MASK);
	instr.flags.dest = (opcode & D_MASK) >> 1;

	uint8_t data = *source++;

	instr.operands[0].type = OperandType::Accumulator;

	instr.operands[1].type = OperandType::Immediate;
	instr.operands[1].imm_value = data;
}

void handle_var_port(const uint8_t *&source, Instruction &instr) {
	auto opcode = *source++;

	instr.flags.wide = (opcode & W_MASK);
	instr.flags.dest = (opcode & D_MASK) >> 1;

	instr.operands[0].type = OperandType::Accumulator;

	instr.operands[1].type = OperandType::Register;
	instr.operands[1].reg = RegisterName::DX;
}

void handle_far_proc(const uint8_t *&source, Instruction &instr) {
	++source;
	auto ip_inc = get_imm_data(source, true);
	auto cs = get_imm_data(source, true);

	instr.operands[0].type = OperandType::FarProc;
	instr.operands[0].far_proc_ip = ip_inc;
	instr.operands[0].far_proc_cs = cs;
}

void decode(const uint8_t *source, std::size_t source_size) {
	auto og_src = source;

	uint8_t sr_prefix = 0xff;
	bool locked = false;
	bool repeated = false;
	while (source < og_src + source_size) {
		const uint8_t opcode = *source;

		Instruction instr = get_instruction(opcode);
		bool special = false;
		if (instr.type == InstructionType::Special) {
			special = true;
			auto snd_byte = *(source+1);
			instr = get_special_instruction(instr, snd_byte);
		}

		switch (instr.type) {
		case InstructionType::Esc:
			handle_esc(source, instr);
			break;
		case InstructionType::Imm8:
			handle_imm(source, instr, false);
			break;
		case InstructionType::NearProc:
		case InstructionType::Imm16:
			handle_imm(source, instr, true);
			break;
		case InstructionType::FixedPort:
			handle_fixed_port(source, instr);
			break;
		case InstructionType::VariablePort:
			handle_var_port(source, instr);
			break;
		case InstructionType::Reg:
			handle_reg(source, instr);
			break;
		case InstructionType::RegMem:
			handle_regmem(source, instr);
			break;
		case InstructionType::RegMem_Far:
			handle_regmem(source, instr);
			instr.flags.far = true;
			break;
		case InstructionType::Mem_Reg:
			handle_mem_reg(source, instr);
			break;
		case InstructionType::RegMem_1:
			handle_regmem_1(source, instr);
			break;
		case InstructionType::RegMem_CL:
			handle_regmem_CL(source, instr);
			break;
		case InstructionType::Reg_Acc:
			handle_reg_acc(source, instr);
			break;
		case InstructionType::SR:
			handle_seg_reg(source, instr);
			break;
		case InstructionType::SR_RegMem:
			handle_sr_regmem(source, instr);
			break;
		case InstructionType::SingleByte:
			++source;
			if (instr.opcode == InstructionOpcode::lock) {
				locked = true;
				continue;
			}
			if (instr.opcode == InstructionOpcode::rep) {
				repeated = true;
				continue;
			}
			break;
		case InstructionType::StringManip:
			++source;
			instr.flags.wide = (opcode & W_MASK);
			instr.flags.string_op = true;
			break;
		case InstructionType::SkipSecond:
			source += 2;
			break;
		case InstructionType::RegMem_Reg:
			handle_regmem_reg(source, instr);
			break;
		case InstructionType::Imm_RegMem:
			handle_imm_regmem(source, instr);
			break;
		case InstructionType::Imm_RegMem_SE:
			handle_imm_regmem(source, instr, true);
			break;
		case InstructionType::Imm_Reg:
			handle_imm_reg(source, instr);
			break;
		case InstructionType::Mem_Acc:
			handle_mem_acc(source, instr);
			break;
		case InstructionType::Acc_Mem:
			handle_acc_mem(source, instr);
			break;
		case InstructionType::Imm_Acc:
			handle_imm_acc(source, instr);
			break;
		case InstructionType::Jmp:
			handle_jmp(source, instr);
			break;
		case InstructionType::FarProc:
			handle_far_proc(source, instr);
			break;
		case InstructionType::SegmentPrefix:
			++source;
			sr_prefix = (opcode & SR_MASK) >> 3;
			continue;
			break;
		default:
			fprintf(STREAM_OUT, "instruction not supported! Type: %d Idx: %llu\n", static_cast<int>(instr.type), decoded.size());
			exit(1);
			break; // just in case
		}

		if (instr.operands[1].type != OperandType::None && instr.flags.dest) {
			std::swap(instr.operands[0], instr.operands[1]);
			instr.flags.dest = false;
		}

		if (sr_prefix < 4) {
			for (int i = 0; i < 2; ++i) {
				if (instr.operands[i].type == OperandType::EffectiveAddress || instr.operands[i].type == OperandType::DirectAccess) {
					instr.operands[i].seg_prefix = sr_prefix;
					sr_prefix = 0xff;
				}
			}
		}

		if (locked) {
			instr.flags.locked = true;
			locked = false;
		}

		if (repeated) {
			instr.flags.repeated = true;
			repeated = false;
		}

		decoded.push_back(instr);
		//print_instr(instr, decoded.size() - 1);
	}
}

std::vector<Instruction> &get_decoded_instructions() {
	return decoded;
}

void print_operand(const Operand &op, bool wide, std::size_t idx, bool print_width_specifier, bool snd = false) {
	if (op.type == OperandType::None) {
		return;
	}

	fprintf(STREAM_OUT, "%s ", snd ? "," : "");

	char specifier[5] = { '\0' };
	int len = sprintf(specifier, "%s", wide ? "word" : "byte");
	specifier[4] = '\0';

	switch (op.type) {
	case OperandType::Immediate:
		fprintf(STREAM_OUT, "%d", op.imm_value);
		break;
	case OperandType::EffectiveAddress:
		if (print_width_specifier) {
			fprintf(STREAM_OUT, "%s ", specifier);
		}
		if (op.seg_prefix != 0xff) {
			fprintf(STREAM_OUT, "%s:", sr_to_str[op.seg_prefix]);
		}
		fprintf(STREAM_OUT, "[%s", eff_addr_to_str[static_cast<int>(op.eff_addr)]);
		if (op.displacement > 0) {
			fprintf(STREAM_OUT, " + %d", op.displacement);
		}
		if (op.displacement < 0) {
			fprintf(STREAM_OUT, " - %d", bitwise_abs(op.displacement));
		}
		fprintf(STREAM_OUT, "]");
		break;
	case OperandType::DirectAccess:
		if (print_width_specifier) {
			fprintf(STREAM_OUT, "%s ", specifier);
		}
		if (op.seg_prefix != 0xff) {
			fprintf(STREAM_OUT, "%s:", sr_to_str[op.seg_prefix]);
		}
		fprintf(STREAM_OUT, "[%d]", op.direct_access);
		break;
	case OperandType::Register:
		fprintf(STREAM_OUT, "%s", reg_to_str[static_cast<int>(op.reg)]);
		break;
	case OperandType::SegmentRegister:
		fprintf(STREAM_OUT, "%s", sr_to_str[static_cast<int>(op.seg_reg)]);
		break;
	case OperandType::Accumulator:
		fprintf(STREAM_OUT, "%s", wide ? "ax" : "al");
		break;
	case OperandType::Label:
	{
		int instr_offset = (op.jmp_offset >> 1);
		std::size_t line = idx + instr_offset;
		if (auto it = labels.find(line); it != labels.end()) {
			fprintf(STREAM_OUT, "label%d", it->second);
		} else {
			fprintf(STREAM_OUT, "LABEL_NOT_FOUND");
		}

		break;
	}
	case OperandType::FarProc:
		fprintf(STREAM_OUT, "%d:%d", op.far_proc_cs, op.far_proc_ip);
		break;
	case OperandType::None:
		break;
	}
}

void print_instr(const Instruction &instr, std::size_t idx) {
	auto &op0 = instr.operands[0];
	auto &op1 = instr.operands[1];

	bool width_specifier = (instr.opcode != InstructionOpcode::call && instr.opcode != InstructionOpcode::jmp);

	fprintf(STREAM_OUT, "%s%s%s%s%s",
		(instr.flags.locked ? "lock " : ""),
		(instr.flags.repeated ? "rep " : ""),
		instr.name.c_str(),
		(instr.flags.string_op ? (instr.flags.wide ? "w" : "b") : ""),
		(instr.flags.far && instr.operands[0].type != OperandType::FarProc ? " far " : "")
	);
	print_operand(op0, instr.flags.wide, idx, (op1.type == OperandType::Immediate || op1.type == OperandType::None) && width_specifier);
	print_operand(op1, instr.flags.wide, idx, op0.type == OperandType::Immediate && width_specifier, true);

	// print label if necessary
	if (auto it = labels.find(idx); it != labels.end()) {
		fprintf(STREAM_OUT, "\nlabel%s:", std::to_string(it->second).c_str());
	}

}

void print_asm() {
	fprintf(STREAM_OUT, "bits 16\n");

	for (std::size_t i = 0; i < decoded.size(); ++i) {
		auto &instr = decoded[i];

		print_instr(instr, i);
		fprintf(STREAM_OUT, "\n");
	}
}

}
