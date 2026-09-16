#include "jit.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace arc {

	bool jit_enabled = true;
	bool vm_enabled = false;

	compiled_fn::compiled_fn() : native_code(nullptr), native_size(0), native_entry(nullptr) {}

	compiled_fn::~compiled_fn() {
		if (native_code) {
#ifdef _WIN32
			VirtualFree(native_code, 0, MEM_RELEASE);
#else
			munmap(native_code, native_size);
#endif
			native_code = nullptr;
		}
	}

	static void* alloc_executable_memory(size_t size) {
#ifdef _WIN32
		return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
		void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		return (ptr == MAP_FAILED) ? nullptr : ptr;
#endif
	}

	static void flush_instruction_cache(void* ptr, size_t size) {
#ifdef _WIN32
		FlushInstructionCache(GetCurrentProcess(), ptr, size);
#else
		__builtin___clear_cache((char*)ptr, (char*)ptr + size);
#endif
	}

	// ---------------------------------------------------------------------------
	// Runtime C++ Helper Functions for JIT
	// ---------------------------------------------------------------------------

	extern "C" {
		error jit_helper_add(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				res->type = T_NUM;
				res->val = std::get<double>(a->val) + std::get<double>(b->val);
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			return builtin_add(vargs, res);
		}

		error jit_helper_sub(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				res->type = T_NUM;
				res->val = std::get<double>(a->val) - std::get<double>(b->val);
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			return builtin_subtract(vargs, res);
		}

		error jit_helper_mul(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				res->type = T_NUM;
				res->val = std::get<double>(a->val) * std::get<double>(b->val);
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			return builtin_multiply(vargs, res);
		}

		error jit_helper_div(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				res->type = T_NUM;
				res->val = std::get<double>(a->val) / std::get<double>(b->val);
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			return builtin_divide(vargs, res);
		}

		error jit_helper_lt(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				*res = (std::get<double>(a->val) < std::get<double>(b->val)) ? sym_t : nil;
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			return builtin_less(vargs, res);
		}

		error jit_helper_gt(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				*res = (std::get<double>(a->val) > std::get<double>(b->val)) ? sym_t : nil;
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			return builtin_greater(vargs, res);
		}

		error jit_helper_le(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				*res = (std::get<double>(a->val) <= std::get<double>(b->val)) ? sym_t : nil;
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			atom gt_res;
			error err = builtin_greater(vargs, &gt_res);
			if (err) return err;
			*res = no(gt_res) ? sym_t : nil;
			return ERROR_OK;
		}

		error jit_helper_ge(atom* res, const atom* a, const atom* b) {
			if (a->type == T_NUM && b->type == T_NUM) {
				*res = (std::get<double>(a->val) >= std::get<double>(b->val)) ? sym_t : nil;
				return ERROR_OK;
			}
			std::vector<atom> vargs = { *a, *b };
			atom lt_res;
			error err = builtin_less(vargs, &lt_res);
			if (err) return err;
			*res = no(lt_res) ? sym_t : nil;
			return ERROR_OK;
		}

		error jit_helper_is(atom* res, const atom* a, const atom* b) {
			*res = is(*a, *b) ? sym_t : nil;
			return ERROR_OK;
		}

		error jit_helper_car(atom* res, const atom* a) {
			if (no(*a)) { *res = nil; return ERROR_OK; }
			if (a->type != T_CONS) return ERROR_TYPE;
			*res = car(*a);
			return ERROR_OK;
		}

		error jit_helper_cdr(atom* res, const atom* a) {
			if (no(*a)) { *res = nil; return ERROR_OK; }
			if (a->type != T_CONS) return ERROR_TYPE;
			*res = cdr(*a);
			return ERROR_OK;
		}

		error jit_helper_cons(atom* res, const atom* a, const atom* b) {
			*res = make_cons(*a, *b);
			return ERROR_OK;
		}

		error jit_helper_get_global(atom* res, sym symbol) {
			return env_get(global_env, symbol, res);
		}

		error jit_helper_set_global(sym symbol, const atom* val) {
			return env_assign_eq(global_env, symbol, *val);
		}

		error jit_helper_get_env(atom* res, struct closure* cls, sym symbol) {
			if (cls && cls->parent_env) {
				return env_get(cls->parent_env, symbol, res);
			}
			return env_get(global_env, symbol, res);
		}

		error jit_helper_set_env(struct closure* cls, sym symbol, const atom* val) {
			if (cls && cls->parent_env) {
				return env_assign_eq(cls->parent_env, symbol, *val);
			}
			return env_assign_eq(global_env, symbol, *val);
		}

		error jit_helper_call(atom* res, const atom* fn, const atom* args, size_t argc) {
			if (fn->type == T_CLOSURE) {
				auto& callee = *std::get<std::shared_ptr<closure>>(fn->val);
				if (vm_enabled && !callee.is_macro && !callee.compiled && !callee.compile_attempted) {
					compile_closure(&callee);
				}
				if (callee.compiled && callee.compiled->native_entry) {
					return callee.compiled->native_entry(args, argc, res, &callee);
				}
				if (callee.compiled) {
					return vm_execute(callee.compiled->chunk, args, argc, res, &callee);
				}
			}
			std::vector<atom> vargs(args, args + argc);
			return apply(*fn, vargs, res);
		}

		void jit_helper_copy_atom(atom* dst, const atom* src) {
			*dst = *src;
		}

		void jit_helper_set_nil(atom* dst) {
			*dst = nil;
		}

		void jit_helper_set_t(atom* dst) {
			*dst = sym_t;
		}
	}

	// ---------------------------------------------------------------------------
	// Bytecode Compiler
	// ---------------------------------------------------------------------------

	struct Compiler {
		BytecodeChunk chunk;
		std::vector<std::unordered_map<sym, uint16_t>> scopes;
		uint16_t next_local_slot = 0;
		sym self_sym = -1;

		uint16_t add_constant(const atom& a) {
			for (size_t i = 0; i < chunk.constants.size(); i++) {
				if (is(chunk.constants[i], a)) return (uint16_t)i;
			}
			uint16_t idx = (uint16_t)chunk.constants.size();
			chunk.constants.push_back(a);
			return idx;
		}

		void emit_u8(uint8_t b) { chunk.code.push_back(b); }
		void emit_u16(uint16_t w) {
			chunk.code.push_back(w & 0xFF);
			chunk.code.push_back((w >> 8) & 0xFF);
		}
		void emit_i32(int32_t d) {
			chunk.code.push_back(d & 0xFF);
			chunk.code.push_back((d >> 8) & 0xFF);
			chunk.code.push_back((d >> 16) & 0xFF);
			chunk.code.push_back((d >> 24) & 0xFF);
		}

		size_t emit_jump(Opcode op) {
			emit_u8(op);
			size_t pos = chunk.code.size();
			emit_i32(0);
			return pos;
		}

		void patch_jump(size_t pos) {
			int32_t offset = (int32_t)(chunk.code.size() - (pos + 4));
			chunk.code[pos] = offset & 0xFF;
			chunk.code[pos + 1] = (offset >> 8) & 0xFF;
			chunk.code[pos + 2] = (offset >> 16) & 0xFF;
			chunk.code[pos + 3] = (offset >> 24) & 0xFF;
		}

		bool lookup_local(sym s, uint16_t* slot) {
			for (int i = (int)scopes.size() - 1; i >= 0; i--) {
				auto it = scopes[i].find(s);
				if (it != scopes[i].end()) {
					*slot = it->second;
					return true;
				}
			}
			return false;
		}

		error compile_expr(atom expr, bool is_tail) {
			if (no(expr)) {
				emit_u8(OP_NIL);
				return ERROR_OK;
			}
			if (expr.type == T_SYM) {
				sym s = std::get<sym>(expr.val);
				if (sym_is(expr, sym_t)) {
					emit_u8(OP_TRUE);
					return ERROR_OK;
				}
				uint16_t slot = 0;
				if (lookup_local(s, &slot)) {
					emit_u8(OP_GET_LOCAL);
					emit_u16(slot);
					return ERROR_OK;
				}
				// Global or outer environment
				emit_u8(OP_GET_ENV);
				emit_i32(s);
				return ERROR_OK;
			}
			if (expr.type != T_CONS) {
				uint16_t idx = add_constant(expr);
				emit_u8(OP_CONST);
				emit_u16(idx);
				return ERROR_OK;
			}

			// Cons expression
			atom op = car(expr);
			atom args = cdr(expr);

			// Quote
			if (op.type == T_SYM && sym_is(op, sym_quote)) {
				if (no(args)) return ERROR_ARGS;
				uint16_t idx = add_constant(car(args));
				emit_u8(OP_CONST);
				emit_u16(idx);
				return ERROR_OK;
			}

			// If
			if (op.type == T_SYM && sym_is(op, sym_if)) {
				std::vector<size_t> exit_jumps;
				atom p = args;
				bool has_trailing_else = false;
				while (!no(p)) {
					if (no(cdr(p))) { // Else part (odd trailing argument)
						error err = compile_expr(car(p), is_tail);
						if (err) return err;
						p = cdr(p);
						has_trailing_else = true;
						break;
					}
					// Condition
					error err = compile_expr(car(p), false);
					if (err) return err;
					size_t jump_false = emit_jump(OP_JUMP_IF_FALSE);

					// Then
					p = cdr(p);
					err = compile_expr(car(p), is_tail);
					if (err) return err;
					exit_jumps.push_back(emit_jump(OP_JUMP));

					patch_jump(jump_false);
					p = cdr(p);
				}
				if (!has_trailing_else) {
					emit_u8(OP_NIL);
				}
				for (size_t jump : exit_jumps) {
					patch_jump(jump);
				}
				return ERROR_OK;
			}

			// Assign
			if (op.type == T_SYM && sym_is(op, sym_assign)) {
				if (no(args) || no(cdr(args))) return ERROR_ARGS;
				atom var = car(args);
				atom val = car(cdr(args));
				if (var.type != T_SYM) return ERROR_TYPE;

				error err = compile_expr(val, false);
				if (err) return err;

				sym s = std::get<sym>(var.val);
				uint16_t slot = 0;
				if (lookup_local(s, &slot)) {
					emit_u8(OP_SET_LOCAL);
					emit_u16(slot);
				} else {
					emit_u8(OP_SET_ENV);
					emit_i32(s);
				}
				return ERROR_OK;
			}

			// Do
			if (op.type == T_SYM && sym_is(op, sym_do)) {
				if (no(args)) {
					emit_u8(OP_NIL);
					return ERROR_OK;
				}
				atom p = args;
				while (!no(p)) {
					bool last = no(cdr(p));
					error err = compile_expr(car(p), last ? is_tail : false);
					if (err) return err;
					if (!last) emit_u8(OP_POP);
					p = cdr(p);
				}
				return ERROR_OK;
			}

			// Nested closure creation (fallback to interpreter)
			if (op.type == T_SYM && sym_is(op, sym_fn)) {
				return ERROR_TYPE;
			}

			// Check for inlined let: ((fn (var ...) body ...) val ...)
			if (op.type == T_CONS && car(op).type == T_SYM && sym_is(car(op), sym_fn)) {
				atom fn_args = car(cdr(op));
				atom fn_body = cdr(cdr(op));

				// Evaluate arguments and push on stack
				std::vector<atom> let_vals = atom_to_vector(args);
				std::vector<atom> let_vars = atom_to_vector(fn_args);

				if (let_vals.size() == let_vars.size()) {
					bool simple_vars = true;
					for (auto& v : let_vars) {
						if (v.type != T_SYM) { simple_vars = false; break; }
					}
					if (simple_vars) {
						// Evaluate all values
						for (auto& val : let_vals) {
							error err = compile_expr(val, false);
							if (err) return err;
						}
						// Bind to new local slots in reverse order of evaluation
						scopes.push_back({});
						for (int i = (int)let_vars.size() - 1; i >= 0; i--) {
							uint16_t slot = next_local_slot++;
							scopes.back()[std::get<sym>(let_vars[i].val)] = slot;
							emit_u8(OP_SET_LOCAL);
							emit_u16(slot);
							emit_u8(OP_POP); // pop assigned value
						}
						// Compile body
						atom b = fn_body;
						while (!no(b)) {
							bool last = no(cdr(b));
							error err = compile_expr(car(b), last ? is_tail : false);
							if (err) return err;
							if (!last) emit_u8(OP_POP);
							b = cdr(b);
						}
						scopes.pop_back();
						return ERROR_OK;
					}
				}
			}

			// Arithmetic & Relational Builtin Inlines (binary)
			if (op.type == T_SYM) {
				sym s = std::get<sym>(op.val);
				std::vector<atom> vargs = atom_to_vector(args);

				if (vargs.size() == 2) {
					Opcode bin_op = (Opcode)0xFF;
					std::string name = str_of_sym[s];
					if (name == "+") bin_op = OP_ADD;
					else if (name == "-") bin_op = OP_SUB;
					else if (name == "*") bin_op = OP_MUL;
					else if (name == "/") bin_op = OP_DIV;
					else if (name == "<") bin_op = OP_LT;
					else if (name == ">") bin_op = OP_GT;
					else if (name == "<=") bin_op = OP_LE;
					else if (name == ">=") bin_op = OP_GE;
					else if (name == "is") bin_op = OP_IS;
					else if (name == "cons") bin_op = OP_CONS;

					if (bin_op != (Opcode)0xFF) {
						error err1 = compile_expr(vargs[0], false);
						if (err1) return err1;
						error err2 = compile_expr(vargs[1], false);
						if (err2) return err2;
						emit_u8(bin_op);
						return ERROR_OK;
					}
				}
				else if (vargs.size() == 1) {
					std::string name = str_of_sym[s];
					if (name == "-") {
						atom zero; zero.type = T_NUM; zero.val = 0.0;
						uint16_t z_idx = add_constant(zero);
						emit_u8(OP_CONST); emit_u16(z_idx);
						error err = compile_expr(vargs[0], false);
						if (err) return err;
						emit_u8(OP_SUB);
						return ERROR_OK;
					}
					else if (name == "no") {
						error err = compile_expr(vargs[0], false);
						if (err) return err;
						emit_u8(OP_NOT);
						return ERROR_OK;
					}
					else if (name == "car") {
						error err = compile_expr(vargs[0], false);
						if (err) return err;
						emit_u8(OP_CAR);
						return ERROR_OK;
					}
					else if (name == "cdr") {
						error err = compile_expr(vargs[0], false);
						if (err) return err;
						emit_u8(OP_CDR);
						return ERROR_OK;
					}
				}

				// Self recursion
				if (self_sym >= 0 && s == self_sym) {
					for (auto& a : vargs) {
						error err = compile_expr(a, false);
						if (err) return err;
					}
					if (is_tail) {
						emit_u8(OP_TAIL_CALL_SELF);
						emit_u16((uint16_t)vargs.size());
					} else {
						emit_u8(OP_CALL_SELF);
						emit_u16((uint16_t)vargs.size());
					}
					return ERROR_OK;
				}
			}

			// General function call
			std::vector<atom> vargs = atom_to_vector(args);
			error err = compile_expr(op, false);
			if (err) return err;
			for (auto& a : vargs) {
				err = compile_expr(a, false);
				if (err) return err;
			}
			if (is_tail) {
				emit_u8(OP_TAIL_CALL);
				emit_u16((uint16_t)vargs.size());
			} else {
				emit_u8(OP_CALL);
				emit_u16((uint16_t)vargs.size());
			}
			return ERROR_OK;
		}
	};

	error compile_closure(struct closure* cls, sym self_sym) {
		if (cls->compiled) return ERROR_OK;
		cls->compile_attempted = true;

		Compiler comp;
		comp.self_sym = self_sym;
		comp.scopes.push_back({});

		// Parse parameters
		atom p = cls->args;
		uint16_t param_idx = 0;
		while (!no(p)) {
			if (p.type == T_SYM) { // rest param (fn args ...)
				comp.scopes[0][std::get<sym>(p.val)] = param_idx++;
				comp.chunk.has_rest_param = true;
				break;
			}
			if (p.type != T_CONS) return ERROR_TYPE;
			atom arg = car(p);
			if (arg.type == T_SYM) {
				comp.scopes[0][std::get<sym>(arg.val)] = param_idx++;
			} else {
				// destructuring or optional argument: fall back to interpreted
				return ERROR_ARGS;
			}
			p = cdr(p);
			if (p.type == T_SYM) { // improper list (fn (a . b) ...)
				comp.scopes[0][std::get<sym>(p.val)] = param_idx++;
				comp.chunk.has_rest_param = true;
				break;
			}
		}

		comp.chunk.num_params = param_idx;
		comp.next_local_slot = param_idx;

		// Compile body
		atom body = cls->body;
		if (no(body)) {
			comp.emit_u8(OP_NIL);
			comp.emit_u8(OP_RETURN);
		} else {
			while (!no(body)) {
				bool last = no(cdr(body));
				error err = comp.compile_expr(car(body), last);
				if (err) return err;
				if (!last) comp.emit_u8(OP_POP);
				body = cdr(body);
			}
			comp.emit_u8(OP_RETURN);
		}

		comp.chunk.num_locals = std::max((size_t)comp.next_local_slot, (size_t)param_idx);
		comp.chunk.max_stack = 64 + comp.chunk.num_locals;
		comp.chunk.self_sym = self_sym;

		auto compiled = std::make_shared<compiled_fn>();
		compiled->chunk = std::move(comp.chunk);

		// Attempt native JIT compilation on x86-64 if JIT is enabled
#if defined(__x86_64__) || defined(_M_X64)
		if (jit_enabled) {
			jit_compile(compiled.get());
		}
#endif

		cls->compiled = compiled;
		return ERROR_OK;
	}

	// ---------------------------------------------------------------------------
	// Direct-Threaded Bytecode Virtual Machine (Tier 1)
	// ---------------------------------------------------------------------------

	static const void* s_label_table[33] = { nullptr };

	void thread_chunk(BytecodeChunk& chunk, const void* const* label_table) {
		if (!chunk.threaded_code.empty() || chunk.code.empty()) return;

		const uint8_t* start = chunk.code.data();
		const uint8_t* ip = start;
		const uint8_t* end = ip + chunk.code.size();

		std::vector<int> bc_to_insn(chunk.code.size() + 1, -1);
		std::vector<size_t> insn_bc_offsets;

		while (ip < end) {
			size_t offset = ip - start;
			bc_to_insn[offset] = (int)insn_bc_offsets.size();
			insn_bc_offsets.push_back(offset);

			Opcode op = (Opcode)*ip++;
			switch (op) {
			case OP_CONST:
			case OP_GET_LOCAL:
			case OP_SET_LOCAL:
			case OP_CALL:
			case OP_TAIL_CALL:
			case OP_CALL_SELF:
			case OP_TAIL_CALL_SELF:
				ip += 2;
				break;
			case OP_GET_GLOBAL:
			case OP_SET_GLOBAL:
			case OP_GET_ENV:
			case OP_SET_ENV:
			case OP_JUMP:
			case OP_JUMP_IF_FALSE:
			case OP_JUMP_IF_TRUE:
				ip += 4;
				break;
			default:
				break;
			}
		}
		bc_to_insn[chunk.code.size()] = (int)insn_bc_offsets.size();

		size_t num_insns = insn_bc_offsets.size();
		chunk.threaded_code.resize(num_insns + 1);

		for (size_t i = 0; i < num_insns; i++) {
			size_t offset = insn_bc_offsets[i];
			const uint8_t* cur_ip = start + offset;
			Opcode op = (Opcode)*cur_ip++;
			ThreadedInsn& insn = chunk.threaded_code[i];
			insn.opcode = op;
			insn.handler = label_table ? label_table[op] : nullptr;

			switch (op) {
			case OP_CONST: {
				uint16_t idx = *(const uint16_t*)cur_ip;
				insn.constant = &chunk.constants[idx];
				insn.u16 = idx;
				break;
			}
			case OP_GET_LOCAL:
			case OP_SET_LOCAL:
			case OP_CALL:
			case OP_TAIL_CALL:
			case OP_CALL_SELF:
			case OP_TAIL_CALL_SELF: {
				insn.u16 = *(const uint16_t*)cur_ip;
				break;
			}
			case OP_GET_GLOBAL:
			case OP_SET_GLOBAL:
			case OP_GET_ENV:
			case OP_SET_ENV: {
				insn.i32 = *(const int32_t*)cur_ip;
				break;
			}
			case OP_JUMP:
			case OP_JUMP_IF_FALSE:
			case OP_JUMP_IF_TRUE: {
				int32_t jmp_offset = *(const int32_t*)cur_ip;
				size_t target_bc = (offset + 1 + 4) + jmp_offset;
				int target_insn = (target_bc <= chunk.code.size()) ? bc_to_insn[target_bc] : -1;
				if (target_insn >= 0 && (size_t)target_insn < chunk.threaded_code.size()) {
					insn.target = &chunk.threaded_code[target_insn];
				} else {
					insn.target = &chunk.threaded_code[num_insns];
				}
				break;
			}
			default:
				break;
			}
		}

		chunk.threaded_code[num_insns].opcode = OP_RETURN;
		chunk.threaded_code[num_insns].handler = label_table ? label_table[OP_RETURN] : nullptr;
	}

	error vm_execute(const BytecodeChunk& chunk, const atom* args, size_t argc, atom* result, struct closure* cls) {
#if defined(__GNUC__) || defined(__clang__)
		if (!s_label_table[0]) {
			s_label_table[OP_CONST]          = &&do_OP_CONST;
			s_label_table[OP_NIL]            = &&do_OP_NIL;
			s_label_table[OP_TRUE]           = &&do_OP_TRUE;
			s_label_table[OP_GET_LOCAL]      = &&do_OP_GET_LOCAL;
			s_label_table[OP_SET_LOCAL]      = &&do_OP_SET_LOCAL;
			s_label_table[OP_POP]            = &&do_OP_POP;
			s_label_table[OP_DUP]            = &&do_OP_DUP;
			s_label_table[OP_GET_GLOBAL]     = &&do_OP_GET_GLOBAL;
			s_label_table[OP_SET_GLOBAL]     = &&do_OP_SET_GLOBAL;
			s_label_table[OP_GET_ENV]        = &&do_OP_GET_ENV;
			s_label_table[OP_SET_ENV]        = &&do_OP_SET_ENV;
			s_label_table[OP_ADD]            = &&do_OP_ADD;
			s_label_table[OP_SUB]            = &&do_OP_SUB;
			s_label_table[OP_MUL]            = &&do_OP_MUL;
			s_label_table[OP_DIV]            = &&do_OP_DIV;
			s_label_table[OP_MOD]            = &&do_OP_MOD;
			s_label_table[OP_LT]             = &&do_OP_LT;
			s_label_table[OP_GT]             = &&do_OP_GT;
			s_label_table[OP_LE]             = &&do_OP_LE;
			s_label_table[OP_GE]             = &&do_OP_GE;
			s_label_table[OP_IS]             = &&do_OP_IS;
			s_label_table[OP_NOT]            = &&do_OP_NOT;
			s_label_table[OP_CAR]            = &&do_OP_CAR;
			s_label_table[OP_CDR]            = &&do_OP_CDR;
			s_label_table[OP_CONS]           = &&do_OP_CONS;
			s_label_table[OP_JUMP]           = &&do_OP_JUMP;
			s_label_table[OP_JUMP_IF_FALSE]  = &&do_OP_JUMP_IF_FALSE;
			s_label_table[OP_JUMP_IF_TRUE]   = &&do_OP_JUMP_IF_TRUE;
			s_label_table[OP_CALL]           = &&do_OP_CALL;
			s_label_table[OP_TAIL_CALL]      = &&do_OP_TAIL_CALL;
			s_label_table[OP_CALL_SELF]      = &&do_OP_CALL_SELF;
			s_label_table[OP_TAIL_CALL_SELF] = &&do_OP_TAIL_CALL_SELF;
			s_label_table[OP_RETURN]         = &&do_OP_RETURN;
		}
#endif

		if (chunk.code.empty() && chunk.threaded_code.empty()) {
			*result = nil;
			return ERROR_OK;
		}

		if (chunk.threaded_code.empty()) {
			thread_chunk(const_cast<BytecodeChunk&>(chunk), s_label_table);
		}

		atom locals[32];
		size_t num_params = std::min(chunk.num_params, (size_t)32);
		size_t num_locals = std::min(chunk.num_locals, (size_t)32);

		size_t fixed_params = chunk.has_rest_param ? (num_params > 0 ? num_params - 1 : 0) : num_params;
		for (size_t i = 0; i < fixed_params; i++) {
			if (i < argc) locals[i] = args[i];
			else locals[i] = nil;
		}
		if (chunk.has_rest_param && num_params > 0) {
			atom rest_list = nil;
			if (argc > fixed_params) {
				std::vector<atom> v(args + fixed_params, args + argc);
				rest_list = vector_to_atom(v, 0);
			}
			locals[fixed_params] = rest_list;
		}
		for (size_t i = num_params; i < num_locals; i++) {
			locals[i] = nil;
		}

		atom stack[64];
		atom* sp = stack;

		const ThreadedInsn* pc = chunk.threaded_code.data();

#if defined(__GNUC__) || defined(__clang__)
	#define VM_DISPATCH() goto *pc->handler
#else
	#define VM_DISPATCH() goto msvc_dispatch
#endif

		VM_DISPATCH();

#if !defined(__GNUC__) && !defined(__clang__)
msvc_dispatch:
		switch (pc->opcode) {
		case OP_CONST: goto do_OP_CONST;
		case OP_NIL: goto do_OP_NIL;
		case OP_TRUE: goto do_OP_TRUE;
		case OP_GET_LOCAL: goto do_OP_GET_LOCAL;
		case OP_SET_LOCAL: goto do_OP_SET_LOCAL;
		case OP_POP: goto do_OP_POP;
		case OP_DUP: goto do_OP_DUP;
		case OP_GET_GLOBAL: goto do_OP_GET_GLOBAL;
		case OP_SET_GLOBAL: goto do_OP_SET_GLOBAL;
		case OP_GET_ENV: goto do_OP_GET_ENV;
		case OP_SET_ENV: goto do_OP_SET_ENV;
		case OP_ADD: goto do_OP_ADD;
		case OP_SUB: goto do_OP_SUB;
		case OP_MUL: goto do_OP_MUL;
		case OP_DIV: goto do_OP_DIV;
		case OP_MOD: goto do_OP_MOD;
		case OP_LT: goto do_OP_LT;
		case OP_GT: goto do_OP_GT;
		case OP_LE: goto do_OP_LE;
		case OP_GE: goto do_OP_GE;
		case OP_IS: goto do_OP_IS;
		case OP_NOT: goto do_OP_NOT;
		case OP_CAR: goto do_OP_CAR;
		case OP_CDR: goto do_OP_CDR;
		case OP_CONS: goto do_OP_CONS;
		case OP_JUMP: goto do_OP_JUMP;
		case OP_JUMP_IF_FALSE: goto do_OP_JUMP_IF_FALSE;
		case OP_JUMP_IF_TRUE: goto do_OP_JUMP_IF_TRUE;
		case OP_CALL: goto do_OP_CALL;
		case OP_TAIL_CALL: goto do_OP_TAIL_CALL;
		case OP_CALL_SELF: goto do_OP_CALL_SELF;
		case OP_TAIL_CALL_SELF: goto do_OP_TAIL_CALL_SELF;
		case OP_RETURN: goto do_OP_RETURN;
		default: return ERROR_SYNTAX;
		}
#endif

do_OP_CONST:
		*sp++ = *pc->constant;
		pc++;
		VM_DISPATCH();

do_OP_NIL:
		*sp++ = nil;
		pc++;
		VM_DISPATCH();

do_OP_TRUE:
		*sp++ = sym_t;
		pc++;
		VM_DISPATCH();

do_OP_GET_LOCAL:
		*sp++ = locals[pc->u16];
		pc++;
		VM_DISPATCH();

do_OP_SET_LOCAL:
		locals[pc->u16] = *(sp - 1);
		pc++;
		VM_DISPATCH();

do_OP_POP:
		--sp;
		pc++;
		VM_DISPATCH();

do_OP_DUP:
		*sp = *(sp - 1);
		sp++;
		pc++;
		VM_DISPATCH();

do_OP_GET_GLOBAL: {
		error err = env_get(global_env, pc->i32, sp);
		if (err) return err;
		sp++;
}
		pc++;
		VM_DISPATCH();

do_OP_SET_GLOBAL: {
		error err = env_assign_eq(global_env, pc->i32, *(sp - 1));
		if (err) return err;
}
		pc++;
		VM_DISPATCH();

do_OP_GET_ENV: {
		error err = jit_helper_get_env(sp, cls, pc->i32);
		if (err) return err;
		sp++;
}
		pc++;
		VM_DISPATCH();

do_OP_SET_ENV: {
		error err = jit_helper_set_env(cls, pc->i32, sp - 1);
		if (err) return err;
}
		pc++;
		VM_DISPATCH();

do_OP_ADD: {
		atom* pa = sp - 2;
		atom* pb = sp - 1;
		if (pa->type == T_NUM && pb->type == T_NUM) {
			pa->val = std::get<double>(pa->val) + std::get<double>(pb->val);
			--sp;
		} else {
			error err = jit_helper_add(pa, pa, pb);
			if (err) return err;
			--sp;
		}
}
		pc++;
		VM_DISPATCH();

do_OP_SUB: {
		atom* pa = sp - 2;
		atom* pb = sp - 1;
		if (pa->type == T_NUM && pb->type == T_NUM) {
			pa->val = std::get<double>(pa->val) - std::get<double>(pb->val);
			--sp;
		} else {
			error err = jit_helper_sub(pa, pa, pb);
			if (err) return err;
			--sp;
		}
}
		pc++;
		VM_DISPATCH();

do_OP_MUL: {
		atom* pa = sp - 2;
		atom* pb = sp - 1;
		if (pa->type == T_NUM && pb->type == T_NUM) {
			pa->val = std::get<double>(pa->val) * std::get<double>(pb->val);
			--sp;
		} else {
			error err = jit_helper_mul(pa, pa, pb);
			if (err) return err;
			--sp;
		}
}
		pc++;
		VM_DISPATCH();

do_OP_DIV: {
		atom* pa = sp - 2;
		atom* pb = sp - 1;
		if (pa->type == T_NUM && pb->type == T_NUM) {
			pa->val = std::get<double>(pa->val) / std::get<double>(pb->val);
			--sp;
		} else {
			error err = jit_helper_div(pa, pa, pb);
			if (err) return err;
			--sp;
		}
}
		pc++;
		VM_DISPATCH();

do_OP_MOD: {
		atom* pa = sp - 2;
		atom* pb = sp - 1;
		if (pa->type == T_NUM && pb->type == T_NUM) {
			double va = std::get<double>(pa->val);
			double vb = std::get<double>(pb->val);
			pa->val = fmod(va, vb);
			--sp;
		} else {
			std::vector<atom> vargs = { *pa, *pb };
			error err = builtin_mod(vargs, pa);
			if (err) return err;
			--sp;
		}
}
		pc++;
		VM_DISPATCH();

do_OP_LT: {
		atom* pa = sp - 2;
		atom* pb = sp - 1;
		if (pa->type == T_NUM && pb->type == T_NUM) {
			bool res = std::get<double>(pa->val) < std::get<double>(pb->val);
			--sp;
			*(sp - 1) = res ? sym_t : nil;
		} else {
			error err = jit_helper_lt(pa, pa, pb);
			if (err) return err;
			--sp;
		}
}
		pc++;
		VM_DISPATCH();

do_OP_GT: {
		atom* pa = sp - 2;
		atom* pb = sp - 1;
		if (pa->type == T_NUM && pb->type == T_NUM) {
			bool res = std::get<double>(pa->val) > std::get<double>(pb->val);
			--sp;
			*(sp - 1) = res ? sym_t : nil;
		} else {
			error err = jit_helper_gt(pa, pa, pb);
			if (err) return err;
			--sp;
		}
}
		pc++;
		VM_DISPATCH();

do_OP_LE: {
		error err = jit_helper_le(sp - 2, sp - 2, sp - 1);
		if (err) return err;
		--sp;
}
		pc++;
		VM_DISPATCH();

do_OP_GE: {
		error err = jit_helper_ge(sp - 2, sp - 2, sp - 1);
		if (err) return err;
		--sp;
}
		pc++;
		VM_DISPATCH();

do_OP_IS: {
		bool res = is(*(sp - 2), *(sp - 1));
		--sp;
		*(sp - 1) = res ? sym_t : nil;
}
		pc++;
		VM_DISPATCH();

do_OP_NOT:
		*(sp - 1) = no(*(sp - 1)) ? sym_t : nil;
		pc++;
		VM_DISPATCH();

do_OP_CAR: {
		error err = jit_helper_car(sp - 1, sp - 1);
		if (err) return err;
}
		pc++;
		VM_DISPATCH();

do_OP_CDR: {
		error err = jit_helper_cdr(sp - 1, sp - 1);
		if (err) return err;
}
		pc++;
		VM_DISPATCH();

do_OP_CONS: {
		atom r = make_cons(*(sp - 2), *(sp - 1));
		--sp;
		*(sp - 1) = r;
}
		pc++;
		VM_DISPATCH();

do_OP_JUMP:
		pc = pc->target;
		VM_DISPATCH();

do_OP_JUMP_IF_FALSE:
		--sp;
		if (no(*sp)) {
			pc = pc->target;
		} else {
			pc++;
		}
		VM_DISPATCH();

do_OP_JUMP_IF_TRUE:
		--sp;
		if (!no(*sp)) {
			pc = pc->target;
		} else {
			pc++;
		}
		VM_DISPATCH();

do_OP_CALL_SELF: {
		uint16_t call_argc = pc->u16;
		atom* call_args = sp - call_argc;
		error err = ERROR_OK;
		if (cls && cls->compiled && cls->compiled->native_entry) {
			err = cls->compiled->native_entry(call_args, call_argc, call_args, cls);
		} else {
			err = vm_execute(chunk, call_args, call_argc, call_args, cls);
		}
		if (err) return err;
		sp = call_args + 1;
		pc++;
}
		VM_DISPATCH();

do_OP_TAIL_CALL_SELF: {
		uint16_t call_argc = pc->u16;
		atom* call_args = sp - call_argc;
		for (size_t i = 0; i < call_argc; i++) {
			locals[i] = call_args[i];
		}
		sp = stack;
		pc = chunk.threaded_code.data();
}
		VM_DISPATCH();

do_OP_CALL: {
		uint16_t call_argc = pc->u16;
		atom* call_args = sp - call_argc;
		atom callee = *(call_args - 1);
		atom call_res;
		error err = jit_helper_call(&call_res, &callee, call_args, call_argc);
		if (err) return err;
		*(call_args - 1) = call_res;
		sp = call_args;
		pc++;
}
		VM_DISPATCH();

do_OP_TAIL_CALL: {
		uint16_t call_argc = pc->u16;
		atom* call_args = sp - call_argc;
		atom callee = *(call_args - 1);
		atom call_res;
		error err = jit_helper_call(&call_res, &callee, call_args, call_argc);
		if (err) return err;
		*result = call_res;
		return ERROR_OK;
}

do_OP_RETURN:
		*result = *--sp;
		return ERROR_OK;
#undef VM_DISPATCH
	}

	// ---------------------------------------------------------------------------
	// Native x86-64 Machine Code Assembler & JIT Compiler (Tier 2)
	// ---------------------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)

	class X86Emitter {
	public:
		std::vector<uint8_t> code;

		void emit_u8(uint8_t b) { code.push_back(b); }
		void emit_u16(uint16_t w) {
			code.push_back(w & 0xFF);
			code.push_back((w >> 8) & 0xFF);
		}
		void emit_u32(uint32_t d) {
			code.push_back(d & 0xFF);
			code.push_back((d >> 8) & 0xFF);
			code.push_back((d >> 16) & 0xFF);
			code.push_back((d >> 24) & 0xFF);
		}
		void emit_u64(uint64_t q) {
			emit_u32((uint32_t)(q & 0xFFFFFFFF));
			emit_u32((uint32_t)(q >> 32));
		}

		void push_reg(int reg) {
			if (reg >= 8) emit_u8(0x41);
			emit_u8(0x50 + (reg & 7));
		}

		void pop_reg(int reg) {
			if (reg >= 8) emit_u8(0x41);
			emit_u8(0x58 + (reg & 7));
		}

		// mov reg, imm64
		void mov_reg_imm64(int reg, uint64_t imm) {
			uint8_t rex = 0x48 | ((reg >= 8) ? 1 : 0);
			emit_u8(rex);
			emit_u8(0xB8 + (reg & 7));
			emit_u64(imm);
		}

		// mov reg, reg (64-bit)
		void mov_reg_reg(int dst, int src) {
			uint8_t rex = 0x48 | ((src >= 8) ? 4 : 0) | ((dst >= 8) ? 1 : 0);
			emit_u8(rex);
			emit_u8(0x89);
			emit_u8(0xC0 | ((src & 7) << 3) | (dst & 7));
		}

		// lea reg, [base + disp32] (64-bit)
		void lea_reg_mem(int dst, int base, int32_t disp) {
			uint8_t rex = 0x48 | ((dst >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0);
			emit_u8(rex);
			emit_u8(0x8D);
			emit_u8(0x80 | ((dst & 7) << 3) | (base & 7));
			emit_u32((uint32_t)disp);
		}

		// sub rsp, imm32
		void sub_rsp(int32_t imm) {
			emit_u8(0x48); emit_u8(0x81); emit_u8(0xEC);
			emit_u32((uint32_t)imm);
		}

		// add rsp, imm32
		void add_rsp(int32_t imm) {
			emit_u8(0x48); emit_u8(0x81); emit_u8(0xC4);
			emit_u32((uint32_t)imm);
		}

		// call reg
		void call_reg(int reg) {
			if (reg >= 8) emit_u8(0x41);
			emit_u8(0xFF);
			emit_u8(0xD0 + (reg & 7));
		}

		// ret
		void ret() { emit_u8(0xC3); }

		// jmp rel32
		size_t jmp_rel32() {
			emit_u8(0xE9);
			size_t pos = code.size();
			emit_u32(0);
			return pos;
		}

		// je rel32
		size_t je_rel32() {
			emit_u8(0x0F); emit_u8(0x84);
			size_t pos = code.size();
			emit_u32(0);
			return pos;
		}

		// jne rel32
		size_t jne_rel32() {
			emit_u8(0x0F); emit_u8(0x85);
			size_t pos = code.size();
			emit_u32(0);
			return pos;
		}

		// jb rel32 (unsigned below / float less than)
		size_t jb_rel32() {
			emit_u8(0x0F); emit_u8(0x82);
			size_t pos = code.size();
			emit_u32(0);
			return pos;
		}

		// ja rel32 (unsigned above / float greater than)
		size_t ja_rel32() {
			emit_u8(0x0F); emit_u8(0x87);
			size_t pos = code.size();
			emit_u32(0);
			return pos;
		}

		// sub reg, imm32
		void sub_reg_imm32(int reg, int32_t imm) {
			uint8_t rex = 0x48 | ((reg >= 8) ? 1 : 0);
			emit_u8(rex); emit_u8(0x81);
			emit_u8(0xE8 + (reg & 7));
			emit_u32((uint32_t)imm);
		}

		// add reg, imm32
		void add_reg_imm32(int reg, int32_t imm) {
			uint8_t rex = 0x48 | ((reg >= 8) ? 1 : 0);
			emit_u8(rex); emit_u8(0x81);
			emit_u8(0xC0 + (reg & 7));
			emit_u32((uint32_t)imm);
		}

		// mov rax, [base + disp32]
		void mov_rax_mem(int base, int32_t disp) {
			uint8_t rex = 0x48 | ((base >= 8) ? 1 : 0);
			emit_u8(rex);
			emit_u8(0x8B);
			emit_u8(0x80 | (base & 7));
			emit_u32((uint32_t)disp);
		}

		// mov [base + disp32], rax
		void mov_mem_rax(int base, int32_t disp) {
			uint8_t rex = 0x48 | ((base >= 8) ? 1 : 0);
			emit_u8(rex);
			emit_u8(0x89);
			emit_u8(0x80 | (base & 7));
			emit_u32((uint32_t)disp);
		}

		// Copy 32 bytes between memory locations using rax
		void copy_atom(int dst_base, int32_t dst_disp, int src_base, int32_t src_disp) {
			for (int i = 0; i < 4; i++) {
				mov_rax_mem(src_base, src_disp + i * 8);
				mov_mem_rax(dst_base, dst_disp + i * 8);
			}
		}

		// Clear 32 bytes at [base + disp] to nil (0)
		void clear_atom(int base, int32_t disp) {
			emit_u8(0x31); emit_u8(0xC0); // xor eax, eax
			for (int i = 0; i < 4; i++) {
				mov_mem_rax(base, disp + i * 8);
			}
		}

		// jbe rel32 (unsigned below or equal)
		size_t jbe_rel32() {
			emit_u8(0x0F); emit_u8(0x86);
			size_t pos = code.size();
			emit_u32(0);
			return pos;
		}

		// jae rel32 (unsigned above or equal)
		size_t jae_rel32() {
			emit_u8(0x0F); emit_u8(0x83);
			size_t pos = code.size();
			emit_u32(0);
			return pos;
		}

		// patch jump
		void patch_rel32(size_t pos) {
			int32_t offset = (int32_t)(code.size() - (pos + 4));
			code[pos] = offset & 0xFF;
			code[pos + 1] = (offset >> 8) & 0xFF;
			code[pos + 2] = (offset >> 16) & 0xFF;
			code[pos + 3] = (offset >> 24) & 0xFF;
		}
	};

	static bool can_emit_native(const BytecodeChunk& chunk) {
		if (chunk.has_rest_param) return false;
		const uint8_t* ip = chunk.code.data();
		const uint8_t* end = ip + chunk.code.size();
		while (ip < end) {
			Opcode op = (Opcode)*ip++;
			switch (op) {
			case OP_CONST:
			case OP_GET_LOCAL:
			case OP_SET_LOCAL:
			case OP_CALL_SELF:
			case OP_TAIL_CALL_SELF:
				ip += 2;
				break;
			case OP_JUMP:
			case OP_JUMP_IF_FALSE:
			case OP_JUMP_IF_TRUE:
				ip += 4;
				break;
			case OP_NIL:
			case OP_TRUE:
			case OP_POP:
			case OP_DUP:
			case OP_ADD:
			case OP_SUB:
			case OP_MUL:
			case OP_DIV:
			case OP_LT:
			case OP_GT:
			case OP_LE:
			case OP_GE:
			case OP_IS:
			case OP_NOT:
			case OP_RETURN:
				break;
			default:
				return false;
			}
		}
		return true;
	}

	bool jit_compile(compiled_fn* comp) {
		if (!comp) return false;
		auto& chunk = comp->chunk;

		if (!can_emit_native(chunk)) {
			return false;
		}

		size_t alloc_size = 8192 + chunk.code.size() * 64;
		void* mem = alloc_executable_memory(alloc_size);
		if (!mem) return false;

		X86Emitter e;

		// Direct native machine code generation
		e.push_reg(5); // push rbp (RSP % 16 == 0)
			e.mov_reg_reg(5, 4); // mov rbp, rsp
			e.push_reg(3); // push rbx
			e.push_reg(6); // push rsi
			e.push_reg(7); // push rdi
			e.push_reg(12); // push r12
			e.push_reg(13); // push r13
			e.push_reg(14); // push r14
			e.push_reg(15); // push r15
			// 56 bytes pushed -> RSP % 16 == 8

			// Frame size: 3112 bytes -> RSP % 16 == 0 before any call
			e.sub_rsp(3112);

			// RBX = locals base (RBP - 1080)
			e.lea_reg_mem(3, 5, -1080);
			// R13 = constants base
			e.mov_reg_imm64(13, (uint64_t)chunk.constants.data());
			// R14 = result pointer (from r8)
			e.mov_reg_reg(14, 8);
			// R15 = cls pointer (from r9)
			e.mov_reg_reg(15, 9);
			// RSI = operand stack top (initially RBP - 3128)
			e.lea_reg_mem(6, 5, -3128);

			// Argument initialization
			for (size_t i = 0; i < chunk.num_params; i++) {
				// cmp rdx, i
				e.emit_u8(0x48); e.emit_u8(0x83); e.emit_u8(0xFA); e.emit_u8((uint8_t)i);
				size_t j_nil = e.jbe_rel32();
				e.copy_atom(3, (int32_t)(i * 32), 1, (int32_t)(i * 32));
				size_t j_next = e.jmp_rel32();
				e.patch_rel32(j_nil);
				e.clear_atom(3, (int32_t)(i * 32));
				e.patch_rel32(j_next);
			}
			for (size_t i = chunk.num_params; i < chunk.num_locals; i++) {
				e.clear_atom(3, (int32_t)(i * 32));
			}

			size_t loop_start = e.code.size();

			std::unordered_map<size_t, size_t> bc_to_native;
			struct Patch { size_t patch_pos; size_t target_bc; };
			std::vector<Patch> patches;
			std::vector<size_t> error_jumps;

			const uint8_t* ip = chunk.code.data();
			const uint8_t* start_ip = ip;
			const uint8_t* end_ip = ip + chunk.code.size();

			while (ip < end_ip) {
				size_t cur_bc = ip - start_ip;
				bc_to_native[cur_bc] = e.code.size();
				Opcode op = (Opcode)*ip++;

				switch (op) {
				case OP_CONST: {
					uint16_t idx = *(uint16_t*)ip; ip += 2;
					e.copy_atom(6, 0, 13, (int32_t)(idx * 32));
					e.add_reg_imm32(6, 32);
					break;
				}
				case OP_NIL: {
					e.clear_atom(6, 0);
					e.add_reg_imm32(6, 32);
					break;
				}
				case OP_TRUE: {
					e.mov_reg_imm64(1, (uint64_t)&sym_t);
					e.copy_atom(6, 0, 1, 0);
					e.add_reg_imm32(6, 32);
					break;
				}
				case OP_GET_LOCAL: {
					uint16_t slot = *(uint16_t*)ip; ip += 2;
					e.copy_atom(6, 0, 3, (int32_t)(slot * 32));
					e.add_reg_imm32(6, 32);
					break;
				}
				case OP_SET_LOCAL: {
					uint16_t slot = *(uint16_t*)ip; ip += 2;
					e.copy_atom(3, (int32_t)(slot * 32), 6, -32);
					break;
				}
				case OP_POP: {
					e.sub_reg_imm32(6, 32);
					break;
				}
				case OP_DUP: {
					e.copy_atom(6, 0, 6, -32);
					e.add_reg_imm32(6, 32);
					break;
				}
				case OP_ADD: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x58); e.emit_u8(0x46); e.emit_u8(0xE8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x11); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.sub_reg_imm32(6, 32);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_add);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);
					e.patch_rel32(j_done);
					break;
				}
				case OP_SUB: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x5C); e.emit_u8(0x46); e.emit_u8(0xE8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x11); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.sub_reg_imm32(6, 32);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_sub);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);
					e.patch_rel32(j_done);
					break;
				}
				case OP_MUL: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x59); e.emit_u8(0x46); e.emit_u8(0xE8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x11); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.sub_reg_imm32(6, 32);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_mul);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);
					e.patch_rel32(j_done);
					break;
				}
				case OP_DIV: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x5E); e.emit_u8(0x46); e.emit_u8(0xE8);
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x11); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.sub_reg_imm32(6, 32);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_div);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);
					e.patch_rel32(j_done);
					break;
				}
				case OP_LT: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0x66); e.emit_u8(0x0F); e.emit_u8(0x2E); e.emit_u8(0x46); e.emit_u8(0xE8);
					size_t j_less = e.jb_rel32();
					e.clear_atom(6, -64);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_less);
					e.mov_reg_imm64(1, (uint64_t)&sym_t);
					e.copy_atom(6, -64, 1, 0);

					e.patch_rel32(j_done);
					e.sub_reg_imm32(6, 32);
					size_t j_exit = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_lt);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);

					e.patch_rel32(j_exit);
					break;
				}
				case OP_GT: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0x66); e.emit_u8(0x0F); e.emit_u8(0x2E); e.emit_u8(0x46); e.emit_u8(0xE8);
					size_t j_greater = e.ja_rel32();
					e.clear_atom(6, -64);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_greater);
					e.mov_reg_imm64(1, (uint64_t)&sym_t);
					e.copy_atom(6, -64, 1, 0);

					e.patch_rel32(j_done);
					e.sub_reg_imm32(6, 32);
					size_t j_exit = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_gt);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);

					e.patch_rel32(j_exit);
					break;
				}
				case OP_LE: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0x66); e.emit_u8(0x0F); e.emit_u8(0x2E); e.emit_u8(0x46); e.emit_u8(0xE8);
					size_t j_le = e.jbe_rel32();
					e.clear_atom(6, -64);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_le);
					e.mov_reg_imm64(1, (uint64_t)&sym_t);
					e.copy_atom(6, -64, 1, 0);

					e.patch_rel32(j_done);
					e.sub_reg_imm32(6, 32);
					size_t j_exit = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_le);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);

					e.patch_rel32(j_exit);
					break;
				}
				case OP_GE: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xC0); e.emit_u8(0x03);
					size_t j_fb1 = e.jne_rel32();
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x03);
					size_t j_fb2 = e.jne_rel32();
					e.emit_u8(0xF2); e.emit_u8(0x0F); e.emit_u8(0x10); e.emit_u8(0x46); e.emit_u8(0xC8);
					e.emit_u8(0x66); e.emit_u8(0x0F); e.emit_u8(0x2E); e.emit_u8(0x46); e.emit_u8(0xE8);
					size_t j_ge = e.jae_rel32();
					e.clear_atom(6, -64);
					size_t j_done = e.jmp_rel32();

					e.patch_rel32(j_ge);
					e.mov_reg_imm64(1, (uint64_t)&sym_t);
					e.copy_atom(6, -64, 1, 0);

					e.patch_rel32(j_done);
					e.sub_reg_imm32(6, 32);
					size_t j_exit = e.jmp_rel32();

					e.patch_rel32(j_fb1);
					e.patch_rel32(j_fb2);
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_ge);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);

					e.patch_rel32(j_exit);
					break;
				}
				case OP_IS: {
					e.lea_reg_mem(1, 6, -64);
					e.lea_reg_mem(2, 6, -64);
					e.lea_reg_mem(8, 6, -32);
					e.mov_reg_imm64(0, (uint64_t)jit_helper_is);
					e.call_reg(0);
					e.sub_reg_imm32(6, 32);
					break;
				}
				case OP_NOT: {
					e.emit_u8(0x83); e.emit_u8(0x7E); e.emit_u8(0xE0); e.emit_u8(0x00);
					size_t j_is_nil = e.je_rel32();
					e.clear_atom(6, -32);
					size_t j_done = e.jmp_rel32();
					e.patch_rel32(j_is_nil);
					e.mov_reg_imm64(1, (uint64_t)&sym_t);
					e.copy_atom(6, -32, 1, 0);
					e.patch_rel32(j_done);
					break;
				}
				case OP_JUMP: {
					int32_t off = *(int32_t*)ip; ip += 4;
					size_t target_bc = (ip - start_ip) + off;
					size_t j = e.jmp_rel32();
					patches.push_back({ j, target_bc });
					break;
				}
				case OP_JUMP_IF_FALSE: {
					int32_t off = *(int32_t*)ip; ip += 4;
					size_t target_bc = (ip - start_ip) + off;
					e.sub_reg_imm32(6, 32);
					e.emit_u8(0x83); e.emit_u8(0x3E); e.emit_u8(0x00);
					size_t j = e.je_rel32();
					patches.push_back({ j, target_bc });
					break;
				}
				case OP_JUMP_IF_TRUE: {
					int32_t off = *(int32_t*)ip; ip += 4;
					size_t target_bc = (ip - start_ip) + off;
					e.sub_reg_imm32(6, 32);
					e.emit_u8(0x83); e.emit_u8(0x3E); e.emit_u8(0x00);
					size_t j = e.jne_rel32();
					patches.push_back({ j, target_bc });
					break;
				}
				case OP_CALL_SELF: {
					uint16_t call_argc = *(uint16_t*)ip; ip += 2;
					e.lea_reg_mem(1, 6, -(int32_t)(call_argc * 32));
					e.emit_u8(0xBA); e.emit_u32((uint32_t)call_argc);
					e.mov_reg_reg(8, 1);
					e.mov_reg_reg(9, 15);
					e.emit_u8(0xE8);
					int32_t rel_off = (int32_t)(0 - (e.code.size() + 4));
					e.emit_u32((uint32_t)rel_off);
					e.emit_u8(0x85); e.emit_u8(0xC0);
					size_t j_err = e.jne_rel32();
					error_jumps.push_back(j_err);
					if (call_argc > 1) {
						e.sub_reg_imm32(6, (call_argc - 1) * 32);
					}
					break;
				}
				case OP_TAIL_CALL_SELF: {
					uint16_t call_argc = *(uint16_t*)ip; ip += 2;
					for (size_t i = 0; i < call_argc; i++) {
						e.copy_atom(3, (int32_t)(i * 32), 6, -(int32_t)((call_argc - i) * 32));
					}
					e.lea_reg_mem(6, 5, -3128);
					e.emit_u8(0xE9);
					int32_t rel_off = (int32_t)(loop_start - (e.code.size() + 4));
					e.emit_u32((uint32_t)rel_off);
					break;
				}
				case OP_RETURN: {
					e.copy_atom(14, 0, 6, -32);
					e.emit_u8(0x31); e.emit_u8(0xC0);
					e.lea_reg_mem(4, 5, -56);
					e.pop_reg(15);
					e.pop_reg(14);
					e.pop_reg(13);
					e.pop_reg(12);
					e.pop_reg(7);
					e.pop_reg(6);
					e.pop_reg(3);
					e.pop_reg(5);
					e.ret();
					break;
				}
				default:
					break;
				}
			}

			// Error exit
			if (error_jumps.size() > 0) {
				size_t err_pos = e.code.size();
				for (size_t j : error_jumps) {
					e.patch_rel32(j);
				}
				(void)err_pos;
				e.lea_reg_mem(4, 5, -56);
				e.pop_reg(15);
				e.pop_reg(14);
				e.pop_reg(13);
				e.pop_reg(12);
				e.pop_reg(7);
				e.pop_reg(6);
				e.pop_reg(3);
				e.pop_reg(5);
				e.ret();
			}

			// Patch all jumps
			for (const auto& p : patches) {
				auto it = bc_to_native.find(p.target_bc);
				if (it != bc_to_native.end()) {
					int32_t rel = (int32_t)(it->second - (p.patch_pos + 4));
					e.code[p.patch_pos] = rel & 0xFF;
					e.code[p.patch_pos + 1] = (rel >> 8) & 0xFF;
					e.code[p.patch_pos + 2] = (rel >> 16) & 0xFF;
					e.code[p.patch_pos + 3] = (rel >> 24) & 0xFF;
				}
			}

		memcpy(mem, e.code.data(), e.code.size());
		flush_instruction_cache(mem, e.code.size());

		comp->native_code = mem;
		comp->native_size = alloc_size;
		comp->native_entry = (JitNativeFn)mem;

		return true;
	}

#else

	bool jit_compile(compiled_fn* comp) {
		return false;
	}

#endif

	void jit_init() {
		// JIT symbols and status
	}

} // namespace arc
