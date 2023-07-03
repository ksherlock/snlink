/*
 * SN to OMF Linker.
 *
 */
#include <string>
#include <vector>

#include <cstring>

#include <err.h>
#include <unistd.h>

#include "mapped_file.h"
#include "link.h"
#include "sn.h"


typedef mapped_file::iterator iter;


template<class T>
uint8_t read_8(T &iter) {
	uint8_t tmp = *iter;
	++iter;
	return tmp;
}

template<class T>
uint16_t read_16(T &iter) {
	uint16_t tmp = 0;

	tmp |= *iter << 0;
	++iter;
	tmp |= *iter << 8;
	++iter;
	return tmp;
}

template<class T>
uint32_t read_32(T &iter) {
	uint32_t tmp = 0;

	tmp |= *iter << 0;
	++iter;
	tmp |= *iter << 8;
	++iter;
	tmp |= *iter << 16;
	++iter;
	tmp |= *iter << 24;
	++iter;


	return tmp;
}

template<class T>
std::string read_cstring(T &iter) {
	std::string s;
	for(;;) {
		uint8_t c = *iter;
		++iter;
		if (!c) break;
		s.push_back(c);
	}
	return s;
}


template<class T>
std::string read_pstring(T &iter) {
	std::string s;
	unsigned  size = *iter;
	++iter;
	s.reserve(size);
	while (size--) {
		uint8_t c = *iter;
		++iter;
		s.push_back(c);
	}
	return s;
}

// std::unordered_map<std::string, symbol> symbol_table;


/*
 * Symbols... 
 * opcode = 0x0c
 * if section id == 0, this is an absolute value
 * (eg, ABC EQU $1234)
 * labels are exported with section id/offset
 * symbol for bss section is identical;
 * SET/= are exported w/ section id (bug?)
 */


inline std::runtime_error eof(void) {
	return std::runtime_error("Unexpected EOF");
}

iter parse_reloc(iter it, iter end, sn_reloc &out) {

	unsigned tokens = 1;

	if (std::distance(it, end) < 4) throw eof();

	out.type = *it++;
	out.address = read_16(it);

	while(tokens) {
		uint32_t value;

		--tokens;
		if (it == end) throw eof();

		unsigned op = *it++;
		switch(op) {

		case OP_EQ:
		case OP_NE:
		case OP_LE:
		case OP_LT:
		case OP_GE:
		case OP_GT:
		case OP_ADD:
		case OP_SUB:
		case OP_MUL:
		case OP_DIV:
		case OP_AND:
		case OP_OR:
		case OP_XOR:
		case OP_LSHIFT:
		case OP_RSHIFT:
		case OP_MOD:
			out.expr.emplace_back(expr_token{ op, 0 });
			tokens += 2;
			break;

		case V_CONST:
			if (std::distance(it, end) < 4) throw eof();
			value = read_32(it);
			out.expr.emplace_back(expr_token{op, value});
			break;
		case V_SECTION:
		case V_SYMBOL:
			if (std::distance(it, end) < 2) throw eof();
			value = read_16(it);
			out.expr.emplace_back(expr_token{op, value});
			break;

		default: {
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "$%02x", op);
			std::string tmp("unknown relocation expression opcode: ");
			tmp += buffer;
			throw std::runtime_error(tmp);
			break;
		}
		}
	}
	return it;
}

iter parse_file(iter it, iter end, sn_file &file) {
	if (std::distance(it, end) < 3) throw eof();

	file.file_id = read_16(it);
	unsigned l = *it;
	if (std::distance(it, end) < l + 1) throw eof();
	file.name = read_pstring(it);
	return it;
}


iter parse_group(iter it, iter end, sn_group &group) {
	if (std::distance(it, end) < 4) throw eof();

	group.group_id = read_16(it);
	group.flags = *it++;
	unsigned l = *it;
	if (std::distance(it, end) < l + 1) throw eof();
	group.name = read_pstring(it);
	return it;
}

iter parse_section(iter it, iter end, sn_section &section) {
	if (std::distance(it, end) < 6) throw eof();

	section.section_id = read_16(it);
	section.group_id = read_16(it);
	section.flags = *it++;
	unsigned l = *it;
	if (std::distance(it, end) < l + 1) throw eof();
	section.name = read_pstring(it);
	return it;
}

iter parse_global_symbol(iter it, iter end, sn_symbol &symbol) {

	if (std::distance(it, end) < 9)  throw eof();
	symbol.symbol_id = read_16(it);
	symbol.section_id = read_16(it);
	symbol.value = read_32(it);

	unsigned l = *it;
	if (std::distance(it, end) < l + 1) throw eof();
	symbol.name = read_pstring(it);
	return it;
}

iter parse_bss_symbol(iter it, iter end, sn_symbol &symbol) {

	// no section

	if (std::distance(it, end) < 7)  throw eof();
	symbol.symbol_id = read_16(it);
	symbol.section_id = -1;
	symbol.value = read_32(it);

	unsigned l = *it;
	if (std::distance(it, end) < l + 1) throw eof();
	symbol.name = read_pstring(it);
	return it;
}

iter parse_extern_symbol(iter it, iter end, sn_symbol &symbol) {

	if (std::distance(it, end) < 3)  throw eof();
	symbol.symbol_id = read_16(it);

	unsigned l = *it;
	if (std::distance(it, end) < l + 1) throw eof();
	symbol.name = read_pstring(it);
	return it;
}


iter skip_local_symbol(iter it, iter end) {
	if (std::distance(it, end) < 7)  throw eof();
	it += 6; // uint16_t section_id, uint32_t offset
	unsigned l = *it++;
	if (std::distance(it, end) < l) throw eof();
	it += l;
	return it;
}


/*

1. parse each file into a translation unit.
2. merge data blocks, update offsets
3. copy symbols into a symbol map.
4. resolve relocations

*/
void parse_unit(const std::string &path, sn_unit &unit) {

	// if (verbose) printf("Linking %s\n", path.c_str());

	std::error_code ec;
	mapped_file mf(path, mapped_file::readonly, ec);
	if (ec) {
		errx(1, "Unable to open %s: %s", path.c_str(), ec.message().c_str());
	}

	unsigned current_file = 0;
	unsigned current_line = 0;
	unsigned current_section = 0;
	sn_section *current = nullptr;


	unit.filename = path;

	auto it = mf.begin();
	auto end = mf.end();

	if (std::distance(it, end) < 7) throw eof();

	if (memcmp(it, "LNK\x02", 4)) throw std::runtime_error("Not an SN Object File");
	it += 6;

	try {
		while (it < end) {
			unsigned op = *it++;

			switch(op) {
			case 0x00:
				if (it == end) return;
				throw std::runtime_error("Unexpected EOF segment");

			case 0x02: {
				// data block
				if (!current)
					throw std::runtime_error("No active section");

				if (std::distance(it, end) < 2)
					throw eof();

				unsigned size = read_16(it);
				if (std::distance(it, end) < size)
					throw eof();
				current->blocks.insert(current->blocks.end(), it, it + size);
				it += size;
				break;
			}
			case 0x06: {
				// switch section.
				if (std::distance(it, end) < 2)
					throw eof();
				current_section = read_16(it);
				current = nullptr;
				for (auto &s : unit.sections) {
					if (s.section_id == current_section) {
						current = &s;
						break;
					}
				}
				if (!current) {
					throw std::runtime_error("No active section");
				}
				break;
			}
			case 0x08: {
				// bss reserved data.
				if (!current)
					throw std::runtime_error("No active section");

				if (std::distance(it, end) < 4)
					throw eof();

				uint32_t size = read_32(it);
				current->bss_size += size;
				break;
			}
			case 0x0a: {
				if (!current)
					throw std::runtime_error("No active section");

				auto &reloc = current->relocs.emplace_back();
				it = parse_reloc(it, end, reloc);
				reloc.file_id = current_file;
				reloc.line = current_line;
				break;
			}
			case 0x0c: {
				// symbol.
				auto &symbol = unit.symbols.emplace_back();
				it = parse_global_symbol(it, end, symbol);
				break;
			}
			case 0x0e: {
				// extern symbol
				if (!current)
					throw std::runtime_error("No active section");

				auto &symbol = unit.externs.emplace_back();
				it = parse_extern_symbol(it, end, symbol);
				break;
			}
			case 0x10: {
				auto &section = unit.sections.emplace_back();
				it = parse_section(it, end, section);

				// re-calc current section since it may have re-allocated.
				if (current) {
					current = nullptr;
					for (auto &s : unit.sections) {
						if (s.section_id == current_section) {
							current = &s;
							break;
						}
					}	
				}
				break;
			}
			case 0x12: {
				// bss? - these are local so skip
				// sn_symbol symbol;
				// it = parse_bss_symbol(it, end, symbol);
				// unit.symbols.emplace_back(std::move(symbol));
				it = skip_local_symbol(it, end);
				break;
			}
			case 0x14: {
				auto &group = unit.groups.emplace_back();
				it = parse_group(it, end, group);
				break;
			}
			case 0x1c: {
				auto &file = unit.files.emplace_back();
				it = parse_file(it, end, file);
				break;
			}

			case 0x1e: {
				if (std::distance(it, end) < 6)
					throw eof();
				// set the line number.
				current_file = read_16(it);
				current_line = read_32(it);
				break;
			}
			case 0x22:
				current_line++;
				break;
			case 0x24:
				if (it == end) eof();
				current_line += *it++;
				break;
			case 0x2c: {
				// unknown /z file record.
				if (std::distance(it, end) < 3)
					throw eof();
				it += 3;
				break;
			}
			case 0x28:
				// local symbol record.
				it = skip_local_symbol(it, end);
				break;
			default:
				throw std::runtime_error("Unknown segment: ");

			}

		}

		throw eof();
	} catch (std::runtime_error &e) {
		errx(1, "%s: %s at offset $%lx", path.c_str(), e.what(), std::distance(mf.begin(), it) - 1);
	}


}


int usage(int rv) {
	fputs("snlink [-v] [-o outputfile] file.obj ...\n", stdout);
	return rv;
}

int main(int argc, char **argv) {

	std::vector<sn_unit> units;

	int ch;

	std::string outfile;
	bool verbose = false;

	while ((ch = getopt(argc, argv, "o:D:vh")) != -1) {
		switch(ch) {
		case 'v': verbose = true; break;
		case 'o': outfile = optarg; break;
		case 'h': return usage(0);
		case 'D':
			// -D key=value
			break;
		default: return usage(1);
		}
	}

	argc -= optind;
	argv += optind;

	// 1. load all the files...
	for (int i = 0; i < argc; ++i) {
		std::string path(argv[i]);

		auto &unit = units.emplace_back();
		parse_unit(path, unit);
	}
	// 2. merge segments.

	// 3. resolve relocation records.

	return 0;
}