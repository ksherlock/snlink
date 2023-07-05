
#include <vector>
#include <string>
#include <cstdint>

struct sn_group {
	std::string name;
	unsigned flags = 0;
	unsigned group_id = 0;
};

struct sn_file {
	std::string name;
	unsigned file_id = 0;
};

struct expr_token {
	unsigned op = 0;
	uint32_t value = 0; // constant, extern symbol id, or section id
};

struct sn_reloc {
	unsigned type = 0;
	uint32_t address = 0;
	unsigned file_id = 0;
	unsigned line = 0;
	std::vector<expr_token> expr;
};

struct sn_symbol {
	std::string name;
	unsigned symbol_id = 0;
	unsigned section_id = 0;
	uint32_t value = 0;
};

struct sn_section {
	std::string name;
	unsigned section_id = 0;
	unsigned group_id = 0;
	unsigned flags = 0; // unknown meaning.

	unsigned bss_size = 0;
	std::vector<uint8_t> data;
	std::vector<sn_reloc> relocs;

	// omf-data
	unsigned segnum = 0;
	uint32_t offset = 0;
};

struct sn_unit {
	// translation unit.
	std::string filename;
	std::vector<sn_section> sections;
	std::vector<sn_group> groups;
	std::vector<sn_file> files;
	std::vector<sn_symbol> symbols;
	std::vector<sn_symbol> externs;
};

