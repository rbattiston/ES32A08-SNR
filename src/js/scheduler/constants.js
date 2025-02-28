/**
 * Global constants for the scheduler
 */

// Maximum number of schedules and events
export const MAX_SCHEDULES = 8;
export const MAX_EVENTS = 50;

// Debug helper functions
export function debugPrintln(msg) { 
  console.log("[DEBUG] " + msg); 
}

export function debugPrintf(fmt, ...args) { 
  console.log("[DEBUG] " + fmt, ...args); 
}