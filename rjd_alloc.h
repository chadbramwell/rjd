#pragma once

// DEPENDENCIES: 
// stdlib.h (malloc/free)
// rjd_debug.h

// TODO realloc
typedef void* (*rjd_func_alloc)(size_t size);
typedef void* (*rjd_func_alloc_scoped)(size_t size, void* allocator);

typedef void (*rjd_func_free)(void* memory);
typedef void (*rjd_func_free_scoped)(void* memory, void* heap);

struct rjd_alloc_context
{
	rjd_func_alloc alloc_global;
	rjd_func_free free_global;
	
	rjd_func_alloc_scoped alloc_scoped;
	rjd_func_free_scoped free_scoped;
	void* scope;
};

struct rjd_linearheap
{
	void* base;
	void* next;
	size_t size;
};

struct rjd_alloc_context rjd_alloc_initglobal(rjd_func_alloc a, rjd_func_free f);
struct rjd_alloc_context rjd_alloc_initscoped(rjd_func_alloc_scoped a, rjd_func_free_scoped f, void* allocator);
struct rjd_alloc_context rjd_alloc_initdefault();
struct rjd_alloc_context rjd_alloc_initlinearheap(size_t heapsize);

void* rjd_malloc(size_t size, struct rjd_alloc_context* context);
void rjd_free(void* mem, struct rjd_alloc_context* context);

struct rjd_linearheap rjd_linearheap_init(void* mem, size_t size);

void* rjd_linearheap_malloc(size_t size, void* heap);
void rjd_free_scoped_noop(void* mem, void* heap);

#if RJD_ENABLE_SHORTNAMES
	// ????
#endif

#ifdef RJD_IMPL

struct rjd_alloc_context rjd_alloc_initglobal(rjd_func_alloc a, rjd_func_free f)
{
	struct rjd_alloc_context context = {0};
	context.alloc_global = a;
	context.free_global = f;

	return context;
}

struct rjd_alloc_context rjd_alloc_initscoped(rjd_func_alloc_scoped a, rjd_func_free_scoped f, void* heap)
{
	struct rjd_alloc_context context = {0};
	context.alloc_scoped = a;
	context.free_scoped = f;
	context.scope = heap;

	return context;
}

struct rjd_alloc_context rjd_alloc_initdefault()
{
	return rjd_alloc_initglobal(malloc, free);
}

struct rjd_alloc_context rjd_alloc_initlinearheap(size_t heapsize)
{
	void* mem = malloc(heapsize);

	const size_t structsize = sizeof(struct rjd_linearheap);
	struct rjd_linearheap* heap = (struct rjd_linearheap*)mem + heapsize - structsize;
	*heap = rjd_linearheap_init(mem, heapsize - structsize);

	return rjd_alloc_initscoped(rjd_linearheap_malloc, rjd_free_scoped_noop, heap);
}

void* rjd_malloc(size_t size, struct rjd_alloc_context* context)
{
	if (context->alloc_global) {
		return context->alloc_global(size);
	}
	return context->alloc_scoped(size, context->scope);
}

void rjd_free(void* mem, struct rjd_alloc_context* context)
{
	if (context->free_global) {
		context->free_global(mem);
	} else {
		context->free_scoped(mem, context->scope);
	}
}

struct rjd_linearheap rjd_linearheap_init(void* mem, size_t size)
{
	struct rjd_linearheap heap = {0};
	heap.base = mem;
	heap.next = mem;
	heap.size = size;
	return heap;
}

void* rjd_linearheap_malloc(size_t size, void* userheap)
{
	struct rjd_linearheap* heap = (struct rjd_linearheap*)userheap;
	
	if ((char*)heap->next + size <= (char*)heap->base + heap->size) 
	{
		void* mem = (char*)heap->next;
		heap->next = (char*)heap->next + size;

		return mem;
	}

	return NULL;
}

void rjd_free_scoped_noop(void* mem, void* heap)
{
	RJD_UNUSED_PARAM(mem);
	RJD_UNUSED_PARAM(heap);
}

#endif // RJD_IMPL

