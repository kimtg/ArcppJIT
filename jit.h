#pragma once
#ifndef _INC_ARC_JIT
#define _INC_ARC_JIT

#include "arc.h"
#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace arc {

	enum Opcode : uint8_t {
		OP_CONST,         // [const_idx: u16] -> push constants[idx]
		OP_NIL,           // -> push nil
		OP_TRUE,          // -> push sym_t
		OP_GET_LOCAL,     // [slot: u16] -> push locals[slot]
		OP_SET_LOCAL,     // [slot: u16] -> locals[slot] = top()
		OP_POP,           // -> pop 1
		OP_DUP,           // -> push top()
		OP_GET_GLOBAL,    // [sym_id: i32] -> push global symbol
		OP_SET_GLOBAL,    // [sym_id: i32] -> assign global symbol
		OP_GET_ENV,       // [sym_id: i32] -> lookup symbol in closure env
		OP_SET_ENV,       // [sym_id: i32] -> assign_eq symbol in closure env
		OP_ADD,           // pop 2, push a + b
		OP_SUB,           // pop 2, push a - b (or pop 1, push -a)
		OP_MUL,           // pop 2, push a * b
		OP_DIV,           // pop 2, push a / b
		OP_MOD,           // pop 2, push a % b
		OP_LT,            // pop 2, push (a < b ? t : nil)
		OP_GT,            // pop 2, push (a > b ? t : nil)
		OP_LE,            // pop 2, push (a <= b ? t : nil)
		OP_GE,            // pop 2, push (a >= b ? t : nil)
		OP_IS,            // pop 2, push (is(a, b) ? t : nil)
		OP_NOT,           // pop 1, push (no(a) ? t : nil)
		OP_CAR,           // pop 1, push car(a)
		OP_CDR,           // pop 1, push cdr(a)
		OP_CONS,          // pop 2, push cons(a, b)
		OP_JUMP,          // [offset: i32] -> jump relative to end of operand
		OP_JUMP_IF_FALSE, // [offset: i32] -> pop cond, jump if no(cond)
		OP_JUMP_IF_TRUE,  // [offset: i32] -> pop cond, jump if !no(cond)
		OP_CALL,          // [argc: u16] -> call function on stack with argc arguments
		OP_TAIL_CALL,     // [argc: u16] -> tail call function on stack
		OP_CALL_SELF,     // [argc: u16] -> recursive call to self
		OP_TAIL_CALL_SELF,// [argc: u16] -> tail recursive call to self (loop)
		OP_RETURN         // pop, return result
	};

	struct ThreadedInsn;

	struct ThreadedInsn {
		const void* handler = nullptr;
		Opcode opcode = OP_NIL;
		const atom* constant = nullptr;
		const ThreadedInsn* target = nullptr;
		int32_t i32 = 0;
		uint16_t u16 = 0;
	};

	struct BytecodeChunk {
		std::vector<uint8_t> code;
		std::vector<atom> constants;
		std::vector<ThreadedInsn> threaded_code;
		size_t num_params = 0;
		size_t num_locals = 0;
		size_t max_stack = 64;
		sym self_sym = -1;
		bool has_rest_param = false;
	};

	void thread_chunk(BytecodeChunk& chunk, const void* const* label_table = nullptr);

	// JIT native function signature:
	// Returns error code (ERROR_OK = 0), writes result into *result
	typedef error(*JitNativeFn)(const atom* args, size_t argc, atom* result, struct closure* cls);

	struct compiled_fn {
		BytecodeChunk chunk;
		void* native_code = nullptr;
		size_t native_size = 0;
		JitNativeFn native_entry = nullptr;

		compiled_fn();
		~compiled_fn();
	};

	extern bool jit_enabled;
	extern bool vm_enabled;

	void jit_init();
	error compile_closure(struct closure* cls, sym self_sym = -1);
	error vm_execute(const BytecodeChunk& chunk, const atom* args, size_t argc, atom* result, struct closure* cls);
	bool jit_compile(compiled_fn* comp);

} // namespace arc

#endif // _INC_ARC_JIT

