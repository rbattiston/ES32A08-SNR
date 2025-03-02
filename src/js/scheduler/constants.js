/**
 * Global constants for the scheduler
 */

// Maximum number of schedules and events
export const MAX_SCHEDULES = 8;
export const MAX_EVENTS = 50;

// WebSocket heartbeat interval (in milliseconds)
export const WS_HEARTBEAT_INTERVAL = 10000;

// Pending schedule timeout (in milliseconds)
export const PENDING_SCHEDULE_TIMEOUT = 300000; // 5 minutes

// Debug helper functions
export function debugPrintln(msg) { 
  console.log("[DEBUG] " + msg); 
}

export function debugPrintf(fmt, ...args) { 
  console.log("[DEBUG] " + fmt, ...args); 
}