#include <vector>
#include <string>
#include <cstdint>

struct sn_group {
	std::string name;
	unsigned group_id = 0;
	unsigned flags = 0;

	// unconfirmed flag bits:
	// $80 = bss
	// $40 = word
	// $20 = org (but org not stored anywhere...)

	bool operator==(const std::string &s) const {
		return name == s;
	}
};

struct sn_file {
	std::string name;
	unsigned file_id = 0;

	bool operator==(const std::string &s) const {
		return name == s;
	}
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

	bool operator==(const std::string &s) const {
		return name == s;
	}

};

struct sn_section {
	std::string name;
	unsigned section_id = 0;
	unsigned group_id = 0;
	unsigned flags = 0;
	// unconfirmed flag bits:
	// 8 = 32-bit alignment
	// 4 = 16-bit aignment
	// 2 = 8-bit alignment

	unsigned bss_size = 0;
	std::vector<uint8_t> data;
	std::vector<sn_reloc> relocs;

	// omf-data
	unsigned segnum = 0;
	uint32_t offset = 0;


	bool operator==(const std::string &s) const {
		return name == s;
	}
};

struct sn_unit {
	// translation unit.
	std::string filename;
	std::vector<sn_section> sections;
	std::vector<sn_group> groups;
	std::vector<sn_file> files;
	std::vector<sn_symbol> locals;
	std::vector<sn_symbol> globals;
	std::vector<sn_symbol> externs;

	sn_file *find_file(const std::string &name);
	sn_file *find_file(unsigned id);

	sn_group *find_group(const std::string &name);
	sn_group *find_group(unsigned id);

//	const sn_group *find_group(const std::string &name) const;
//	const sn_group *find_group(unsigned id) const;

	sn_section *find_section(const std::string &name);
	sn_section *find_section(unsigned id);

	sn_symbol *find_extern(const std::string &name);
	sn_symbol *find_extern(unsigned id);


};


// relocation ops
enum {
	OP_EQ = 0x20,
	OP_NE = 0x22,
	OP_LE = 0x24,
	OP_LT = 0x26,
	OP_GE = 0x28,
	OP_GT = 0x2a,
	OP_ADD = 0x2c,
	OP_SUB = 0x2e,
	OP_MUL = 0x30,
	OP_DIV = 0x32,
	OP_AND = 0x34,
	OP_OR = 0x36,
	OP_XOR = 0x38,
	OP_LSHIFT = 0x3a,
	OP_RSHIFT = 0x3c,
	OP_MOD = 0x3e,

	V_OMF = 0x01, // synthetic

	V_CONST = 0x00,
	V_EXTERN = 0x02,
	V_SECTION = 0x4,
	V_FN_SECT = 0x0c,
	V_FN_GROUP = 0x0e,
	// 0x10?? 0x12??
	V_FN_GROUP_ORG = 0x14,
	V_FN_SECT_END = 0x16,
	V_FN_GROUP_END = 0x18,
};

// relocation flags
enum {	
	RELOC_PC_REL_1 = 0x32,
	RELOC_PC_REL_2 = 0x34,

	RELOC_1 = 0x02,
	RELOC_2 = 0x1a,
	RELOC_3 = 0x2c,
	RELOC_4 = 0x10,

	RELOC_1_WARN = 0x0a,
	RELOC_2_WARN = 0x1c,
	RELOC_3_WARN = 0x30,
};




void sn_parse_unit(const std::string &path, sn_unit &unit);
