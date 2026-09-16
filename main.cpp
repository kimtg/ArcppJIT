#include "arc.h"
#include "jit.h"

constexpr auto VERSION = "0.36.3";

void print_logo() {
	printf("Arc++ JIT %s\n", VERSION);
}

int main(int argc, char **argv)
{
	try {
		bool no_jit = false;
		bool no_vm = false;
		bool interactive = false;
		bool print_result = false;
		const char *eval_code = nullptr;
		std::vector<const char*> files;

		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "-h") == 0) {
				puts("Usage: arc++ [OPTIONS...] [FILES...]");
				puts("");
				puts("OPTIONS:");
				puts("    -h             print this screen.");
				puts("    -v             print version.");
				puts("    -e EXPR        evaluate expression.");
				puts("    -p EXPR        evaluate expression and print result.");
				puts("    -i             enter interactive REPL after executing.");
				puts("    --no-jit       disable JIT (use Direct-Threaded Bytecode VM).");
				puts("    --no-vm        disable VM and JIT (use pure AST interpreter).");
				puts("    --interp       alias for --no-vm.");
				return 0;
			}
			else if (strcmp(argv[i], "-v") == 0) {
				puts(VERSION);
				return 0;
			}
			else if (strcmp(argv[i], "--no-jit") == 0) {
				no_jit = true;
			}
			else if (strcmp(argv[i], "--no-vm") == 0 || strcmp(argv[i], "--interp") == 0) {
				no_vm = true;
			}
			else if (strcmp(argv[i], "-i") == 0) {
				interactive = true;
			}
			else if (strcmp(argv[i], "-e") == 0) {
				if (i + 1 < argc) {
					eval_code = argv[++i];
					print_result = false;
				}
			}
			else if (strcmp(argv[i], "-p") == 0) {
				if (i + 1 < argc) {
					eval_code = argv[++i];
					print_result = true;
				}
			}
			else {
				files.push_back(argv[i]);
			}
		}

		if (no_vm) {
			arc::vm_enabled = false;
			arc::jit_enabled = false;
		} else if (no_jit) {
			arc::jit_enabled = false;
		}

		arc::arc_init();

		if (eval_code) {
			arc::atom res;
			arc::error err = arc::eval_string(eval_code, print_result ? &res : nullptr);
			if (err) {
				arc::print_error(err);
				return 1;
			}
			if (print_result) {
				arc::print_expr(res);
				puts("");
			}
			if (!interactive) return 0;
		}

		/* execute files */
		for (const char *f : files) {
			arc::error err = arc::arc_load_file(f);
			if (err) {
				fprintf(stderr, "In file %s:\n", f);
				arc::print_error(err);
				return 1;
			}
		}

		if (files.empty() && !eval_code) {
			interactive = true;
		}

		if (interactive) {
			print_logo();
			arc::repl();
			puts("");
		}
		return 0;
	} catch (const std::exception& ex) {
		fprintf(stderr, "FATAL EXCEPTION: %s\n", ex.what());
		return 2;
	}
}
