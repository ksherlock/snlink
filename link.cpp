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

#if 0
template<class Input, class UnaryPredicate>
Input::iterator find_if(Input &&input, UnaryPredicate p) {
	return std::find_if(input.begin(), input.end(), p);
}
#endif

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
		"snlink [-v1XCS] [-o outputfile] [-t type] [-D name=value] [-l 0|1|2] file.obj ...\n"
		"       -v: be verbose\n"
		"       -o: specify output file\n"
		"       -t: specify output file type\n"
		"       -1: OMF Version 1\n"
		"       -X: Inhibit Expressload\n"
		"       -C: Inhibit OMF Compression\n"
		"       -S: Inhibit OMF Super Records\n"
		"       -D: define an equate\n"
		"       -l: link type\n"

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

void print_segments(std::vector<omf::segment> &segments) {

	fputs("Segments:\n", stdout);
	for (auto &seg : segments) {
		printf("%u (%-10s): $%06x\n", seg.segnum, seg.loadname.c_str(), (uint32_t)seg.data.size());
	}
}

std::vector<std::string> collect_groups(std::vector<sn_unit> &units) {

	std::vector<std::string> rv;

	// check for empty...
	bool has_empty = false;
	for (auto &u : units) {
		for (auto &s : u.sections) {
			if (s.group_id) continue;
			rv.emplace_back();
			has_empty = true;
			break;
		}
		if (has_empty) break;
	}

	for (auto &u : units) {
		for (auto &g : u.groups) {
			if (std::find(rv.begin(), rv.end(), g.name) == rv.end())
				rv.emplace_back(g.name);
		}
	}
	return rv;
}

std::vector<std::string> collect_sections(std::vector<sn_unit> &units, const std::string &group_name) {

	std::vector<std::string> rv;

	for (auto &u : units) {
		unsigned group_id = 0;
		if (!group_name.empty()) {
			auto g = u.find_group(group_name);
			if (!g) continue;
			group_id = g->group_id;
		}

		for (auto &s : u.sections) {
			if (s.group_id != group_id) continue;
			if (std::find(rv.begin(), rv.end(), s.name) == rv.end())
				rv.emplace_back(s.name);
		}
	}
	return rv;
}

template<>
struct std::hash< std::pair<std::string, std::string> > {
	std::size_t operator()(std::pair<std::string, std::string> const &s ) const noexcept {
		auto s1 = std::hash<std::string>{}(s.first);
		auto s2 = std::hash<std::string>{}(s.second);
		return s1 ^ s2;
	}
};

// link types -
// 0: 1 segment for everything
// 1: 1 segment per group
// 2: 1 segment per section (group/groupend might not work)
std::vector<omf::segment> link_it(std::vector<sn_unit> &units, int type) {


	typedef std::pair<std::string, std::string> key;
	struct value {
		unsigned segnum = 0;
		uint32_t start = 0;
		uint32_t end = 0;
	};

	std::unordered_map<key, value> dict;


	std::vector<omf::segment> rv;

	omf::segment * seg;
	if (type == 0) {
		// 1 segment
		seg = &rv.emplace_back();
		seg->segnum = 1;
	}


	auto groups = collect_groups(units);
	for (auto gname : groups) {

		auto sections = collect_sections(units, gname);

		if (type == 1) {
			// 1 segment per group
			seg = &rv.emplace_back();
			seg->segnum = rv.size();
			seg->loadname = gname;
			// set attributes based on name?
		}


		uint32_t group_offset = seg->data.size();

		for (auto &sname : sections) {


			if (type == 2) {
				// 1 section per segment.
				seg = &rv.emplace_back();
				seg->segnum = rv.size();
				seg->loadname = sname;
				// set attributes based on name?
			}

			uint32_t section_offset = seg->data.size();

			for (auto &u : units) {

				unsigned group_id = 0;
				if (!gname.empty()) {
					auto gg = u.find_group(gname);
					if (!gg) continue;
					group_id = gg->group_id;
				}


				for (auto &s : u.sections) {
					if (s.group_id != group_id) continue;
					if (s.name != sname) continue;

					s.segnum = seg->segnum;
					s.offset = seg->data.size();

					append(seg->data, s.data);

					// also update the relocations...
					for (auto &r : s.relocs) {
						r.address += s.offset;
					}
				}
			}


			auto k = std::make_pair(gname, sname);
			auto v = value { seg->segnum, section_offset, (uint32_t)seg->data.size() };

			dict.emplace(k, v);
		}

		// type 2 can't do group/groupend() ... unless it's 1-section
		if (type == 2 && sections.size() == 1) {
			auto k = std::make_pair(gname, "");
			auto v = value { seg->segnum, 0, (uint32_t)seg->data.size() };
			dict.emplace(k, v);			
		}
		if (type != 2) {
			auto k = std::make_pair(gname, "");
			auto v = value { seg->segnum, group_offset, (uint32_t)seg->data.size() };
			dict.emplace(k, v);
		}
	}


	// now process all the relocation records for group() / groupend() / sect() / sectend()

	for (auto &u : units) {
		for (auto &s : u.sections) {
			for (auto &r : s.relocs) {
				for (auto &e : r.expr) {
					if (e.op == V_SECTION) {
						// internal section reference.

						auto ss = u.find_section(e.value);

						if (!ss) {
							errx(1, "%s: %s: Unable to find section %u",
								u.filename.c_str(), s.name.c_str(), e.value
							);
						}

						e.op = (ss->segnum << 8) | V_OMF;
						e.value = ss->offset;
					}

					if (e.op == V_FN_SECT || e.op == V_FN_SECT_END) {
						auto ss = u.find_section(e.value);
						if (!ss) {
							errx(1, "%s: %s: Unable to find section %u",
								u.filename.c_str(), s.name.c_str(), e.value
							);
						}

						auto gg = ss->group_id ? u.find_group(ss->group_id) : nullptr;
						if (ss->group_id && !gg) {
							errx(1, "%s: %s: Unable to find group %s",
								u.filename.c_str(), s.name.c_str(), gg->name.c_str()
							);				
						}

						auto k = std::make_pair(gg ? gg->name : "", ss->name);
						auto iter = dict.find(k);
						if (iter == dict.end()) {
							errx(1, "%s: %s: Unable to find %s:%s",
								u.filename.c_str(), s.name.c_str(), gg->name.c_str(), s.name.c_str()
							);
						}
						const auto &v = iter->second;
						e.op = (v.segnum << 8) | V_OMF;
						e.value = e.op == V_FN_SECT ? v.start : v.end;
						continue;
					}

					if (e.op == V_FN_GROUP || e.op == V_FN_GROUP_END) {

						#if 0
						if (type == 2) {
							errx(1, "group() and groupend() not supported with section-based segments")
						}
						#endif

						auto gg = u.find_group(e.value);
						if (!gg) {
							errx(1, "%s: %s: Unable to find group %u",
								u.filename.c_str(), s.name.c_str(), e.value
							);
						}

						auto k = std::make_pair(gg->name, std::string());
						auto iter = dict.find(k);
						if (iter == dict.end()) {
							errx(1, "%s: %s: Unable to find group %s",
								u.filename.c_str(), s.name.c_str(), gg->name.c_str()
							);
						}
						const auto &v = iter->second;
						e.op = (v.segnum << 8) | V_OMF;
						e.value = e.op == V_FN_GROUP ? v.start : v.end;
						continue;
					}
				}
			}
		}
	}


	return rv;
}

int main(int argc, char **argv) {

	std::vector<sn_unit> units;
	std::vector<omf::segment> segments;

	int ch;

	std::string outfile = "iigs.omf";

	unsigned file_type = 0xb3;
	unsigned aux_type = 0;
	unsigned link_type = 1;

	unsigned omf_flags = OMF_V2;
	bool verbose = false;

	while ((ch = getopt(argc, argv, "o:D:t:vhX1CSl:")) != -1) {
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
		case 'l': 
			if (*optarg >= '0' && *optarg <= '2')
				link_type = *optarg - '0';
			else
				errx(1, "Bad link type: %s", optarg);
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


	// load all the files...
	for (int i = 0; i < argc; ++i) {
		std::string path(argv[i]);

		auto &unit = units.emplace_back();
		sn_parse_unit(path, unit);
	}

	// merge into omf segments.
	segments = link_it(units, link_type);

	// build a symbol table.
	for (auto &u : units) {

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

			auto ss = u.find_section(sym.section_id);

			if (!ss) {
				errx(1, "%s: %s Unable to find section %u",
					u.filename.c_str(), name.c_str(), sym.section_id
				);
			}

			symbol_table.emplace(name, sym_info{ ss->segnum, ss->offset + sym.value });
		}
	}

	// update the extern references
	for (auto &u : units) {
		for (auto &s : u.sections) {
			for (auto &r : s.relocs) {
				for (auto &e : r.expr) {
					if (e.op == V_EXTERN) {
						// extern symbol.

						auto ee = u.find_extern(e.value);

						if (!ee) {
							errx(1, "%s: %s: Unable to find symbol %u",
								u.filename.c_str(), s.name.c_str(), e.value
							);
						}

						auto iter = symbol_table.find(ee->name);
						if (iter == symbol_table.end()) {
							errx(1, "%s: %s Unable to find extern symbol %s",
								u.filename.c_str(), s.name.c_str(), ee->name.c_str()
							);
						}
						const auto &si = iter->second;

						e.value = si.value;
						// could be an EQU
						if (si.segnum == 0) {
							e.op = V_CONST;
						} else {
							e.op = (si.segnum << 8) | V_OMF;
						}
					}
				}
				simplify(r);
			}
		}
	}

	// final resolution into OMF relocation records
	resolve(units, segments);

	if (verbose) {
		print_symbols();
		print_segments(segments);
	}

	save_omf(outfile, segments, omf_flags);
	set_file_type(outfile, file_type, aux_type);



	return 0;
}