/**
 * Main entry point for the Irrigation Scheduler
 */
import { debugPrintln } from './constants';
import { loadSchedulerState, saveSchedulerState, createNewSchedule } from './state';
import { initUI, populateScheduleDropdown, loadActiveSchedule, updateAllScheduleViews } from './ui/core';
import { renderEventList, renderTimeline, addEvent, initModalListeners } from './ui/events';
import { renderActiveSchedules } from './ui/active-schedules';
import { updateSchedulerStatus, activateScheduler, deactivateScheduler } from './api';

// Initialize the scheduler app
async function initScheduler() {
  debugPrintln("Initializing scheduler application");
  
  // Initialize UI
  initUI();
  
  // Initialize modal event listeners
  initModalListeners();
  
  // Start timeline updates
  import('./ui/timeline').then(timeline => {
    timeline.startTimelineUpdates();
  });
  
  // Load scheduler state
  const success = await loadSchedulerState();
  
  if (success) {
    // Load UI components
    updateAllScheduleViews();
    
    // Check scheduler status
    await updateSchedulerStatus();
  } else {
    // Handle failed load
    debugPrintln("Failed to load scheduler state");
  }
  
  // Add event listeners
  bindEventListeners();
  
  debugPrintln("Scheduler initialization complete");
}

// Bind all event listeners
function bindEventListeners() {
  debugPrintln("Binding event listeners");
  
  // Schedule selection dropdown
  const scheduleDropdown = document.getElementById("schedule-dropdown");
  if (scheduleDropdown) {
    scheduleDropdown.addEventListener("change", handleScheduleChange);
  }
  
  // Add event button
  const addEventButton = document.getElementById("add-event");
  if (addEventButton) {
    addEventButton.addEventListener("click", addEvent);
  }
  
  // Save schedule button
  const saveScheduleButton = document.getElementById("save-schedule");
  if (saveScheduleButton) {
    saveScheduleButton.addEventListener("click", saveSchedulerState);
  }
  
  // Activate scheduler button
  const activateSchedulerButton = document.getElementById("activate-scheduler");
  if (activateSchedulerButton) {
    activateSchedulerButton.addEventListener("click", activateScheduler);
  }
  
  // Deactivate scheduler button
  const deactivateSchedulerButton = document.getElementById("deactivate-scheduler");
  if (deactivateSchedulerButton) {
    deactivateSchedulerButton.addEventListener("click", deactivateScheduler);
  }
  
  // New schedule button
  const newScheduleButton = document.getElementById("new-schedule");
  if (newScheduleButton) {
    newScheduleButton.addEventListener("click", createNewSchedule);
  }
  
  // Listen for schedule selection events
  document.addEventListener('scheduleSelected', () => {
    loadActiveSchedule();
  });
}

// Handle schedule change in dropdown
function handleScheduleChange(event) {
  const index = parseInt(event.target.value);
  debugPrintln(`Schedule changed to index ${index}`);
  
  // Update current index in state
  import('./state').then(({ schedulerState }) => {
    schedulerState.currentScheduleIndex = index;
    loadActiveSchedule();
  });
}

// Initialize the app when the DOM is loaded
document.addEventListener("DOMContentLoaded", initScheduler);