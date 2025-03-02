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
    // Only include the relay if it's checked and either:
    // 1. Not disabled, or
    // 2. Already assigned to this schedule (checked but disabled)
    if (checkbox.checked) {
      relayMask |= (1 << index);
    }
  });
  schedule.relayMask = relayMask;
}

// Check for relay conflicts across all schedules
function checkRelayConflicts() {
  // Create array to track which schedule is using each relay
  const relayUsage = new Array(8).fill(null);
  const conflicts = [];
  
  for (let i = 0; i < schedulerState.scheduleCount; i++) {
    const schedule = schedulerState.schedules[i];
    const relayMask = schedule.relayMask;
    
    for (let relayIndex = 0; relayIndex < 8; relayIndex++) {
      if ((relayMask & (1 << relayIndex)) !== 0) {
        if (relayUsage[relayIndex] === null) {
          relayUsage[relayIndex] = {
            scheduleIndex: i,
            scheduleName: schedule.name
          };
        } else {
          // Conflict detected!
          conflicts.push({
            relay: relayIndex + 1,
            schedules: [
              relayUsage[relayIndex].scheduleName,
              schedule.name
            ]
          });
        }
      }
    }
  }
  
  return conflicts;
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
  
  // Add visual feedback during saving
  const saveButton = document.getElementById("save-schedule");
  if (saveButton) {
    const originalText = saveButton.textContent;
    saveButton.textContent = "Saving...";
    saveButton.disabled = true;
  }
  
  // Create status message container if it doesn't exist
  let statusMessage = document.getElementById("schedule-status-message");
  if (!statusMessage) {
    statusMessage = document.createElement("div");
    statusMessage.id = "schedule-status-message";
    statusMessage.style.padding = "10px";
    statusMessage.style.margin = "10px 0";
    statusMessage.style.borderRadius = "4px";
    statusMessage.style.display = "none";
    
    // Insert after the controls section
    const controlsSection = document.getElementById("scheduler-controls");
    if (controlsSection) {
      controlsSection.parentNode.insertBefore(statusMessage, controlsSection.nextSibling);
    }
  }
  
  // Hide status message initially
  statusMessage.style.display = "none";
  
  // Update the active schedule with UI values first
  updateActiveScheduleFromUI();
  
  // Check for conflicts
  const conflicts = checkRelayConflicts();
  if (conflicts.length > 0) {
    // Format the warning message
    let warningMsg = "Warning: Some relays are assigned to multiple schedules:\n";
    conflicts.forEach(conflict => {
      warningMsg += `- Relay ${conflict.relay} is used by both "${conflict.schedules[0]}" and "${conflict.schedules[1]}"\n`;
    });
    
    statusMessage.textContent = warningMsg;
    statusMessage.style.backgroundColor = "#fff3cd";
    statusMessage.style.color = "#856404";
    statusMessage.style.borderLeft = "5px solid #ffc107";
    statusMessage.style.display = "block";
    statusMessage.style.whiteSpace = "pre-line";
    
    console.warn(warningMsg);
  }

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
  
  const jsonData = JSON.stringify(schedulerState);
  console.log(`Sending data to server (${jsonData.length} bytes):`);
  console.log(jsonData.substring(0, 200) + '...'); // Show just the first 200 chars
   
  try {
    const response = await fetch("/api/scheduler/save", {
       method: "POST",
       headers: { "Content-Type": "application/json" },
       body: jsonData
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
    
    // Show success message
    statusMessage.textContent = "Schedule saved successfully!";
    statusMessage.style.backgroundColor = "#d4edda";
    statusMessage.style.color = "#155724";
    statusMessage.style.borderLeft = "5px solid #28a745";
    statusMessage.style.display = "block";
    
    // Reset button state
    if (saveButton) {
      saveButton.textContent = "Save Schedule";
      saveButton.disabled = false;
    }
    
    // Hide success message after 5 seconds
    setTimeout(() => {
      statusMessage.style.display = "none";
    }, 5000);
    
    return true;
  } catch (error) {
    console.error("Error saving scheduler state:", error);
    
    // Show error message
    statusMessage.textContent = "Failed to save schedule: " + error.message;
    statusMessage.style.backgroundColor = "#f8d7da";
    statusMessage.style.color = "#721c24";
    statusMessage.style.borderLeft = "5px solid #dc3545";
    statusMessage.style.display = "block";
    
    // Reset button state
    if (saveButton) {
      saveButton.textContent = "Save Schedule";
      saveButton.disabled = false;
    }
    
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