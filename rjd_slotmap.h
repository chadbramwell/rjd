#pragma once

#define RJD_SLOTMAP_H 1

struct rjd_slot
{
	uint16_t index;
	uint16_t salt;
};

static inline bool rjd_slot_isvalid(struct rjd_slot slot);
static inline void rjd_slot_invalidate(struct rjd_slot* slot);

#define rjd_slotmap_alloc(type, capacity, allocator)	(rjd_slotmap_alloc_impl(sizeof(type), capacity, allocator))
#define rjd_slotmap_insert(map, data, out_slot)		    (rjd_slotmap_insert_impl((void**)(&map), (out_slot)), \
													        (map)[(out_slot)->index] = data)
#define rjd_slotmap_contains(map, slot)				    (rjd_slotmap_contains_impl((map), (slot)))
#define rjd_slotmap_get(map, slot)				    	((map) + rjd_slotmap_get_impl((map), (slot)))
#define rjd_slotmap_count(map)					    	(rjd_slotmap_count_impl(map))
#define rjd_slotmap_erase(map, slot)			    	(rjd_slotmap_erase_impl((map), slot))
#define rjd_slotmap_free(map)					    	(rjd_slotmap_free_impl(map))
#define rjd_slotmap_next(map, slot)				    	(rjd_slotmap_next_impl((map), (slot))) // pass null slot for first

void* rjd_slotmap_alloc_impl(size_t sizeof_type, uint32_t capacity, struct rjd_mem_allocator* allocator);
void rjd_slotmap_insert_impl(void** map_p, struct rjd_slot* out_slot);
bool rjd_slotmap_contains_impl(const void* map, struct rjd_slot slot);
uint32_t rjd_slotmap_get_impl(const void* map, struct rjd_slot slot);
uint32_t rjd_slotmap_count_impl(const void* map);
void rjd_slotmap_erase_impl(void* map, struct rjd_slot slot);
void rjd_slotmap_free_impl(void* map);
struct rjd_slot rjd_slotmap_next_impl(void* map, const struct rjd_slot* slot);

static inline bool rjd_slot_isvalid(struct rjd_slot slot)
{
	return slot.salt != 0;
}

static inline void rjd_slot_invalidate(struct rjd_slot* slot)
{
	RJD_ASSERT(slot);
	slot->salt = 0;
}

#if RJD_IMPL

////////////////////////////////////////////////////////////////////////////////
// private interface

struct rjd_slotmap_header
{
	struct rjd_mem_allocator* allocator;
	void* data;
	uint16_t* salts;
	bool* used;
	uint32_t* freelist;
	uint32_t sizeof_type;
    uint32_t capacity;
	uint32_t count;
	uint32_t debug_sentinel;
};

enum {
	RJD_SLOTMAP_DEBUG_SENTINEL = 0x5A5A5A5A,
};

static struct rjd_slotmap_header* rjd_slotmap_getheader(const void* map);
static void* rjd_slotmap_grow(void* oldmap, size_t sizeof_type, uint32_t capacity, struct rjd_mem_allocator* allocator);

////////////////////////////////////////////////////////////////////////////////
// public implementation

void* rjd_slotmap_alloc_impl(size_t sizeof_type, uint32_t capacity, struct rjd_mem_allocator* allocator)
{
	return rjd_slotmap_grow(NULL, sizeof_type, capacity, allocator);
}

void rjd_slotmap_insert_impl(void** map_p, struct rjd_slot* out_slot)
{
	RJD_ASSERT(map_p);
	RJD_ASSERT(*map_p);
	RJD_ASSERT(out_slot);

	void* map = *map_p;

	struct rjd_slotmap_header* header = rjd_slotmap_getheader(map);

	if (rjd_array_count(header->freelist) == 0) {
		map = rjd_slotmap_grow(map, header->sizeof_type, header->capacity * 2, header->allocator);
		header = rjd_slotmap_getheader(map);
		RJD_ASSERT(rjd_array_count(header->freelist) > 0);
		*map_p = map;
	}

	uint32_t index = rjd_array_pop(header->freelist);

	uint16_t* salt = header->salts + index;
	*salt += 1;

	header->used[index] = true;
    ++header->count;

	out_slot->index = rjd_math_truncate_u32_to_u16(index);
	out_slot->salt = *salt;
}

bool rjd_slotmap_contains_impl(const void* map, struct rjd_slot slot)
{
	RJD_ASSERT(map);

	const struct rjd_slotmap_header* header = rjd_slotmap_getheader(map);
	if (slot.index >= header->capacity) {
		return false;
	}
	uint32_t index = slot.index;
	return !rjd_array_contains(header->freelist, &index);
}

uint32_t rjd_slotmap_get_impl(const void* map, struct rjd_slot slot)
{
	RJD_ASSERT(map);

	const struct rjd_slotmap_header* header = rjd_slotmap_getheader(map);
	RJD_ASSERT(slot.index < header->capacity);
	uint32_t index = slot.index;
	RJD_ASSERTMSG(!rjd_array_contains(header->freelist, &index), "This slot is unallocated. Use rjd_slotmap_contains to check if the slot is valid first.");
    int16_t salt = header->salts[slot.index];
	RJD_ASSERT(salt == slot.salt);
	return slot.index;
}

uint32_t rjd_slotmap_count_impl(const void* map)
{
	RJD_ASSERT(map);

	const struct rjd_slotmap_header* header = rjd_slotmap_getheader(map);
	return header->count;
}

void rjd_slotmap_erase_impl(void* map, struct rjd_slot slot)
{
	RJD_ASSERT(map);
	struct rjd_slotmap_header* header = rjd_slotmap_getheader(map);
	RJD_ASSERT(slot.index < header->capacity);
	
	rjd_array_push(header->freelist, slot.index);
	header->used[slot.index] = false;
    --header->count;
}

void rjd_slotmap_free_impl(void* map)
{
	RJD_ASSERT(map);
	struct rjd_slotmap_header* header = rjd_slotmap_getheader(map);

    rjd_array_free(header->used);
	rjd_array_free(header->freelist);
	rjd_mem_free(header);
}

struct rjd_slot rjd_slotmap_next_impl(void* map, const struct rjd_slot* slot)
{
	RJD_ASSERT(map);
	struct rjd_slotmap_header* header = rjd_slotmap_getheader(map);

	uint32_t start = 0;
	if (slot) {
		start = slot->index + 1;
	}

	for (uint32_t i = start; i < header->capacity; ++i) {
		if (header->used[i]) {
			struct rjd_slot next = {
				.index = rjd_math_truncate_u32_to_u16(i),
				.salt = header->salts[i],
			};
			return next;
		}
	}

	struct rjd_slot next = {0};
	RJD_ASSERT(!rjd_slot_isvalid(next));
	return next;
}

////////////////////////////////////////////////////////////////////////////////
// private implementation

struct rjd_slotmap_header* rjd_slotmap_getheader(const void* map)
{
	char* raw = (char*)map;
	char* rawheader = (raw - sizeof(struct rjd_slotmap_header));
	struct rjd_slotmap_header* header = (struct rjd_slotmap_header*)rawheader;
	RJD_ASSERTMSG(header->debug_sentinel == RJD_SLOTMAP_DEBUG_SENTINEL, 
		"Debug sentinel does not match. Address %p does not point to a slotmap or there was a buffer underrun corruption.", map);
	return header;
}

void* rjd_slotmap_grow(void* oldmap, size_t sizeof_type, uint32_t capacity, struct rjd_mem_allocator* allocator)
{
	struct rjd_slotmap_header* oldheader = oldmap ? rjd_slotmap_getheader(oldmap) : NULL;

	uint32_t oldcapacity = oldheader ? oldheader->capacity : 0;
	if (capacity <= oldcapacity) {
		return oldmap;
	}

	size_t total_mem_size = sizeof(struct rjd_slotmap_header) + sizeof_type * capacity + sizeof(uint16_t) * capacity;
	char* mem = rjd_mem_alloc_array(char, total_mem_size, allocator);
	memset(mem, 0, total_mem_size);

	struct rjd_slotmap_header* header = (struct rjd_slotmap_header*)mem;
	header->allocator = allocator;
	header->sizeof_type = (uint32_t)sizeof_type;
	header->capacity = capacity;
    header->count = 0;
	header->data = (void*)(mem + sizeof(struct rjd_slotmap_header));
	header->salts = (uint16_t*)((char*)header->data + sizeof_type * capacity);
	header->used = rjd_array_alloc(bool, capacity, allocator);
	header->freelist = rjd_array_alloc(uint32_t, capacity, allocator);
	header->debug_sentinel = RJD_SLOTMAP_DEBUG_SENTINEL;

	memset(header->salts + oldcapacity, 0, (header->capacity - oldcapacity) * sizeof(*header->salts));

	rjd_array_resize(header->used, capacity);
	if (oldheader) {
		memcpy(header->used, oldheader->used, sizeof(*header->used) * oldheader->capacity);
        header->count = oldheader->count;
	}

	for (uint32_t i = oldcapacity; i < capacity; ++i) {
		rjd_array_push(header->freelist, i);
	}

	// copy existing data
	if (oldheader) {
		memcpy(header->data, oldheader->data, sizeof_type * oldheader->capacity);
		memcpy(header->salts, oldheader->salts, sizeof(uint16_t) * oldheader->capacity);
		rjd_slotmap_free(oldmap);
	}

	return header->data;
}

#endif

