/*
  nm - display symbol table 
 */

#include <string>
#include <algorithm>
#include <unordered_map>
#include <cstdio>

#include <unistd.h>
#include <err.h>
#include <sysexits.h>

#include "sn.h"

void usage(int rv) {

	fputs(
		"sn-nm [-AefgoPprSuvx] [-t d|o|x] object file ...\n"
		"      -A: write full pathname\n"
		"      -e: only external and static symbols\n"
		"      -f: full output\n"
		"      -g: only external symbols\n"
		"      -G: display for each SN group\n" // custom
		"      -o: octal format\n"
		"      -P: POSIX format\n"
		"      -p: do not sort\n"
		"      -r: reverse sort\n"
		"      -S: display for each SN section\n" // custom
		"      -u: only undefined symbols\n"
		"      -v: sort by value\n"
		"      -x: hexadecimal format\n"
		"      -t: output format (d/o/x)\n"

		, stdout
	);

	exit(rv);
}

int main(int argc, char **argv) {




	bool flag_A = false;
	bool flag_f = false;
	bool flag_v = false;
	bool flag_r = false;
	bool flag_P = false;
	bool flag_p = false;
	bool flag_S = false;
	bool flag_G = false;

	unsigned flag_t = 'x';
	unsigned display = 0;

	int ch;
	while ((ch = getopt(argc, argv, "AefgoPprSuvxt:")) != -1) {
		switch(ch) {
		case 'e': display = 'e'; break;
		case 'g': display = 'g'; break;
		case 'u': display = 'u'; break;

		case 'o': flag_t = 'o'; break;
		case 'x': flag_t = 'x'; break;
		case 't':
			flag_t = optarg[0];
			if (flag_t != 'd' && flag_t != 'o' && flag_t != 'x')
				usage(1);
			break;

		case 'A': flag_A = true; break;
		case 'f': flag_f = true; break; // full output -- also include sections????
		case 'P': flag_P = true; break;
		case 'p': flag_p = true; break;
		case 'S': flag_S = true; break;
		case 'G': flag_G = true; break;
		case 'v': flag_v = true; break;
		case 'r': flag_r = true; break;

		default:
			usage(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) usage(0);

	bool include_global = false;
	bool include_local = false;
	bool include_extern = false;
	switch(display) {
	case 'g':
		include_global = true;
		break;
	case 'e':
		include_global = true;
		include_local = true;
		break;
	case 'u':
		include_extern = true;
		break;
	default:
		include_local = true;
		include_global = true;
		include_extern = true;
		break;
	}


	for (int i = 0; i < argc; ++i) {
		sn_unit u;

		struct xsym {
			std::string name;
			uint32_t value = 0;
			unsigned type = 0;
		};

		sn_parse_unit(argv[i], u);


		std::vector<xsym> syms;

		// incorporate group/section info for local/global symbols???

		if (include_local) {
			for (const auto &e : u.locals) {
				unsigned type = 't'; // local text
				if (e.section_id == 0) type = 'a'; // local absolute

				syms.push_back(xsym{e.name, e.value, type});
			}
		}

		if (include_global) {
			for (const auto &e : u.globals) {
				unsigned type = 'T'; // global text
				if (e.section_id == 0) type = 'A'; // global absolute

				syms.push_back(xsym{e.name, e.value, type});
			}
		}


		if (include_extern) {
			for (const auto &e : u.externs) {
				syms.push_back(xsym{e.name, 0, 'U'});
			}
		}

		// sort...
		if (!flag_p) {
			std::sort(syms.begin(), syms.end(), [=](const auto &a, const auto &b){
				if (flag_v) return a.value < b.value;
				return a.name < b.name;
			});
		}
		if (flag_r) {
			std::reverse(syms.begin(), syms.end());
		}

		// todo -- proper output
		if (!flag_A) printf("\n%s:\n", argv[i]);
		for (const auto &e : syms) {

			char value_str[32];

			switch(flag_t) {
			case 'o':
				snprintf(value_str, sizeof(value_str), "%010o", e.value);
				break;
			case 'd':
				snprintf(value_str, sizeof(value_str), "%010u", e.value);
				break;
			case 'x':
			default:
				snprintf(value_str, sizeof(value_str), "%010x", e.value);
				break;
			}

			if (flag_A) printf("%s: ", argv[i]);

			if (flag_P) {
				printf("%s %c", e.name.c_str(), e.type);
				if (e.type != 'U') printf(" %s", value_str);
				fputs("\n", stdout);
			} else {

				fputs(e.type == 'U' ? "        " : value_str, stdout);
				printf(" %c %s\n", e.type, e.name.c_str());
			}
		}
	}


	return 0;
}