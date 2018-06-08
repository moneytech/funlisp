/*
 * gc.c: mark and sweep garbage collection for funlisp
 *
 * Stephen Brennan <stephen@brennan.io>
 */
#include <assert.h>

#include "funlisp_internal.h"

void lisp_init(lisp_runtime *rt)
{
	rt->nil = type_list->new();
	rt->nil->mark = 0;
	rt->nil->type = type_list;
	rt->nil->next = NULL;
	rt->head = rt->nil;
	rt->tail = rt->nil;
	rt->user = NULL;
	rb_init(&rt->rb, sizeof(lisp_value*), 16);
	rt->error= NULL;
	rt->error_line = 0;
	rt->error_stack = NULL;
	rt->stack = (lisp_list *) rt->nil;
	rt->stack_depth = 0;
}

void lisp_destroy(lisp_runtime *rt)
{
	lisp_sweep(rt);
	rb_destroy(&rt->rb);
	lisp_free(rt->nil);
}

void lisp_mark(lisp_runtime *rt, lisp_value *v)
{
	rb_push_back(&rt->rb, &v);

	while (rt->rb.count > 0) {
		rb_pop_front(&rt->rb, &v);
		v->mark = GC_MARKED;
		struct iterator it = v->type->expand(v);
		while (it.has_next(&it)) {
			v = it.next(&it);
			if (v->mark == GC_NOMARK) {
				v->mark = GC_QUEUED;
				rb_push_back(&rt->rb, &v);
			}
		}
		it.close(&it);
	}
}

void lisp_sweep(lisp_runtime *rt)
{
	lisp_value *curr = rt->head;

	while (curr->next) {
		if (curr->next->mark != GC_MARKED) {
			lisp_value *tmp = curr->next->next;
			lisp_free(curr->next);
			curr->next = tmp;
		} else {
			curr->mark = GC_NOMARK;
			curr = curr->next;
		}
	}

	curr->mark = GC_NOMARK;
	rt->tail = curr;
}
