
/*
 * evaluate an expression.
 * result should be a number or an omf relocation (shift w/ offset)
 */

/*

 expressions are an encoded tree

 a+b*c+1 -> + 1 + * c b a

    +
  1   +
    *   a
  c   b


convert to rpn?


post-order lrn
traverse left subtree
traverse right subtree
current node

ops: * + + 
terms: c b a 1

 */

bool is_op(const expr_token &t) {
	return t.op >= 0x20;
}
bool is_term(const expr_token &t) {
	return t.op < 0x20;
}

unsigned to_rpn(const std::vector<expr_token> &expr, std::vector<expr_token> &terms, std::vector<uint8_t> &ops, unsigned ix = 0)
{
	auto op = expr[ix++];

	if (is_term(op)) {
		// should only happen if expression is 1-term.
		terms.push_back(op);
		return ix;
	}

	auto l = expr[ix++];
	if (is_op(l)) {
		ix = to_rpn(expr, terms, ops, ix);
	}
	auto r = expr[ix++];
	if (is_op(r)) {
		ix = to_rpn(expr, terms, ops, ix);
	}
	ops.push_back(op.value);
	if (is_term(l)) terms.push_back(l);
	if (is_term(r)) terms.push_back(r);
	return ix;
}

// actually... we can just process it backwards?

void simplify() {
	stack;
	for (const auto &e :reverse(expr)) {
		if (is_term(e) || stack.size() < 2 ) { stack.push(e); continue; }

		auto a = stack[stack.size() - 2];
		auto b = stack[stack.size() - 1];
		if (is_op(a) || is_op(b)) { stack.push(e); continue; }

		if (is_const(a) && is_const(b)) {
			uint32_t value = 0;
			switch(op) {
				case OP_EQ: value = a.value == b.value; break;
				case OP_NE: value = a.value <> b.value; break;
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
			stack.pop_back();
			stack.pop_back();
			stack.emplace_back({V_CONST, value});
			continue;
		}

		// constant + symbol
		if (is_const(a) && is_sym(b) && op == OP_ADD) {
			b.offset += a.value;
			stack.pop_back();
			stack.pop_back();
			stack.emplace_back(b);
			continue;
		}

		if (is_const(b) && is_sym(a) && op == OP_ADD) {
			a.offset += b.value;
			stack.pop_back();
			stack.pop_back();
			stack.emplace_back(a);
			continue;
		}
	}
	std::reverse(stack);
}

// special cases:
// assembler generates xref & 0xff, xref & 0xffff to indicate zero-page/absolute w/o error checking
// assembler generates a mess for jsr xref to check if it's in the current bank.

// lda <var -> lda (var + 0) & 0xff
// lda |var -> lda (var + 0) & 0xffff
// lda >var -> lda (var + 0) & 0xffffff
// jsr xref -> jsr xref - ((section + 0) & 0x00ff0000)

// - & 0x00ff0000 + section 0 xref
//
//         -
//      &      xref
//0x0ff   +
//       section 0
//

bool is_trunc() {
	// can just drop the & bit since OMF will handle it.
	if (expr.size() >= 3 && expr[0].op == OP_AND && expr[1].op == V_CONST) {
		if (size == 1 && expr[1].value == 0xff) return true;
		if (size == 2 && expr[1].value == 0xffff) return true;
		if (size == 3 && expr[1].value == 0xffffff) return true;
	}
}