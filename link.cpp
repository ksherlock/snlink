/*
 * SN to OMF Linker.
 *
 */
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <cstring>

#include <err.h>
#include <unistd.h>

#include "mapped_file.h"
#include "link.h"
#include "sn.h"
#include "omf.h"

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


std::vector<uint8_t> &append(std::vector<uint8_t> &v, const std::vector<uint8_t> &w) {
	v.insert(v.end(), w.begin(), w.end());
	return v;	
}

template<class InputIt>
std::vector<uint8_t> &append(std::vector<uint8_t> &v, InputIt first, InputIt last) {
	v.insert(v.end(), first, last);
	return v;
}


template<class A, class B>
bool contains(std::unordered_map<A, B> &d, const A& k) {
	return d.find(k) != d.end();
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
		case V_EXTERN:
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
	fputs(
		"snlink [-v] [-o outputfile] file.obj ...\n"
		"       -o: specify output file\n"
		"       -1: OMF Version 1"
		"       -X: Inhinit Expressload\n"
		"       -C: Inhibit OMF Compression\n"
		"       -S: Inhibit OMF Super Records\n"

		, stdout
	);
	return rv;
}


extern void simplify(std::vector<expr_token> &v);
extern void print(const std::vector<expr_token> &v);

void resolve(const std::vector<sn_unit> &units, std::vector<omf::segment> &segments);

int set_file_type(const std::string &path, uint16_t file_type, uint32_t aux_type);


bool parse_ft(const std::string &s, unsigned &ftype, unsigned &atype) {

	const static struct {
		const std::string name;
		unsigned ftype;
		unsigned atype;
	} types[] = {
		{ "cda", 0xb9, 0x0000 },
		{ "driver", 0xbb, 0x0000 },
		{ "dvr", 0xbb, 0x0000 },
		{ "exe", 0xb5, 0x0000 },
		{ "fst", 0xbd, 0x0000 },
		{ "ldf", 0xbc, 0x0000 },
		{ "loadfile", 0xbc, 0x0000 },
		{ "nda", 0xb8, 0x0000 },
		{ "pif", 0xb6, 0x0000 },
		{ "rtl", 0xb4, 0x0000 },
		{ "s16", 0xb3, 0x0000 },
		{ "tif", 0xb7, 0x0000 },
		{ "tol", 0xba, 0x0000 },
		{ "tool", 0xba, 0x0000 },
	};

	auto iter = std::lower_bound(std::begin(types), std::end(types), s, [](const auto &e, const std::string &s){
		return e.name < s;
	});
	if (iter == std::end(types)) return false;
	if (s != iter->name) return false;
	ftype = iter->ftype;
	atype = iter->atype;
	return true;
}

int main(int argc, char **argv) {

	std::vector<sn_unit> units;
	std::vector<omf::segment> segments;

	int ch;

	std::string outfile = "iigs.omf";

	unsigned file_type = 0xb3;
	unsigned aux_type = 0;

	unsigned omf_flags = OMF_V2;
	bool verbose = false;

	while ((ch = getopt(argc, argv, "o:D:t:vhX1CS")) != -1) {
		switch(ch) {
		case 'v': verbose = true; break;
		case 'o': outfile = optarg; break;
		case 'h': return usage(0);
		case 'X': omf_flags |= OMF_NO_EXPRESS; break;
		case '1': omf_flags |= OMF_V1; break;
		case 'C': omf_flags |= OMF_NO_COMPRESS; break;
		case 'S': omf_flags |= OMF_NO_SUPER; break;
		case 't':
			if (!parse_ft(optarg, file_type, aux_type)) {
				errx(1, "Bad filetype: %s", optarg);
			}
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

	// check for duplicate symbols?

	unsigned sn = 1;
	omf::segment &seg = segments.emplace_back();
	seg.segnum = sn;


	#if 0
	struct group_info {
		unsigned flags;
		omf::segment *segment = nullptr;
	};
	std::unordered_map<std::string, group_info> groups;
	#endif

	struct sym_info {
		uint32_t segnum = 0;
		uint32_t value = 0;
	};

	std::unordered_map<std::string, sym_info> symbol_table;

	// simple case - link into 1 segment.
	for (auto &u : units) {
		for (auto &s : u.sections) {
			if (s.bss_size && s.data.size()) {
				errx(1, "%s: section %s has bss and data",
					u.filename.c_str(), s.name.c_str()
				);
			}
			if (s.bss_size) continue; // later
			s.segnum = seg.segnum;
			s.offset = seg.data.size();

			append(seg.data, s.data);

			// also update the relocations...
			for (auto &r : s.relocs) {
				r.address += s.offset;
			}

		}
		uint32_t offset = seg.data.size();
		for (auto &s : u.sections) {
			if (!s.bss_size) continue;
			s.segnum = seg.segnum;
			s.offset = offset;
			seg.reserved_space += s.bss_size;
			offset += s.bss_size;

			// also update the relocations...
			for (auto &r : s.relocs) {
				r.address += s.offset;
			}
		}

		// process the symbols....
		for (const auto &sym : u.symbols) {
			const auto &name = sym.name;
			auto dupe_iter = symbol_table.find(name);
			auto dupe = dupe_iter != symbol_table.end();
			// duplicate constants are ok...
			if (!sym.section_id) {

				if (!dupe) {
					symbol_table.emplace(name, sym_info{0, sym.value });
					continue;
				}
				const auto &other = dupe_iter->second;
				if (other.segnum == 0 && other.value == sym.value)
					continue;
			}
			if (dupe) {
				warnx("%s: Duplicate symbol %s",
					u.filename.c_str(), name.c_str()
				);
				continue;
			}

			auto iter = std::find_if(u.sections.begin(), u.sections.end(),[&sym](const auto &s){
				return s.section_id == sym.section_id;
			});

			if (iter == u.sections.end()) {
				errx(1, "%s: %s Unable to find section %u",
					u.filename.c_str(), name.c_str(), sym.section_id
				);
			}

			symbol_table.emplace(name, sym_info{ iter->segnum, iter->offset + sym.value });
		}
	}

	// now update the relocation expressions
	// and replace extern/section references with OMF segments.
	for (auto &u : units) {
		for (auto &s : u.sections) {
			for (auto &r : s.relocs) {
				for (auto &e : r.expr) {
					if (e.op == V_EXTERN) {
						// extern symbol.

						auto iter1 = std::find_if(u.externs.begin(), u.externs.end(), [&e](const auto &x){
							return x.symbol_id == e.value;
						});
						if (iter1 == u.externs.end()) {
							errx(1, "%s: %s: Unable to find symbol %u",
								u.filename.c_str(), s.name.c_str(), e.value
							);
						}

						auto iter2 = symbol_table.find(iter1->name);
						if (iter2 == symbol_table.end()) {
							errx(1, "%s: %s Unable to find extern symbol %s",
								u.filename.c_str(), s.name.c_str(), iter1->name.c_str()
							);
						}
						const auto &si = iter2->second;

						e.value = si.value;
						// could be a EQU
						if (si.segnum == 0) {
							e.op = V_CONST;
						} else {
							e.op = (si.segnum << 8) | V_OMF;
						}
					}
					if (e.op == V_SECTION) {
						// internal section reference.

						auto iter = std::find_if(u.sections.begin(), u.sections.end(), [&e](const auto &x){
							return x.section_id == e.value;
						});
						if (iter == u.sections.end()) {
							errx(1, "%s: %s: Unable to find section %u",
								u.filename.c_str(), s.name.c_str(), e.value
							);
						}

						const auto &ss = *iter;

						e.op = (ss.segnum << 8) | V_OMF;
						e.value = ss.offset;
					}
				}
				simplify(r.expr);
			}
		}
	}

	resolve(units, segments);

	save_omf(outfile, segments, omf_flags);
	set_file_type(outfile, file_type, aux_type);

	if (verbose) {
		// print the symbol table.
		for (auto &kv : symbol_table) {
			printf("%-20s %02x/%04x\n", kv.first.c_str(), kv.second.segnum, kv.second.value);
		}
	}

	return 0;
}