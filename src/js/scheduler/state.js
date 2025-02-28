/**
 * State management for the scheduler
 */
import { MAX_EVENTS, debugPrintln } from './constants';
import { renderEventList, renderTimeline } from './ui/events';
import { renderActiveSchedules } from './ui/active-schedules';

// Global scheduler state model
export const schedulerState = {
  scheduleCount: 0,
  currentScheduleIndex: 0,
  schedules: []
};

// Update the active schedule from UI values
export function updateActiveScheduleFromUI() {
  debugPrintln("Updating active schedule from UI");
  
  if (schedulerState.scheduleCount === 0) {
    debugPrintln("No schedules to update");
    return;
  }
  
  const scheduleNameInput = document.getElementById("schedule-name");
  const lightsOnInput = document.getElementById("lights-on-time");
  const lightsOffInput = document.getElementById("lights-off-time");
  const relayMaskContainer = document.getElementById("relay-mask");
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  
  // Update basic info
  schedule.name = scheduleNameInput.value;
  schedule.lightsOnTime = lightsOnInput.value;
  schedule.lightsOffTime = lightsOffInput.value;
  
  // Update relay mask from checkboxes
  let relayMask = 0;
  const checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
  checkboxes.forEach((checkbox, index) => {
    if (checkbox.checked) {
      relayMask |= (1 << index);
    }
  });
  schedule.relayMask = relayMask;
}

// Load scheduler state from the server
export async function loadSchedulerState() {
  debugPrintln("Loading scheduler state from server");
  
  try {
    const response = await fetch("/api/scheduler/load");
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    
    // Update global state
    schedulerState.scheduleCount = data.scheduleCount || 0;
    schedulerState.currentScheduleIndex = data.currentScheduleIndex || 0;
    schedulerState.schedules = data.schedules || [];
    
    debugPrintln(`Loaded ${schedulerState.scheduleCount} schedules`);
    
    return true;
  } catch (error) {
    console.error("Error loading scheduler state:", error);
    return false;
  }
}

// Save scheduler state to the server
export async function saveSchedulerState() {
  debugPrintln("Saving scheduler state to server");
  
  // Update the active schedule with UI values first
  updateActiveScheduleFromUI();
  
  // Ensure consistent events array and eventCount
  for (let i = 0; i < schedulerState.scheduleCount; i++) {
    const schedule = schedulerState.schedules[i];
    
    // Initialize events array if needed
    if (!schedule.events) {
      schedule.events = [];
    }
    
    // Update eventCount to match events array length
    schedule.eventCount = schedule.events.length;
  }
  
  try {
    const response = await fetch("/api/scheduler/save", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(schedulerState)
    });
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const result = await response.json();
    debugPrintln("Save result:", result);
    
    // Update UI after saving
    renderEventList();
    renderTimeline();
    renderActiveSchedules();
    
    alert("Schedule saved successfully");
    return true;
  } catch (error) {
    console.error("Error saving scheduler state:", error);
    alert("Failed to save schedule: " + error.message);
    return false;
  }
}

// Create a new schedule
export function createNewSchedule() {
  debugPrintln("Creating new schedule");
  
  if (schedulerState.scheduleCount >= MAX_EVENTS) {
    alert(`Maximum number of schedules (${MAX_SCHEDULES}) reached`);
    return;
  }
  
  // Generate default name with timestamp
  const now = new Date();
  const defaultName = `Schedule ${now.toLocaleString()}`;
  
  // Create new schedule object
  const newSchedule = {
    name: defaultName,
    metadata: now.toISOString(),
    relayMask: 0,
    lightsOnTime: "06:00",
    lightsOffTime: "18:00",
    eventCount: 0,
    events: []
  };
  
  // Add to scheduler state
  schedulerState.schedules.push(newSchedule);
  schedulerState.scheduleCount = schedulerState.schedules.length;
  schedulerState.currentScheduleIndex = schedulerState.scheduleCount - 1;
  
  // Save changes
  saveSchedulerState();
}