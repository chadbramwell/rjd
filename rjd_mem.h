#pragma once

#define RJD_MEM 1

struct rjd_mem_allocator_stats
{
	uint32_t total_size;
	struct {
		uint32_t used;
		uint32_t overhead;
		uint32_t peak;
		uint32_t unused;
		uint32_t allocs;
		uint32_t frees;
	} current;
	struct {
		uint32_t peak;
		uint32_t allocs;
		uint32_t frees;
		uint32_t resets;
	} lifetime;
};

// TODO realloc
typedef const char* (*rjd_mem_allocator_type_func)(void);
typedef void* (*rjd_mem_allocator_alloc_func)(size_t size, void* optional_heap);
typedef void (*rjd_mem_allocator_free_func)(void* memory);
typedef void (*rjd_mem_allocator_reset_func)(void* optional_heap);

struct rjd_mem_allocator
{
	rjd_mem_allocator_type_func type_func; // must return a static string
	rjd_mem_allocator_alloc_func alloc_func;
	rjd_mem_allocator_free_func free_func;
	rjd_mem_allocator_reset_func reset_func; // optional
	void* optional_heap;

	struct rjd_mem_allocator_stats stats;

	uint32_t debug_sentinel;
};

struct rjd_mem_allocator rjd_mem_allocator_init_default(void);
struct rjd_mem_allocator rjd_mem_allocator_init_linear(void* memblock, size_t size);

const char* rjd_mem_allocator_type(struct rjd_mem_allocator* allocator);
bool rjd_mem_allocator_reset(struct rjd_mem_allocator* allocator);
struct rjd_mem_allocator_stats rjd_mem_allocator_getstats(const struct rjd_mem_allocator* allocator);

void rjd_mem_free(const void* mem);
void rjd_mem_swap(void* restrict mem1, void* restrict mem2, size_t size);

#define rjd_mem_alloc(type, allocator) ((type*)rjd_mem_alloc_impl(sizeof(type), (allocator), 4))
#define rjd_mem_alloc_aligned(type, allocator, alignment) ((type*)rjd_mem_alloc_impl(sizeof(type), allocator, alignment))
#define rjd_mem_alloc_array(type, count, allocator) ((type*)rjd_mem_alloc_impl(sizeof(type) * count, allocator, 4))
#define rjd_mem_alloc_array_aligned(type, count, allocator, alignment) ((type*)rjd_mem_alloc_impl(sizeof(type) * count, allocator, alignment))

#define RJD_MEM_ISALIGNED(p, align) (((uintptr_t)(p) & ((align)-1)) == 0)
#define RJD_MEM_ALIGN(size, align) ((size) + (RJD_MEM_ISALIGNED(size, align) ? 0 : ((align) - ((size) & ((align)-1)))))

void* rjd_mem_alloc_impl(size_t size, struct rjd_mem_allocator* allocator, uint32_t alignment);

////////////////////////////////////////////////////////////////////////////////

#ifdef RJD_IMPL

struct rjd_mem_heap_linear
{
	void* base;
	void* next;
	size_t size;
	uint32_t debug_sentinel;
};

struct rjd_mem_allocation_header
{
	struct rjd_mem_allocator* allocator;
	uint8_t offset_to_block_begin_from_user;
	uint32_t total_blocksize;
	uint32_t debug_sentinel;
};

static const char* rjd_mem_allocator_global_type(void);
static void* rjd_mem_allocator_global_alloc(size_t size, void* heap);
static void rjd_mem_allocator_global_free(void* mem);

static const char* rjd_mem_allocator_linear_type(void);
static void* rjd_mem_allocator_linear_alloc(size_t size, void* heap);
static void rjd_mem_allocator_linear_reset(void* heap);

const uint32_t RJD_MEM_DEBUG_DEFAULT_SENTINEL32 = 0xA7A7A7A7u;
const uint32_t RJD_MEM_DEBUG_LINEAR_SENTINEL32 = 0xA8A8A8A8u;
const uint32_t RJD_MEM_STATS_UNKNOWN_UPPERBOUND = UINT32_MAX;

struct rjd_mem_allocator rjd_mem_allocator_init_default()
{
	struct rjd_mem_allocator allocator = {
		.type_func = rjd_mem_allocator_global_type,
		.alloc_func = NULL,
		.free_func = NULL,
		.reset_func = NULL,
		.optional_heap = NULL,
		.stats = {
			.total_size = RJD_MEM_STATS_UNKNOWN_UPPERBOUND,
			.current = {
				.unused = RJD_MEM_STATS_UNKNOWN_UPPERBOUND,
			},
		},
		.debug_sentinel = RJD_MEM_DEBUG_DEFAULT_SENTINEL32,
	};

	// MSVC has a slightly different signature for malloc/free so to avoid platform-specific
	// code, we just wrap them for all platforms here
	allocator.alloc_func = rjd_mem_allocator_global_alloc;
	allocator.free_func = rjd_mem_allocator_global_free;

	return allocator;
}

struct rjd_mem_allocator rjd_mem_allocator_init_linear(void* memblock, size_t size)
{
	RJD_ASSERT(memblock);

	struct rjd_mem_heap_linear* heap = (struct rjd_mem_heap_linear*)RJD_MEM_ALIGN((uintptr_t)memblock, 64);
	uint32_t usable_size = 0;
	{
		char* usable_memory_start = (char*)RJD_MEM_ALIGN((uintptr_t)heap + sizeof(struct rjd_mem_heap_linear), 64);
		char* usable_memory_end = (char*)memblock + size;
		RJD_ASSERTMSG(usable_memory_start < usable_memory_end, 
			"Given size was not large enough to make a heap. You need at least 128 bytes...");

		usable_size = (uint32_t)(usable_memory_end - usable_memory_start);

		struct rjd_mem_heap_linear copy = { 
			.base = usable_memory_start, 
			.next = usable_memory_start, 
			.size = usable_size, 
			.debug_sentinel = RJD_MEM_DEBUG_LINEAR_SENTINEL32 
		};
		*heap = copy;
	}

	struct rjd_mem_allocator allocator = {
		.type_func = rjd_mem_allocator_linear_type,
		.alloc_func = rjd_mem_allocator_linear_alloc,
		.free_func = NULL,
		.reset_func = rjd_mem_allocator_linear_reset,
		.optional_heap = heap,
		.stats = {
			.total_size = usable_size,
			.current = {
				.unused = usable_size,
			},
		},
		.debug_sentinel = RJD_MEM_DEBUG_LINEAR_SENTINEL32,
	};

	return allocator;
}

const char* rjd_mem_allocator_type(struct rjd_mem_allocator* allocator)
{
	RJD_ASSERT(allocator);
	return allocator->type_func();
}

bool rjd_mem_allocator_reset(struct rjd_mem_allocator* allocator)
{
	RJD_ASSERT(allocator);

	allocator->stats.current.used = 0;
	allocator->stats.current.overhead = 0;
	allocator->stats.current.peak = 0;
	allocator->stats.current.unused = allocator->stats.total_size;
	allocator->stats.current.allocs = 0;
	allocator->stats.current.frees = 0;
	++allocator->stats.lifetime.resets;

	if (allocator->reset_func) {
		allocator->reset_func(allocator->optional_heap);

		return true;
	}
	return false;
}

struct rjd_mem_allocator_stats rjd_mem_allocator_getstats(const struct rjd_mem_allocator* allocator)
{
	RJD_ASSERT(allocator);
	return allocator->stats;
}

void* rjd_mem_alloc_impl(size_t size, struct rjd_mem_allocator* allocator, uint32_t alignment)
{
	RJD_ASSERT(allocator);
    RJD_ASSERT(size >= 0)
	//RJD_ASSERTMSG(alignment <= 16, "TODO implment extra support for large alignments");

	const uint32_t header_size = sizeof(struct rjd_mem_allocation_header);
	const uint32_t alignment_padding = header_size < alignment ? alignment - header_size : 0;
	const uint32_t total_size = (uint32_t)size + header_size + alignment_padding;

	char* raw = allocator->alloc_func(total_size, allocator->optional_heap);
	if (raw == NULL) {
		return raw;
	}

	{
		uint32_t* current_used = &allocator->stats.current.used;
		uint32_t* current_peak = &allocator->stats.current.peak;
		uint32_t* lifetime_peak = &allocator->stats.lifetime.peak;
		*current_used += total_size;
		allocator->stats.current.overhead += header_size + alignment_padding;
		*current_peak = (*current_peak < *current_used) ? *current_used : *current_peak;
		*lifetime_peak = (*lifetime_peak < *current_used) ? *current_used : *lifetime_peak;
		if (allocator->stats.current.unused != RJD_MEM_STATS_UNKNOWN_UPPERBOUND) {
			RJD_ASSERT(allocator->stats.current.unused >= total_size);
			allocator->stats.current.unused -= total_size;
		}
		++allocator->stats.current.allocs;
		++allocator->stats.lifetime.allocs;
	}

	char* raw_user = raw + header_size + alignment_padding;
	uintptr_t aligned_user = RJD_MEM_ALIGN((uintptr_t)raw_user, alignment);

	struct rjd_mem_allocation_header* header = (void*)(aligned_user - header_size);
	header->allocator = allocator;
	header->total_blocksize = (uint32_t)total_size;
	RJD_ASSERT(header_size + alignment_padding < 255);
	header->offset_to_block_begin_from_user = (uint8_t)(header_size + alignment_padding);
	header->debug_sentinel = allocator->debug_sentinel;

	uintptr_t user = RJD_MEM_ALIGN((uintptr_t)raw + sizeof(struct rjd_mem_allocator*), alignment);
	return (void*)user;
}

void rjd_mem_free(const void* mem)
{
	if (!mem) {
		return;
	}

	char* raw = (void*)mem;
	struct rjd_mem_allocation_header* header = (void*)(raw - sizeof(struct rjd_mem_allocation_header));
	struct rjd_mem_allocator* allocator = header->allocator;

	RJD_ASSERTMSG(header->debug_sentinel == allocator->debug_sentinel, "This memory was not allocated with rjd_mem_alloc.");

	if (allocator->free_func) {
		char* begin = raw - header->offset_to_block_begin_from_user;
		allocator->free_func(begin);

		RJD_ASSERT(allocator->stats.current.used >= header->total_blocksize);
		allocator->stats.current.used -= header->total_blocksize;
		if (allocator->stats.current.unused != RJD_MEM_STATS_UNKNOWN_UPPERBOUND) {
			allocator->stats.current.unused += header->total_blocksize;
		}
		++allocator->stats.current.frees;
		++allocator->stats.lifetime.frees;
	}
}

void rjd_mem_swap(void* restrict mem1, void* restrict mem2, size_t size)
{
	uint8_t tmp[1024];
	RJD_ASSERTMSG(size < (uint32_t)sizeof(tmp), "Increase size of static buffer to at least %u", size);

	memcpy(tmp, mem1, size);
	memcpy(mem1, mem2, size);
	memcpy(mem2, tmp, size);
}

////////////////////////////////////////////////////////////////////////////////
// local helper definitions

const char* rjd_mem_allocator_global_type(void)
{
	return "rjd_global";
}

void* rjd_mem_allocator_global_alloc(size_t size, void* unused_heap)
{
	RJD_UNUSED_PARAM(unused_heap);

	return malloc(size);
}

void rjd_mem_allocator_global_free(void* mem)
{
	free(mem);
}

const char* rjd_mem_allocator_linear_type(void)
{
	return "rjd_linear";
}

void* rjd_mem_allocator_linear_alloc(size_t size, void* optional_heap)
{
	struct rjd_mem_heap_linear* heap = (struct rjd_mem_heap_linear*)optional_heap;
	RJD_ASSERT(heap->debug_sentinel == RJD_MEM_DEBUG_LINEAR_SENTINEL32);

	size_t align_diff = size % 8;
	if (align_diff != 0) {
		size += align_diff;
	}

	if ((char*)heap->next + size <= (char*)heap->base + heap->size) 
	{
		void* mem = (char*)heap->next;
		heap->next = (char*)heap->next + size;

		return mem;
	}

	return NULL;
}

void rjd_mem_allocator_linear_reset(void* optional_heap)
{
	struct rjd_mem_heap_linear* heap = (struct rjd_mem_heap_linear*)optional_heap;
	RJD_ASSERT(heap->debug_sentinel == RJD_MEM_DEBUG_LINEAR_SENTINEL32);

	heap->next = heap->base;
}

#endif // RJD_IMPL

