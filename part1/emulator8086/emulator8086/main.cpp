#include <cstdio>
#include <filesystem>

#include "decoder.h"
#include "instructions.h"
#include "emulator.h"

#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(STREAM_OUT, "Usage: %s <filename> [<param>,]\n", argv[0]);
		fprintf(STREAM_OUT, "\tSupported parameters:\n");
		fprintf(STREAM_OUT, "\t\t-exec Execute the decoded instructions\n");
		fprintf(STREAM_OUT, "\t\t-print Print asm of decoded instructions\n");
		return 1;
	}

	const char *filename = argv[1];

	bool exec = false;
	bool print = false;
	for (int i = 2; i < argc; ++i) {
		if (strncmp(argv[i], "-exec", 5) == 0) {
			exec = true;
		}
		if (strncmp(argv[i], "-print", 5) == 0) {
			print = true;
		}
	}

	auto filesize = std::filesystem::file_size(filename);

	FILE *f = fopen(filename, "rb");
	if (f) {
		std::unique_ptr<uint8_t[]> source(new uint8_t[filesize]);
		auto bytes_read = fread(source.get(), sizeof(uint8_t), filesize, f);
		
		if (bytes_read != filesize) {
			fprintf(STREAM_ERR, "Failed to read file %s!\n", filename);
			return 1;
		}

		emu8086::decode(source.get(), filesize);

		if (print) {
			emu8086::print_asm();
		}

		if (exec) {
			auto instructions = emu8086::get_decoded_instructions();
			emu8086::emulate(instructions);
			emu8086::print_state();
			fprintf(STREAM_OUT, "\n");
		}
	}

	return 0;
}