#include <algorithm>
#include <string>
#include <vector>

#include <cstring>

#include <err.h>

#include "mapped_file.h"
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


[[maybe_unused]] static std::vector<uint8_t> &append(std::vector<uint8_t> &v, const std::vector<uint8_t> &w) {
	v.insert(v.end(), w.begin(), w.end());
	return v;	
}

template<class InputIt>
static std::vector<uint8_t> &append(std::vector<uint8_t> &v, InputIt first, InputIt last) {
	v.insert(v.end(), first, last);
	return v;
}



sn_file *sn_unit::find_file(const std::string &name) {
	auto iter = std::find_if(files.begin(), files.end(), [&name](const auto &x){
		return x.name == name;
	});
	return iter == files.end() ? nullptr : &*iter;
}

sn_file *sn_unit::find_file(unsigned id) {
	auto iter = std::find_if(files.begin(), files.end(), [id](const auto &x){
		return x.file_id == id;
	});
	return iter == files.end() ? nullptr : &*iter;
}

sn_group *sn_unit::find_group(const std::string &name) {
	auto iter = std::find_if(groups.begin(), groups.end(), [&name](const auto &x){
		return x.name == name;
	});
	return iter == groups.end() ? nullptr : &*iter;
}

sn_group *sn_unit::find_group(unsigned id) {
	auto iter = std::find_if(groups.begin(), groups.end(), [id](const auto &x){
		return x.group_id == id;
	});
	return iter == groups.end() ? nullptr : &*iter;
}

sn_section *sn_unit::find_section(const std::string &name) {
	auto iter = std::find_if(sections.begin(), sections.end(), [&name](const auto &x){
		return x.name == name;
	});
	return iter == sections.end() ? nullptr : &*iter;
}

sn_section *sn_unit::find_section(unsigned id) {
	auto iter = std::find_if(sections.begin(), sections.end(), [id](const auto &x){
		return x.section_id == id;
	});
	return iter == sections.end() ? nullptr : &*iter;
}

sn_symbol *sn_unit::find_extern(const std::string &name) {
	auto iter = std::find_if(externs.begin(), externs.end(), [&name](const auto &x){
		return x.name == name;
	});
	return iter == externs.end() ? nullptr : &*iter;
}

sn_symbol *sn_unit::find_extern(unsigned id) {
	auto iter = std::find_if(externs.begin(), externs.end(), [id](const auto &x){
		return x.symbol_id == id;
	});
	return iter == externs.end() ? nullptr : &*iter;
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

inline std::runtime_error bad_opcode(std::string msg, unsigned opcode) {
	char buffer[16];
	snprintf(buffer, sizeof(buffer), ": $%02x", opcode & 0xff);

	msg.append(buffer);
	return std::runtime_error(msg);
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
		case V_EXTERN:
			if (std::distance(it, end) < 2) throw eof();
			value = read_16(it);
			out.expr.emplace_back(expr_token{op, value});
			break;

		default:
			throw bad_opcode("Unknown relocation expression opcode", op);
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

iter parse_local_symbol(iter it, iter end, sn_symbol &symbol) {

	if (std::distance(it, end) < 7)  throw eof();
	symbol.symbol_id = 0;
	symbol.section_id = read_16(it);
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



void sn_parse_unit(const std::string &path, sn_unit &unit) {

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
				append(current->data, it, it + size);
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
				// ds reserved space.
				if (!current)
					throw std::runtime_error("No active section");

				if (std::distance(it, end) < 4)
					throw eof();

				uint32_t size = read_32(it);
				current->data.resize(current->data.size() + size, 0x00);
				// current->bss_size += size;
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
				// global symbol.
				auto &symbol = unit.globals.emplace_back();
				it = parse_global_symbol(it, end, symbol);
				break;
			}
			case 0x0e: {
				// extern symbol
				auto &symbol = unit.externs.emplace_back();
				it = parse_extern_symbol(it, end, symbol);
				break;
			}

			case 0x12: {
				// non-global symbol (included with /g)
				auto &symbol = unit.locals.emplace_back();
				it = parse_local_symbol(it, end, symbol);
				// it = skip_local_symbol(it, end);
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

			case 0x2a: // 32-bit regs?
			case 0x18: // 16-bit regs?
			case 0x16: // 8-bit regs?
				// regs pc=$12345678, etc
				// uint8_t unknown, uint32_t value, uint8_t register, uint8_t unknown.
				// register mapping: 00=a; 02=x; 04=y; 06=s;08=pc;0b=p;0c=d;0e=db
				if (std::distance(it, end) < 7)
					throw eof();
				it += 7;
				break;

			default:
				throw bad_opcode("Unknown opcode", op);

			}

		}

		throw eof();
	} catch (std::runtime_error &e) {
		errx(1, "%s: %s at offset $%lx", path.c_str(), e.what(), std::distance(mf.begin(), it) - 1);
	}
}
