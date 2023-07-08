/*
 * SN to OMF Linker.
 *
 */

#include <string>
#include <algorithm>
#include <unordered_map>
#include <cstdio>

#include <unistd.h>
#include <err.h>
#include <sysexits.h>

/* old version of stdlib have this stuff in utility */
#if __has_include(<charconv>)
#define HAVE_CHARCONV
#include <charconv>
#endif


#include "sn.h"
#include "omf.h"

extern void simplify(std::vector<expr_token> &v);
extern void simplify(sn_reloc &r);
extern void print(const std::vector<expr_token> &v);

void resolve(const std::vector<sn_unit> &units, std::vector<omf::segment> &segments);

int set_file_type(const std::string &path, uint16_t file_type, uint32_t aux_type);


struct sym_info {
	uint32_t segnum = 0;
	uint32_t value = 0;
};

std::unordered_map<std::string, sym_info> symbol_table;


static std::vector<uint8_t> &append(std::vector<uint8_t> &v, const std::vector<uint8_t> &w) {
	v.insert(v.end(), w.begin(), w.end());
	return v;	
}

template<class InputIt>
static std::vector<uint8_t> &append(std::vector<uint8_t> &v, InputIt first, InputIt last) {
	v.insert(v.end(), first, last);
	return v;
}





/* older std libraries lack charconv and std::from_chars */
static bool parse_number(const char *begin, const char *end, uint32_t &value, int base = 10) {

#if defined(HAVE_CHARCONV)
	auto r =  std::from_chars(begin, end, value, base);
	if (r.ec != std::errc() || r.ptr != end) return false;
#else
	auto xerrno = errno;
	errno = 0;
	char *ptr = nullptr;
	value = std::strtoul(begin, &ptr, base);
	std::swap(errno, xerrno);
	if (xerrno || ptr != end) {
		return false;
	}
#endif

	return true;
}

int usage(int rv) {
	fputs(
		"snlink [-v1XCS] [-o outputfile] [-t type] [-D name=value] file.obj ...\n"
		"       -v: be verbose\n"
		"       -o: specify output file\n"
		"       -t: specify output file type\n"
		"       -1: OMF Version 1\n"
		"       -X: Inhibit Expressload\n"
		"       -C: Inhibit OMF Compression\n"
		"       -S: Inhibit OMF Super Records\n"
		"       -D: define an equate\n"

		, stdout
	);
	return rv;
}


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


static void add_define(std::string str) {
	/* -D key[=value]
 		value = 0x, $, % or base 10 */

	uint32_t value = 0;

	auto ix = str.find('=');
	if (ix == 0) usage(EX_USAGE);
	if (ix == str.npos) {
		value = 1;
	} else {

		int base = 10;
		auto pos = ++ix;

		char c = str[pos]; /* returns 0 if == size */

		switch(c) {
			case '%':
				base = 2; ++pos; break;
			case '$':
				base = 16; ++pos; break;
			case '0':
				c = str[pos+1];
				if (c == 'x' || c == 'X') {
					base = 16; pos += 2;					
				}
				break;
		}
		if (!parse_number(str.data() + pos, str.data() + str.length(), value, base))
			usage(EX_USAGE);

		str.resize(ix-1);
	}

	symbol_table.emplace(str, sym_info{ 0, value });
}


void print_symbols() {

	struct xsym_info {
		std::string name;
		uint32_t segnum = 0;
		uint32_t value = 0;
	};

	std::vector<xsym_info> table;

	if (symbol_table.empty()) return;


	int len = 0;
	table.reserve(symbol_table.size());
	for (const auto &kv : symbol_table) {
		table.emplace_back(xsym_info{kv.first, kv.second.segnum, kv.second.value});

		len = std::max(len, (int)kv.first.length());		
	}
	// also include local symbols?

	// alpha-sort
	std::sort(table.begin(), table.end(), [](const auto &a, const auto &b){
		return a.name < b.name;
	});

	fputs("\nSymbol table, alphabetical order:\n", stdout);
	for (auto &e : table) {
		if (e.segnum)
			printf("%-*s=%02x/%04x\n", len, e.name.c_str(), e.segnum, e.value);
		else 
			printf("%-*s=%08x\n", len, e.name.c_str(), e.value);
	}


	// numeric-sort
	std::sort(table.begin(), table.end(), [](const auto &a, const auto &b){
		return std::pair(a.segnum, a.value) < std::pair(b.segnum, b.value);
	});


	fputs("\nSymbol table, numerical order:\n", stdout);
	for (auto &e : table) {
		if (e.segnum)
			printf("%-*s=%02x/%04x\n", len, e.name.c_str(), e.segnum, e.value);
		else 
			printf("%-*s=%08x\n", len, e.name.c_str(), e.value);
	}


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
			break;
		case 'D':
			// -D key=value
			add_define(optarg);
			break;
		default: return usage(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) usage(0);


	// 1. load all the files...
	for (int i = 0; i < argc; ++i) {
		std::string path(argv[i]);

		auto &unit = units.emplace_back();
		sn_parse_unit(path, unit);
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
		for (const auto &sym : u.globals) {
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
				simplify(r);
			}
		}
	}

	resolve(units, segments);

	save_omf(outfile, segments, omf_flags);
	set_file_type(outfile, file_type, aux_type);

	if (verbose)
		print_symbols();

	return 0;
}