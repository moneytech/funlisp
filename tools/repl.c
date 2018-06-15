/*
 * repl.c: A simple read-eval-print loop for funlisp
 *
 * Stephen Brennan <stephen@brennan.io>
 */

#include <editline/readline.h>
#include <stdio.h>
#include <stdlib.h>

#include "funlisp.h"

int main(int argc, char **argv)
{
	lisp_runtime *rt = lisp_runtime_new();
	lisp_scope *scope = lisp_new_default_scope(rt);

	(void)argc; /* unused parameters */
	(void)argv;

	for (;;) {
		char *input;
		lisp_value *value, *result;
		int bytes;

		input = readline("> ");
		if (input == NULL)
			break; /* Ctrl-D, EOF */
		bytes = lisp_parse_value(rt, input, 0, &value);
		add_history(input);
		free(input);
		if (bytes < 0) {
			/* parse error */
			lisp_print_error(rt, stderr);
			lisp_clear_error(rt);
			continue;
		} else if (!value) {
			/* empty line */
			continue;
		}
		result = lisp_eval(rt, scope, value);
		if (!result) {
			lisp_print_error(rt, stderr);
			lisp_clear_error(rt);
		} else if (!lisp_nil_p(result)) {
			lisp_print(stdout, result);
			fprintf(stdout, "\n");
		}
		lisp_mark(rt, (lisp_value*)scope);
		lisp_sweep(rt);
	}

	lisp_runtime_free(rt);
	return 0;
}
