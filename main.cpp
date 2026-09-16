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
		const char *eval_code = nullptr;
		std::vector<const char*> files;

		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "-h") == 0) {
				puts("Usage: arc++ [OPTIONS...] [FILES...]");
				puts("");
				puts("OPTIONS:");
				puts("    -h        print this screen.");
				puts("    -v        print version.");
				puts("    -e EXPR   evaluate expression.");
				puts("    --no-jit  disable JIT compilation.");
				return 0;
			}
			else if (strcmp(argv[i], "-v") == 0) {
				puts(VERSION);
				return 0;
			}
			else if (strcmp(argv[i], "--no-jit") == 0) {
				no_jit = true;
			}
			else if (strcmp(argv[i], "-e") == 0) {
				if (i + 1 < argc) {
					eval_code = argv[++i];
				}
			}
			else {
				files.push_back(argv[i]);
			}
		}

		if (no_jit) {
			arc::jit_enabled = false;
		}

		arc::arc_init();

		if (eval_code) {
			arc::error err = arc::load_string(eval_code);
			if (err) {
				arc::print_error(err);
				return 1;
			}
			return 0;
		}

		if (files.empty()) { /* REPL */
			print_logo();
			arc::repl();
			puts("");
			return 0;
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
		return 0;
	} catch (const std::exception& ex) {
		fprintf(stderr, "FATAL EXCEPTION: %s\n", ex.what());
		return 2;
	}
}
