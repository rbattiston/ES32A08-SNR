#include "MemoryManager.h"
#include "Utils.h"
#include "esp_heap_caps.h"

// Rest of the file remains the same
void *heap_caps_malloc(size_t size, uint32_t caps) {
    // For now, just use standard malloc
    // In a real implementation, you might want to add more sophisticated logic
    return malloc(size);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
    return calloc(n, size);
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps) {
    return realloc(ptr, size);
}

void heap_caps_free(void *ptr) {
    free(ptr);
}

size_t heap_caps_get_free_size(uint32_t caps) {
    // Simplified implementation
    return ESP.getFreeHeap();
}

size_t heap_caps_get_minimum_free_size(uint32_t caps) {
    return ESP.getMinFreeHeap();
}

size_t heap_caps_get_largest_free_block(uint32_t caps) {
    // This is a very simplified approximation
    return ESP.getMaxAllocHeap();
}

size_t heap_caps_get_total_size(uint32_t caps) {
    // Total heap size is roughly free + used
    return ESP.getFreeHeap() + ESP.getHeapSize();
}

bool heap_caps_check_integrity_all(bool print_errors) {
    // Basic integrity check
    size_t free_heap = ESP.getFreeHeap();
    size_t min_free_heap = ESP.getMinFreeHeap();
    
    if (print_errors && free_heap < min_free_heap) {
        debugPrintf("Heap integrity warning: Current free heap (%d) less than minimum free heap (%d)\n", 
                    free_heap, min_free_heap);
        return false;
    }
    
    return true;
}

bool heap_caps_check_integrity(uint32_t caps, bool print_errors) {
    // For now, just call the all-heap version
    return heap_caps_check_integrity_all(print_errors);
}

void heap_caps_dump(uint32_t caps) {
    debugPrintf("Heap Information:\n");
    debugPrintf("  Total Heap: %d bytes\n", ESP.getHeapSize());
    debugPrintf("  Free Heap: %d bytes\n", ESP.getFreeHeap());
    debugPrintf("  Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
    debugPrintf("  Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
}

void heap_caps_dump_all(void) {
    heap_caps_dump(MALLOC_CAP_DEFAULT);
}

// Maximum time between memory optimization checks (in milliseconds)
const unsigned long MEMORY_OPTIMIZATION_INTERVAL = 60000; // 1 minute
static unsigned long lastMemoryOptimizationTime = 0;

void initMemoryManager() {
    debugPrintln("Initializing Memory Manager");
    
    // Initial memory diagnostics
    logMemoryDiagnostics();
    
    // Check initial heap integrity
    checkHeapIntegrity();
}

void performMemoryOptimization() {
    unsigned long currentTime = millis();
    
    // Only perform optimization periodically
    if (currentTime - lastMemoryOptimizationTime < MEMORY_OPTIMIZATION_INTERVAL) {
        return;
    }
    
    lastMemoryOptimizationTime = currentTime;
    
    debugPrintln("Performing Memory Optimization");
    
    // Get heap information before optimization
    size_t before_free = ESP.getFreeHeap();
    
    // Optional: Perform memory consolidation
    void* temp_block = heap_caps_malloc(1024, MALLOC_CAP_DEFAULT);
    if (temp_block) {
        heap_caps_free(temp_block);
    }
    
    // Get heap information after optimization
    size_t after_free = ESP.getFreeHeap();
    
    // Log optimization results
    debugPrintf("Memory Optimization Results:\n");
    debugPrintf("  Free heap before: %d bytes\n", before_free);
    debugPrintf("  Free heap after: %d bytes\n", after_free);
}

void logMemoryDiagnostics() {
    debugPrintln("Memory Diagnostics:");
    
    // Memory capability types to check
    const uint32_t cap_types[] = {
        MALLOC_CAP_DEFAULT,
        MALLOC_CAP_8BIT,
        MALLOC_CAP_32BIT,
        MALLOC_CAP_DMA
    };
    
    for (size_t i = 0; i < sizeof(cap_types)/sizeof(cap_types[0]); i++) {
        uint32_t caps = cap_types[i];
        
        debugPrintf("Memory Capabilities: 0x%08X\n", caps);
        debugPrintf("  Total Size: %d bytes\n", heap_caps_get_total_size(caps));
        debugPrintf("  Free Size: %d bytes\n", heap_caps_get_free_size(caps));
        debugPrintf("  Largest Free Block: %d bytes\n", 
                    heap_caps_get_largest_free_block(caps));
        debugPrintf("  Minimum Free Size: %d bytes\n", 
                    heap_caps_get_minimum_free_size(caps));
    }
}

void* safeHeapAlloc(size_t size, uint32_t caps) {
    void* ptr = heap_caps_malloc(size, caps);
    
    if (!ptr) {
        debugPrintf("Memory Allocation Failed: %d bytes with caps 0x%08X\n", size, caps);
        
        // Optional: Perform memory optimization before retrying
        performMemoryOptimization();
        
        // Retry allocation
        ptr = heap_caps_malloc(size, caps);
        
        if (!ptr) {
            debugPrintf("Second allocation attempt failed. Allocation impossible.\n");
        }
    }
    
    return ptr;
}

bool checkHeapIntegrity() {
    bool integrityStatus = heap_caps_check_integrity_all(true);
    
    if (!integrityStatus) {
        debugPrintln("CRITICAL: Heap Integrity Check FAILED!");
        
        // Additional logging
        heap_caps_dump_all();
    } else {
        debugPrintln("Heap Integrity Check: PASSED");
    }
    
    return integrityStatus;
}