#ifndef ESP_HEAP_CAPS_H
#define ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Memory capability flags
#define MALLOC_CAP_EXEC           (1<<0)   // Memory must be executable
#define MALLOC_CAP_32BIT          (1<<1)   // Memory must be 32-bit addressable
#define MALLOC_CAP_8BIT           (1<<2)   // Memory must be 8-bit addressable
#define MALLOC_CAP_DMA            (1<<3)   // Memory suitable for DMA
#define MALLOC_CAP_PID2           (1<<4)   // Memory with PID2 capabilities
#define MALLOC_CAP_DEFAULT        (1<<5)   // Default memory capabilities

// Memory allocation functions
void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps);

// Memory deallocation
void heap_caps_free(void *ptr);

// Heap information functions
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);

// Heap integrity and debugging
bool heap_caps_check_integrity_all(bool print_errors);
bool heap_caps_check_integrity(uint32_t caps, bool print_errors);
void heap_caps_dump(uint32_t caps);
void heap_caps_dump_all(void);

#ifdef __cplusplus
}
#endif

#endif // ESP_HEAP_CAPS_H