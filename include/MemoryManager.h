#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Memory management initialization function
void initMemoryManager();

// Perform periodic memory optimization
void performMemoryOptimization();

// Advanced memory diagnostics
void logMemoryDiagnostics();

// Safe memory allocation wrapper
void* safeHeapAlloc(size_t size, uint32_t caps);

// Check and log heap integrity
bool checkHeapIntegrity();

// Memory capability flags for allocation
#define MALLOC_CAP_EXEC           (1<<0)   // Memory must be executable
#define MALLOC_CAP_32BIT          (1<<1)   // Memory must be 32-bit addressable
#define MALLOC_CAP_8BIT           (1<<2)   // Memory must be 8-bit addressable
#define MALLOC_CAP_DMA            (1<<3)   // Memory suitable for DMA
#define MALLOC_CAP_PID2           (1<<4)   // Memory with PID2 capabilities
#define MALLOC_CAP_DEFAULT        (1<<5)   // Default memory capabilities

#endif // MEMORY_MANAGER_H