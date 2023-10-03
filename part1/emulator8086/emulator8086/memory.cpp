#include "memory.h"

#include <cstdio>

namespace emu8086 {

Register registers[8];
SegmentRegister seg_regs[4];
Flag flags;

namespace detail {

Flag from(uint16_t value) {
	return static_cast<Flag>(value);
}

uint16_t to(Flag f) {
	return static_cast<uint16_t>(f);
}

}

Register *get_registers() {
	return registers;
}

uint16_t get_register_data(RegisterName reg) {
	int r = static_cast<int>(reg);
	if (r >= 8) {
		return registers[r - 8].data;
	} else if (r >= 4) {
		return registers[r - 4].high;
	} else {
		return registers[r].low;
	}
}

void set_register(RegisterName reg, uint16_t data) {
	auto r = static_cast<int>(reg);
	if (r >= 8) {
		registers[r - 8].data = data;
	} else if (r >= 4) {
		registers[r - 4].high = (data & 0xFF);
	} else {
		registers[r].low = (data & 0xFF);
	}
}

void print_flags() {
	fprintf(STREAM_OUT, "flags:");
	auto f = detail::to(flags);
	for (int i = 15; i >= 0; --i) {
		if (f & (1 << i)) {
			fprintf(STREAM_OUT, " %s", flag_name[i]);
		}
	}
}

void print_state() {
	static constexpr int idxs[8] = { 0, 3, 1, 2, 4, 5, 6, 7 };
	fprintf(STREAM_OUT, "\n==========================================\n");
	for (int i = 0; i < 8; ++i) {
		fprintf(STREAM_OUT, "%s -> %04x\n", reg_to_str[idxs[i] + 8], registers[idxs[i]].data);
	}

	for (int i = 0; i < 4; ++i) {
		fprintf(STREAM_OUT, "%s -> %04x\n", sr_to_str[i], seg_regs[i]);
	}

	print_flags();
}

SegmentRegister* get_srs() {
	return seg_regs;
}

SegmentRegister get_sr(SegmentRegisterName sr) {
	return seg_regs[static_cast<int>(sr)];
}

void set_sr(SegmentRegisterName sr, uint16_t data) {
	seg_regs[static_cast<int>(sr)] = data;
}

Flag operator|(Flag f1, Flag f2) {
	return detail::from(detail::to(f1) | detail::to(f2));
}

bool flags_set(Flag flag) {
	return (detail::to(flag) & detail::to(flags)) != 0;
}

void set_flags(Flag f) {
	flags = f;
}

std::vector<Flag> get_flags(Flag flag) {
	std::vector<Flag> result;
	auto fs = detail::to(flag);
	for (int i = 0; i < 16; ++i) {
		if (fs & (1 << i)) {
			result.push_back(detail::from(1 << i));
		}
	}

	return result;
}

void set_flag(Flag flag, bool set) {
	if (set) {
		flags = flags | flag;
	} else {
		flags = detail::from(detail::to(flags) & ~detail::to(flag));
	}
}

} // namespace emu8086
