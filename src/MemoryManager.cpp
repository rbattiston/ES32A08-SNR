#include "MemoryManager.h"
#include "Utils.h"
#include "esp_heap_caps.h"

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
    void* temp_block = malloc(1024);
    if (temp_block) {
        free(temp_block);
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
    
    debugPrintf("Memory Capabilities:\n");
    debugPrintf("  Total Heap: %d bytes\n", ESP.getHeapSize());
    debugPrintf("  Free Heap: %d bytes\n", ESP.getFreeHeap());
    debugPrintf("  Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
    debugPrintf("  Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
}

void* safeHeapAlloc(size_t size, uint32_t caps) {
    void* ptr = malloc(size);
    
    if (!ptr) {
        debugPrintf("Memory Allocation Failed: %d bytes\n", size);
        
        // Optional: Perform memory optimization before retrying
        performMemoryOptimization();
        
        // Retry allocation
        ptr = malloc(size);
        
        if (!ptr) {
            debugPrintf("Second allocation attempt failed. Allocation impossible.\n");
        }
    }
    
    return ptr;
}

bool checkHeapIntegrity() {
    // Basic integrity check using available ESP methods
    size_t free_heap = ESP.getFreeHeap();
    size_t min_free_heap = ESP.getMinFreeHeap();
    
    if (free_heap < min_free_heap) {
        debugPrintf("Heap integrity warning: Current free heap (%d) less than minimum free heap (%d)\n", 
                    free_heap, min_free_heap);
        return false;
    }
    
    debugPrintln("Heap Integrity Check: PASSED");
    return true;
}