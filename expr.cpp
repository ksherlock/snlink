#include <vector>
#include <cstdio>
#include <algorithm>

#include <err.h>

#include "link.h"
#include "sn.h"
#include "omf.h"



/*

 expressions are an encoded tree

 a+b*c+1 -> + 1 + * c b a

     +
  1     +
     *     a
  c     b

where the right side is evaluated first.


note: the encoded tree can also be evaluated backwards via RPN, which is what we do.

 */


inline bool is_op(unsigned x) {
	return (x & 0xff) >= 0x20;
}

inline bool is_term(unsigned x) {
	return (x & 0xff) < 0x20;
}

inline bool is_const(unsigned x) {
	return x == V_CONST;
}
inline bool is_omf(unsigned x) {
	return (x & 0xff) == V_OMF;
}


void print(const std::vector<expr_token> &v) {

	for (const auto &e : v) {
		switch (e.op & 0xff) {
			case OP_EQ: printf("== "); break;
			case OP_NE: printf("!= "); break;
			case OP_LE: printf("<= "); break;
			case OP_LT: printf("<" ); break;
			case OP_GE: printf(">= "); break;
			case OP_GT: printf(">"); break;
			case OP_ADD: printf("+ "); break;
			case OP_SUB: printf("- "); break;
			case OP_MUL: printf("* "); break;
			case OP_DIV: printf("/ "); break;
			case OP_AND: printf("& "); break;
			case OP_OR: printf("| "); break;
			case OP_XOR: printf("^ "); break;
			case OP_LSHIFT: printf("<< "); break;
			case OP_RSHIFT: printf(">> "); break;
			case OP_MOD: printf("%% "); break;
			case V_CONST: printf("%04x ", e.value); break;
			case V_EXTERN: printf("{extern %02x} ", e.value); break;
			case V_SECTION: printf("{section %02x} ", e.value); break;
			case V_OMF: printf("{omf %02x/%04x} ", e.op >> 8, e.value); break;
			default: printf(" ???");
		}
	}

	printf("\n");
}

void simplify(std::vector<expr_token> &v) {


	if (v.size() <= 1) return;

	std::vector<expr_token> out;

	for (auto iter = v.rbegin(); iter != v.rend(); ++iter) {

		const auto &e = *iter;
		if (is_term(e.op)) {
			out.push_back(e);
			continue;
		}

		// must be an operation...
		if (out.size() < 2) continue;
		auto &a = out[out.size() - 2];
		auto &b = out[out.size() - 1];

		if (is_const(a.op) && is_const(b.op)) {
			uint32_t value = 0;
			switch(e.op) {
				case OP_EQ: value = a.value == b.value; break;
				case OP_NE: value = a.value != b.value; break;
				case OP_LE: value = a.value <= b.value; break;
				case OP_LT: value = a.value < b.value; break;
				case OP_GE: value = a.value >= b.value; break;
				case OP_GT: value = a.value > b.value; break;
				case OP_ADD: value = a.value + b.value; break;
				case OP_SUB: value = a.value - b.value; break;
				case OP_MUL: value = a.value * b.value; break;
				case OP_AND: value = a.value & b.value; break;
				case OP_OR: value = a.value | b.value; break;
				case OP_XOR: value = a.value ^ b.value; break;
				case OP_LSHIFT: value = a.value << b.value; break;
				case OP_RSHIFT: value = a.value >> b.value; break;

				case OP_DIV: 
					if (b.value == 0) value = 0;
					else value = a.value / b.value;
					break;
				case OP_MOD:
					if (b.value == 0) value = 0;
					else value = a.value % b.value;
					break;
			}

			out.pop_back();
			out.pop_back();
			out.push_back(expr_token{V_CONST, value});
			continue;
		}

		if (is_omf(a.op) && is_const(b.op) && e.op == OP_ADD) {
			auto x = a;
			x.value += b.value;
			out.pop_back();
			out.pop_back();
			out.push_back(x);
			continue;
		}

		// will this ever happen?
		if (is_omf(a.op) && is_const(b.op) && e.op == OP_SUB) {
			auto x = a;
			x.value -= b.value;
			out.pop_back();
			out.pop_back();
			out.push_back(x);
			continue;
		}

		if (is_omf(b.op) && is_const(a.op) && e.op == OP_ADD) {
			auto x = b;
			x.value += a.value;
			out.pop_back();
			out.pop_back();
			out.push_back(x);
			continue;
		}

		// symbol - symbol = number...
		if (is_omf(a.op) && a.op == b.op && e.op == OP_SUB) {
			uint32_t value = a.value - b.value;
			out.pop_back();
			out.pop_back();
			out.push_back(expr_token{V_CONST, value});
			continue;
		}

		// can't simplify...
		out.push_back(e);
	}
	if (v.size() == out.size()) return;
	std::reverse(out.begin(), out.end());
	v = std::move(out);
}

void resolve(const sn_section &s, omf::segment &seg) {

	for (const auto &r : s.relocs) {


		unsigned size = 0;
		bool check = false;
		bool pcrel = false;

		switch (r.type) {
			case RELOC_PC_REL_1:
				size = 1;
				pcrel = true;
				break;

			case RELOC_PC_REL_2:
				size = 2;
				pcrel = true;
				break;

			case RELOC_1: size = 1; break;
			case RELOC_2: size = 2; break;
			case RELOC_3: size = 3; break;
			case RELOC_4: size = 4; break;

			case RELOC_1_WARN:
				check = true;
				size = 1;
				break;

			case RELOC_2_WARN:
				check = true;
				size = 2;
				break;

			case RELOC_3_WARN:	
				check = true;
				size = 2;
				break;
			default:
				errx(1, "Bad relocation type %02x", r.type);
		}

		const auto &a = r.expr.front();


		// constant expression?
		if (r.expr.size() == 1 && is_const(a.op)) {
			uint32_t value = a.value;
			uint32_t address = r.address;

			if (pcrel) {
				warnx("PC-relative constant");
			}

			if (check && (size < 4) && value > (1 << (8 * size))) {
				warnx("Value overflow");
			}

			if (seg.data.size() < address + size) {
				errx(1, "Bad relocation address");
			}
			while (size--) {
				seg.data[address++] = value & 0xff;
				value >>= 8;
			}
			continue;
		}

		omf::interseg is;
		is.size = size;
		is.offset = r.address;
		bool ok = false;

		// symbol
		// symbol >> const -- OP_RSHIFT CONST SYMBOL
		// symbol << const -- OP_LSHIFT CONST SYMBOL

		// other nonsense....

		// truncation checks
		// symbol & 0xff/ffff/fffff -- OP_AND CONST SYMBOL 

		// same-bank jsr check
		// OP_SUB OP_AND 0xff0000 SYMBOL(current segment) SYMBOL (jsr target)

		if (r.expr.size() == 1 && is_omf(a.op)) {

			// TODO -- handle pc-relative stuff here...

			is.shift = 0;
			is.segment = a.op >> 8;
			is.segment_offset = a.value;
			ok = true;

		} else if (r.expr.size() == 3) {
			const auto &b = r.expr[1];
			const auto &c = r.expr[2];

			if (a.op == OP_RSHIFT && is_const(b.op) && is_omf(c.op)) {
				is.shift = b.value;
				is.segment = c.op >> 8;
				is.segment_offset = c.value;
				ok = true;
			} else if (a.op == OP_LSHIFT && is_const(b.op) && is_omf(c.op)) {
				// does OMF allow this???
				is.shift = -b.value;
				is.segment = c.op >> 8;
				is.segment_offset = c.value;
				ok = true;
			} else if (a.op == OP_AND && is_const(b.op) && is_omf(c.op)) {
				// auto-truncation check?
				if ((b.value == 0xff) && size == 1) ok = true;
				if ((b.value == 0xffff) && size == 2) ok = true;
				if ((b.value == 0xffffff) && size == 3) ok = true;
				if (ok) {
					is.shift = 0;
					is.segment = c.op >> 8;
					is.segment_offset = c.value;
					check = false;
				}
			}
		}
		else if (r.expr.size() == 5 && size == 2) {
			const auto &b = r.expr[1];
			const auto &c = r.expr[2];
			const auto &d = r.expr[3];
			const auto &e = r.expr[2];

			if (a.op == OP_SUB && b.op == OP_AND && is_const(c.op) && c.value == 0xff0000 && is_omf(d.op) && is_omf(e.op)) {
				if (d.op != e.op) {
					warnx("Out-of-bank-reference");
				}
				is.shift = 0;
				is.segment = e.op >> 8;
				is.segment_offset = e.value;
				check = false;
				ok = true;
			}
		}

		if (!ok) {
			print(r.expr);
			errx(1, "relocation expression too complex.");
		}
		if (pcrel) {
			warnx("PC-relative reloc not supported");
		}

		// TODO -- can we do any size checks here?

		// regular reloc good enough?
		if (is.segment == s.segnum) {
			omf::reloc r;
			r.size = is.size;
			r.shift = is.shift;
			r.offset = is.offset;
			r.value = is.segment_offset;
			seg.relocs.push_back(r);
		} else {
			seg.intersegs.push_back(is);
		}
	}
}

void resolve(const std::vector<sn_unit> &units, std::vector<omf::segment> &segments) {

	for (const auto &u : units) {
		for (const auto &s : u.sections) {

			auto &seg = segments[s.segnum - 1];


			for (const auto &r : s.relocs) {


				unsigned size = 0;
				bool check = false;
				bool pcrel = false;

				switch (r.type) {
					case RELOC_PC_REL_1:
						size = 1;
						pcrel = true;
						break;

					case RELOC_PC_REL_2:
						size = 2;
						pcrel = true;
						break;

					case RELOC_1: size = 1; break;
					case RELOC_2: size = 2; break;
					case RELOC_3: size = 3; break;
					case RELOC_4: size = 4; break;

					case RELOC_1_WARN:
						check = true;
						size = 1;
						break;

					case RELOC_2_WARN:
						check = true;
						size = 2;
						break;

					case RELOC_3_WARN:	
						check = true;
						size = 2;
						break;
					default:
						errx(1, "Bad relocation type %02x", r.type);
				}

				const auto &a = r.expr.front();


				// constant expression?
				if (r.expr.size() == 1 && is_const(a.op)) {
					uint32_t value = a.value;
					uint32_t address = r.address;

					if (pcrel) {
						warnx("PC-relative constant");
					}

					if (check && (size < 4) && value > (1 << (8 * size))) {
						warnx("Value overflow");
					}

					if (seg.data.size() < address + size) {
						errx(1, "Bad relocation address");
					}
					while (size--) {
						seg.data[address++] = value & 0xff;
						value >>= 8;
					}
					continue;
				}

				omf::interseg is;
				is.size = size;
				is.offset = r.address;
				bool ok = false;

				// symbol
				// symbol >> const -- OP_RSHIFT CONST SYMBOL
				// symbol << const -- OP_LSHIFT CONST SYMBOL

				// other nonsense....

				// truncation checks
				// symbol & 0xff/ffff/fffff -- OP_AND CONST SYMBOL 

				// same-bank jsr check
				// OP_SUB OP_AND 0xff0000 SYMBOL(current segment) SYMBOL (jsr target)

				if (r.expr.size() == 1 && is_omf(a.op)) {

					// TODO -- handle pc-relative stuff here...

					is.shift = 0;
					is.segment = a.op >> 8;
					is.segment_offset = a.value;
					ok = true;

				} else if (r.expr.size() == 3) {
					const auto &b = r.expr[1];
					const auto &c = r.expr[2];

					if (a.op == OP_RSHIFT && is_const(b.op) && is_omf(c.op)) {
						is.shift = -b.value;
						is.segment = c.op >> 8;
						is.segment_offset = c.value;
						ok = true;
					} else if (a.op == OP_LSHIFT && is_const(b.op) && is_omf(c.op)) {
						// does OMF allow this???
						is.shift = b.value;
						is.segment = c.op >> 8;
						is.segment_offset = c.value;
						ok = true;
					} else if (a.op == OP_AND && is_const(b.op) && is_omf(c.op)) {
						// auto-truncation check?
						if ((b.value == 0xff) && size == 1) ok = true;
						if ((b.value == 0xffff) && size == 2) ok = true;
						if ((b.value == 0xffffff) && size == 3) ok = true;
						if (ok) {
							is.shift = 0;
							is.segment = c.op >> 8;
							is.segment_offset = c.value;
							check = false;
						}
					}
				}
				else if (r.expr.size() == 5 && size == 2) {
					const auto &b = r.expr[1];
					const auto &c = r.expr[2];
					const auto &d = r.expr[3];
					const auto &e = r.expr[4];

					if (a.op == OP_SUB && b.op == OP_AND && is_const(c.op) && c.value == 0xff0000 && is_omf(d.op) && is_omf(e.op)) {
						if (d.op != e.op) {
							warnx("Out-of-bank-reference");
						}
						is.shift = 0;
						is.segment = e.op >> 8;
						is.segment_offset = e.value;
						check = false;
						ok = true;
					}
				}

				if (!ok) {
					print(r.expr);
					errx(1, "relocation expression too complex.");
				}
				if (pcrel) {
					warnx("PC-relative reloc not supported");
				}

				// TODO -- can we do any size checks here?

				// regular reloc good enough?
				if (is.segment == s.segnum) {
					omf::reloc r;
					r.size = is.size;
					r.shift = is.shift;
					r.offset = is.offset;
					r.value = is.segment_offset;
					seg.relocs.push_back(r);
				} else {
					seg.intersegs.push_back(is);
				}
			}
		}
	}

	for (auto &seg : segments) {
		std::sort(seg.relocs.begin(), seg.relocs.end(), [](const auto &a, const auto &b){
			return a.offset < b.offset;
		});

		std::sort(seg.intersegs.begin(), seg.intersegs.end(), [](const auto &a, const auto &b){
			return a.offset < b.offset;
		});		
	}
}
