#include <stdlib.h>

#include "eval.h"
#include "gc.h"
#include "value.h"

#ifdef DEBUG_GC
# include <stdio.h>
#endif

#define GC_HINT_THRESHOLD 2048
#define GC_INITIAL_THRESHOLD (1024 * 1024)
#define GC_HEAP_GROW_FACTOR 2

static struct cb_gc_header *allocated = NULL;
static size_t amount_allocated = 0;
static size_t next_allocation_threshold = GC_INITIAL_THRESHOLD;
static size_t hint = 0;

inline void cb_gc_register(struct cb_gc_header *obj, size_t size,
		cb_deinit_fn *deinit_fn)
{
	obj->deinit = deinit_fn;
	obj->mark = 0;
	obj->refcount = 0;
	obj->size = size;
	obj->next = allocated;
	allocated = obj;
	amount_allocated += size;
}

inline int cb_gc_should_collect(void)
{
	return hint > GC_HINT_THRESHOLD
		|| amount_allocated > next_allocation_threshold;
}

inline void cb_gc_adjust_refcount(cb_gc_header *obj, int amount)
{
	obj->refcount += amount;
	if (obj->refcount == 0) {
		hint += 1;
		if (cb_gc_should_collect()) {
#ifdef DEBUG_GC
			printf("GC: collecting due to refcount hint\n");
#endif
			cb_gc_collect();
		}
	}
}

inline void cb_gc_mark(struct cb_gc_header *obj)
{
	obj->mark = 1;
}

inline int cb_gc_is_marked(struct cb_gc_header *obj)
{
	return obj->mark;
}

static void mark(void)
{
	int i;

	/* Roots:
	 * - stack
	 *
	 * Globals have a refcount of at least 1 thanks to the hashmap, so we
	 * don't need to iterate over every global scope map. The global vm
	 * state holds only open upvalues (i.e. they just point to the stack),
	 * so we don't have to worry about those. The closed upvalues will be
	 * marked by the functions that depend on them.
	 */

	for (i = 0; i < cb_vm_state.sp; i += 1)
		cb_value_mark(&cb_vm_state.stack[i]);
}

static void sweep(void)
{
	struct cb_gc_header *current, *tmp, **prev_ptr;

	current = allocated;
	prev_ptr = &allocated;
	while (current) {
		tmp = current;
		current = current->next;
		if (!tmp->mark && tmp->refcount == 0) {
			*prev_ptr = tmp->next;
			if (tmp->deinit)
				tmp->deinit(tmp);
#ifdef DEBUG_GC
			printf("GC: freeing object at %p\n", tmp);
#endif
			amount_allocated -= tmp->size;
			free(tmp);
		} else {
#ifdef DEBUG_GC
			printf("GC: not freeing object at %p (%s)\n", tmp,
					tmp->mark ? "mark" : "refcount");
#endif
			tmp->mark = 0;
			prev_ptr = &tmp->next;
		}
	}
}

void cb_gc_collect(void)
{
#ifdef DEBUG_GC
	size_t before = amount_allocated;
	printf("GC: start; allocated=%zu\n", amount_allocated);
#endif
	hint = 0;
	mark();
	sweep();
	next_allocation_threshold = amount_allocated * GC_HEAP_GROW_FACTOR;
#ifdef DEBUG_GC
	printf("GC: end; allocated=%zu, collected=%zu, next collection at %zu\n",
			amount_allocated, before - amount_allocated,
			next_allocation_threshold);
#endif
}
