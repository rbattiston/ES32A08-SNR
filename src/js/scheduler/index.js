/**
 * Main entry point for the Irrigation Scheduler
 */
import { debugPrintln } from './constants';
import { initScheduler, initUI, updateAllScheduleViews } from './ui/core';
import { startTimelineUpdates } from './ui/events';

// Initialize the app when the DOM is loaded
document.addEventListener("DOMContentLoaded", async () => {
  debugPrintln("DOM loaded, initializing scheduler application");
  
  // Initialize UI elements and event listeners
  initUI();
  
  // Start timeline updates
  startTimelineUpdates();
  
  // Initialize and load the scheduler
  await initScheduler();
  
  // Update UI with loaded data
  updateAllScheduleViews();
  
  debugPrintln("Scheduler initialization complete");
});