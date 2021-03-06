/*
 * parse.c: recursive descent parser for funlisp
 *
 * Stephen Brennan <stephen@brennan.io>
 */

#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "funlisp_internal.h"
#include "charbuf.h"

typedef struct {
	lisp_value *result;
	int index;
	enum lisp_errno error;
} result;

#define return_result_err(v, i, e)                        \
	do {                                              \
		result uncommon_name;                     \
		uncommon_name.result = (lisp_value*) (v); \
		uncommon_name.index = (i);                \
		uncommon_name.error = (e);                \
		return uncommon_name;                     \
	} while(0)

#define return_result(v, i) return_result_err(v, i, 0)

#define COMMENT ';'


static result lisp_parse_value_internal(lisp_runtime *rt, char *input, int index);

static result lisp_parse_integer(lisp_runtime *rt, char *input, int index)
{
	int n, rv;
	lisp_integer *v = (lisp_integer*)lisp_new(rt, type_integer);
	rv = sscanf(input + index, "%d%n", &v->x, &n);
	if (rv != 1) {
		rt->error = "syntax error: error parsing integer";
		return_result_err(NULL, index, LE_SYNTAX);
	} else {
		return_result(v, index + n);
	}
}

static int skip_space_and_comments(char *input, int index)
{
	for (;;) {
		while (isspace(input[index])) {
			index++;
		}
		if (input[index] && input[index] == COMMENT) {
			while (input[index] && input[index] != '\n') {
				index++;
			}
		} else {
			return index;
		}
	}
}

static char lisp_escape(char escape)
{
	switch (escape) {
	case 'a':
		return '\a';
	case 'b':
		return '\b';
	case 'f':
		return '\f';
	case 'n':
		return '\n';
	case 'r':
		return '\b';
	case 't':
		return '\t';
	case 'v':
		return '\v';
	default:
		return escape;
	}
}

static result lisp_parse_string(lisp_runtime *rt, char *input, int index)
{
	int i;
	struct charbuf cb;
	lisp_string *str;

	i = index + 1;
	cb_init(&cb, 16);
	while (input[i] && input[i] != '"') {
		if (input[i] == '\\') {
			cb_append(&cb, lisp_escape(input[++i]));
		} else {
			cb_append(&cb, input[i]);
		}
		i++;
	}
	if (!input[i]) {
		cb_destroy(&cb);
		rt->error = "unexpected eof while parsing string";
		return_result_err(NULL, i, LE_SYNTAX);
	}
	cb_trim(&cb);
	str = (lisp_string*)lisp_new(rt, type_string);
	str->s = cb.buf;
	str->can_free = 1;
	i++;
	return_result(str, i);
}

static result lisp_parse_list_or_sexp(lisp_runtime *rt, char *input, int index)
{
	result r;
	lisp_list *rv, *l;

	index = skip_space_and_comments(input, index);
	if (!input[index]) {
		rt->error = "unexpected eof while parsing list";
		return_result_err(NULL, index, LE_EOF);
	} else if (input[index] == ')') {
		return_result(lisp_nil_new(rt), index + 1);
	}

	r = lisp_parse_value_internal(rt, input, index);
	if (r.error) return r;
	else if (!r.result) return_result_err(NULL, r.index, 1);
	index = r.index;
	rv = (lisp_list*)lisp_new(rt, type_list);
	rv->left = r.result;
	l = rv;

	while (true) {
		index = skip_space_and_comments(input, index);

		if (!input[index]) {
			rt->error = "unexpected eof while parsing list";
			return_result_err(NULL, index, LE_EOF);
		} else if (input[index] == '.') {
			index++;
			r = lisp_parse_value_internal(rt, input, index);
			if (r.error) return r;
			else if (!r.result) return_result_err(NULL, r.index, 1);
			index = r.index;
			l->right = r.result;
			/* this MUST be the end of the list / or sexp. make sure
			 * it is (returning error if not), and consume the
			 * closing paren */
			index = skip_space_and_comments(input, index);
			if (input[index] != ')') {
				rt->error = "bad s-expression form";
				return_result_err(NULL, index, LE_SYNTAX);
			}
			index++;
			return_result(rv, index);
		} else if (input[index] == ')') {
			index++;
			l->right = lisp_nil_new(rt);
			return_result(rv, index);
		} else {
			r = lisp_parse_value_internal(rt, input, index);
			if (r.error) return r;
			else if (!r.result) return_result_err(NULL, r.index, 1);
			l->right = lisp_new(rt, type_list);
			l = (lisp_list*)l->right;
			l->left = r.result;
			index = r.index;
		}
	}
}

static lisp_value *split_symbol(lisp_runtime *rt, char *string, int dotcount, int remain)
{
	char *delim, *tok;
	int i, len;
	lisp_value *prev = NULL;
	lisp_symbol *sym;
	lisp_list *list;
	lisp_symbol *getattr = lisp_symbol_new(rt, "getattr", 0);

	/* Create the first symbol, which is the left hand side */
	delim = strchr(string, '.');
	len = (int) (delim - string);
	tok = malloc(len + 1);
	strncpy(tok, string, len);
	tok[len] = '\0';
	sym = lisp_symbol_new(rt, tok, LS_OWN);
	prev = (lisp_value*) sym;
	string = delim + 1;
	remain -= len + 1;

	/* Create a "getattr" for each right hand side remaining */
	for (i = 0; i < dotcount; i++) {
		/* Attribute symbol */
		if (i < dotcount - 1) {
			delim = strchr(string, '.');
			len = (int) (delim - string);
		} else {
			len = remain;
		}
		tok = malloc(len + 1);
		strncpy(tok, string, len);
		tok[len] = '\0';
		sym = lisp_symbol_new(rt, tok, LS_OWN);

		/* Create (getattr PREV 'tok) */
		list = lisp_list_new(rt,
			(lisp_value *) getattr,
			(lisp_value *) lisp_list_new(rt,
				prev,
				(lisp_value *) lisp_list_new(rt,
					(lisp_value *) lisp_quote_with(rt, (lisp_value *) sym, "quote"),
					lisp_nil_new(rt)
				)
			)
		);
		prev = (lisp_value *) list;
		string = delim + 1;
		remain -= len + 1;
	}
	return prev;
}

static result lisp_parse_symbol(lisp_runtime *rt, char *input, int index)
{
	int n = 0;
	int dotcount = 0;
	char *copy;
	lisp_symbol *s;

	while (input[index + n] && !isspace(input[index + n]) &&
	       input[index + n] != ')' && /* input[index + n] != '.' && */
	       input[index + n] != '\'' && input[index + n] != COMMENT) {
		if (input[index + n] == '.')
			dotcount++;
		n++;
	}
	if (!input[index]) {
		rt->error = "unexpected eof while parsing symbol";
		return_result_err(NULL, index, LE_EOF);
	}

	if (dotcount) {
		if (input[index] == '.' || input[index + n - 1] == '.') {
			rt->error = "unexpected '.' at beginning or end of symbol";
			return_result_err(NULL, index, LE_SYNTAX);
		}
		return_result(split_symbol(rt, input + index, dotcount, n), index + n);
	}

	copy = malloc(n + 1);
	strncpy(copy, input + index, n);
	copy[n] = '\0';
	/* use lisp_symbol_new(), ensuring that we use the symbol cache if it
	 * exists */
	s = lisp_symbol_new(rt, copy, LS_OWN);
	return_result(s, index + n);
}

static result lisp_parse_quote(lisp_runtime *rt, char *input, int index)
{
	char *qc;
	result r = lisp_parse_value_internal(rt, input, index + 1);
	if (r.error) return r;
	else if (!r.result) return_result_err(NULL, r.index, 1);
	switch (input[index]) {
	case '\'':
		qc = "quote";
		break;
	case '`':
		qc = "quasiquote";
		break;
	case ',':
		qc = "unquote";
		break;
	default:
		assert(0);
	}
	r.result = (lisp_value*) lisp_quote_with(rt, r.result, qc);
	return r;
}

static result lisp_parse_value_internal(lisp_runtime *rt, char *input, int index)
{
	index = skip_space_and_comments(input, index);

	switch (input[index]) {
	case '"':
		return lisp_parse_string(rt, input, index);
	case '\0':
		return_result(NULL, index);
	case ')':
		return_result(lisp_nil_new(rt), index + 1);
	case '(':
		return lisp_parse_list_or_sexp(rt, input, index + 1);
	case '`':
	case ',':
	case '\'':
		return lisp_parse_quote(rt, input, index);
	default:
		if (isdigit(input[index])) {
			return lisp_parse_integer(rt, input, index);
		} else {
			return lisp_parse_symbol(rt, input, index);
		}
	}
}

static void set_error_lineno(lisp_runtime *rt, char *input, int index)
{
	int i;
	rt->error_line = 1;
	for (i = 0; i < index; i++)
		if (input[i] == '\n')
			rt->error_line++;
}

int lisp_parse_value(lisp_runtime *rt, char *input, int index, lisp_value **output)
{
	int bytes;
	result r = lisp_parse_value_internal(rt, input, index);
	bytes = r.index - index;
	if (r.error) {
		rt->err_num = r.error;
		set_error_lineno(rt, input, r.index);
		bytes = -1;
	}
	*output = r.result;
	return bytes;
}

lisp_value *lisp_parse_progn(lisp_runtime *rt, char *input)
{
	lisp_list *final_result, *prev;
	lisp_value *expression;
	int bytes, index=0;

	final_result = (lisp_list*) lisp_new(rt, type_list);
	final_result->left = (lisp_value*)lisp_symbol_new(rt, "progn", 0);
	prev = final_result;
	for (;;) {
		bytes = lisp_parse_value(rt, input, index, &expression);
		index += bytes;
		if (bytes < 0) {
			return NULL; /* error! */
		} else if (!expression) {
			prev->right = lisp_nil_new(rt);
			return (lisp_value*) final_result;
		} else {
			prev->right = (lisp_value*) lisp_list_new(
					rt, expression, NULL);
			prev = (lisp_list*) prev->right;
		}
	}
}

static char *read_file(FILE *input)
{
	size_t bufsize = 1024;
	size_t length = 0;
	char *buf = malloc(bufsize);

	while (!feof(input) && !ferror(input)) {
		length += fread(buf + length, sizeof(char), bufsize - length, input);
		if (length >= bufsize) {
			bufsize *= 2;
			buf = realloc(buf, bufsize);
		}
	}

	if (feof(input)) {
		buf[length] = '\0';
		return buf;
	} else {
		free(buf);
		return NULL;
	}
}

lisp_value *lisp_parse_progn_f(lisp_runtime *rt, FILE *input)
{
	lisp_value *result;
	char *input_string;
	
	input_string = read_file(input);
	if (!input_string) {
		rt->error = "error reading from input file";
		rt->err_num = LE_FERROR;
		return NULL;
	}
	result = lisp_parse_progn(rt, input_string);
	free(input_string);
	return result;
}

lisp_value *lisp_load_file(lisp_runtime *rt, lisp_scope *scope, FILE *input)
{
	lisp_value *progn = lisp_parse_progn_f(rt, input);
	lisp_error_check(progn);
	return lisp_eval(rt, scope, progn);
}
