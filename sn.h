
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

	V_CONST = 0x00,
	V_EXTERN = 0x02,
	V_SECTION = 0x4,
	V_OMF = 0x10, // synthetic
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
