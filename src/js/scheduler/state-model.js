/**
 * State management for the scheduler
 */
import { MAX_EVENTS, MAX_SCHEDULES, debugPrintln } from './constants';
import { renderEventList, renderTimeline } from './ui/events';
import { renderActiveSchedules } from './ui/active-schedules';

// Scheduler mode enum
export const SchedulerMode = {
  VIEW_ONLY: 'view-only',
  CREATING: 'creating',
  EDITING: 'editing'
};

// Global scheduler state model
export const schedulerState = {
  scheduleCount: 0,
  currentScheduleIndex: 0,
  schedules: [],
  mode: SchedulerMode.VIEW_ONLY,
  pendingSchedule: null // Used for temporary storage during creation/editing
};

// Check if there's a pending schedule in localStorage
export function checkPendingSchedule() {
  debugPrintln("Checking for pending schedule in localStorage");
  
  try {
    const pendingData = localStorage.getItem('pendingSchedule');
    const pendingMode = localStorage.getItem('schedulerMode');
    
    if (pendingData && pendingMode) {
      debugPrintln("Found pending schedule data");
      
      // Parse the data
      const pendingSchedule = JSON.parse(pendingData);
      
      // Confirm with user
      const resume = confirm("You have unsaved changes from your last session. Resume editing or discard?");
      
      if (resume) {
        // Restore the pending schedule
        schedulerState.pendingSchedule = pendingSchedule;
        schedulerState.mode = pendingMode;
        
        if (pendingMode === SchedulerMode.EDITING) {
          // Find the schedule being edited
          const scheduleIndex = schedulerState.schedules.findIndex(s => s.name === pendingSchedule.name);
          if (scheduleIndex >= 0) {
            schedulerState.currentScheduleIndex = scheduleIndex;
          }
        }
        
        // Send a WebSocket message to inform server
        sendWebSocketMessage({
          action: pendingMode === SchedulerMode.CREATING ? "startCreating" : "startEditing",
          scheduleId: pendingMode === SchedulerMode.EDITING ? pendingSchedule.name : null
        });
        
        return true;
      } else {
        // Discard pending data
        clearPendingSchedule();
      }
    }
  } catch (error) {
    debugPrintln("Error checking pending schedule: " + error.message);
    clearPendingSchedule();
  }
  
  return false;
}

// Clear pending schedule data
export function clearPendingSchedule() {
  localStorage.removeItem('pendingSchedule');
  localStorage.removeItem('schedulerMode');
  schedulerState.pendingSchedule = null;
}

// Store pending schedule in localStorage
export function storePendingSchedule() {
  if (schedulerState.mode !== SchedulerMode.VIEW_ONLY && schedulerState.pendingSchedule) {
    localStorage.setItem('pendingSchedule', JSON.stringify(schedulerState.pendingSchedule));
    localStorage.setItem('schedulerMode', schedulerState.mode);
    
    // Also send to server via WebSocket
    sendWebSocketMessage({
      action: "updatePending",
      data: schedulerState.pendingSchedule
    });
  }
}

// Initialize WebSocket connection
let websocket = null;

export function initWebSocket() {
  const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${wsProtocol}//${window.location.host}/ws/scheduler`;
  
  websocket = new WebSocket(wsUrl);
  
  websocket.onopen = function(evt) {
    debugPrintln("WebSocket connection established");
  };
  
  websocket.onclose = function(evt) {
    debugPrintln("WebSocket connection closed");
    // Try to reconnect after a delay
    setTimeout(initWebSocket, 2000);
  };
  
  websocket.onmessage = function(evt) {
    const message = JSON.parse(evt.data);
    handleWebSocketMessage(message);
  };
  
  websocket.onerror = function(evt) {
    debugPrintln("WebSocket error: " + evt.data);
  };
}

// Send a message via WebSocket
export function sendWebSocketMessage(message) {
  if (websocket && websocket.readyState === WebSocket.OPEN) {
    websocket.send(JSON.stringify(message));
  } else {
    debugPrintln("WebSocket not connected, cannot send message");
  }
}

// Handle incoming WebSocket messages
function handleWebSocketMessage(message) {
  switch (message.type) {
    case "pendingSchedule":
      if (message.isPending && message.data) {
        debugPrintln("Received pending schedule from server");
        schedulerState.pendingSchedule = message.data;
      }
      break;
    
    case "scheduleUpdate":
      debugPrintln("Received schedule update from server");
      loadSchedulerState();
      break;
      
    default:
      debugPrintln("Unknown message type: " + message.type);
  }
}

// Update the active schedule from UI values
export function updateActiveScheduleFromUI() {
  debugPrintln("Updating active schedule from UI");
  
  if (schedulerState.mode === SchedulerMode.VIEW_ONLY) {
    debugPrintln("Cannot update in view-only mode");
    return;
  }
  
  const scheduleNameInput = document.getElementById("schedule-name");
  const lightsOnInput = document.getElementById("lights-on-time");
  const lightsOffInput = document.getElementById("lights-off-time");
  const relayMaskContainer = document.getElementById("relay-mask");
  
  if (!scheduleNameInput || !lightsOnInput || !lightsOffInput || !relayMaskContainer) {
    debugPrintln("Missing UI elements for update");
    return;
  }
  
  // Get the schedule to update (either pending or current)
  const schedule = schedulerState.pendingSchedule || 
                  (schedulerState.scheduleCount > 0 ? 
                   schedulerState.schedules[schedulerState.currentScheduleIndex] : null);
  
  if (!schedule) {
    debugPrintln("No schedule to update");
    return;
  }
  
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
  
  // Store the updated schedule
  if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
    schedulerState.pendingSchedule = schedule;
    storePendingSchedule();
  }
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

// Switch to create mode
export function startCreateMode() {
  debugPrintln("Switching to create mode");
  
  // Create a new empty schedule
  schedulerState.pendingSchedule = {
    name: `New Schedule ${new Date().toLocaleString()}`,
    metadata: new Date().toISOString(),
    relayMask: 0,
    lightsOnTime: "06:00",
    lightsOffTime: "18:00",
    eventCount: 0,
    events: []
  };
  
  // Update mode
  schedulerState.mode = SchedulerMode.CREATING;
  
  // Store pending data
  storePendingSchedule();
  
  // Send WebSocket message
  sendWebSocketMessage({ action: "startCreating" });
  
  // Update UI
  updateUIForMode();
}

// Switch to edit mode for the selected schedule
export function startEditMode(scheduleIndex) {
  debugPrintln(`Switching to edit mode for schedule ${scheduleIndex}`);
  
  if (scheduleIndex >= 0 && scheduleIndex < schedulerState.scheduleCount) {
    // Create a copy of the selected schedule
    schedulerState.currentScheduleIndex = scheduleIndex;
    schedulerState.pendingSchedule = JSON.parse(JSON.stringify(schedulerState.schedules[scheduleIndex]));
    
    // Update mode
    schedulerState.mode = SchedulerMode.EDITING;
    
    // Store pending data
    storePendingSchedule();
    
    // Send WebSocket message
    sendWebSocketMessage({ 
      action: "startEditing", 
      scheduleId: schedulerState.pendingSchedule.name 
    });
    
    // Update UI
    updateUIForMode();
  } else {
    debugPrintln("Invalid schedule index for edit mode");
  }
}

// Switch back to view-only mode
export function cancelEditCreateMode() {
  debugPrintln("Canceling edit/create mode");
  
  // Clear pending schedule
  clearPendingSchedule();
  schedulerState.pendingSchedule = null;
  
  // Update mode
  schedulerState.mode = SchedulerMode.VIEW_ONLY;
  
  // Update UI
  updateUIForMode();
}

// Update UI elements based on current mode
export function updateUIForMode() {
  debugPrintln(`Updating UI for mode: ${schedulerState.mode}`);
  
  // Get sections
  const scheduleEditorSection = document.getElementById("schedule-settings");
  const activeSchedulesSection = document.getElementById("active-schedules");
  const scheduleSelectionSection = document.getElementById("schedule-selection");
  const timelineSection = document.getElementById("schedule-visualization");
  const eventsSection = document.getElementById("events");
  const controlsSection = document.getElementById("scheduler-controls");
  
  // Get buttons
  const saveButton = document.getElementById("save-schedule");
  const cancelButton = document.getElementById("cancel-schedule");
  const createButton = document.getElementById("new-schedule");
  
  switch (schedulerState.mode) {
    case SchedulerMode.CREATING:
    case SchedulerMode.EDITING:
      // Enable editor, disable other sections
      scheduleEditorSection.classList.remove("disabled");
      activeSchedulesSection.classList.add("disabled");
      scheduleSelectionSection.classList.add("disabled");
      
      // Show save/cancel buttons, hide create button
      if (saveButton) saveButton.style.display = "inline-block";
      if (cancelButton) cancelButton.style.display = "inline-block";
      if (createButton) createButton.style.display = "none";
      
      // Load pending schedule data to UI
      loadPendingScheduleToUI();
      break;
      
    case SchedulerMode.VIEW_ONLY:
    default:
      // Disable editor, enable other sections
      scheduleEditorSection.classList.add("disabled");
      activeSchedulesSection.classList.remove("disabled");
      scheduleSelectionSection.classList.remove("disabled");
      
      // Hide save/cancel buttons, show create button
      if (saveButton) saveButton.style.display = "none";
      if (cancelButton) cancelButton.style.display = "none";
      if (createButton) createButton.style.display = "inline-block";
      
      // Load active schedule to UI
      loadActiveScheduleToUI();
      break;
  }
  
  // Update event display and timeline
  renderEventList();
  renderTimeline();
  renderActiveSchedules();
}

// Load pending schedule data to UI
function loadPendingScheduleToUI() {
  if (!schedulerState.pendingSchedule) return;
  
  const scheduleNameInput = document.getElementById("schedule-name");
  const lightsOnInput = document.getElementById("lights-on-time");
  const lightsOffInput = document.getElementById("lights-off-time");
  const relayMaskContainer = document.getElementById("relay-mask");
  
  if (scheduleNameInput) scheduleNameInput.value = schedulerState.pendingSchedule.name;
  if (lightsOnInput) lightsOnInput.value = schedulerState.pendingSchedule.lightsOnTime;
  if (lightsOffInput) lightsOffInput.value = schedulerState.pendingSchedule.lightsOffTime;
  
  // Update relay checkboxes
  if (relayMaskContainer) {
    const checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
    checkboxes.forEach((checkbox, index) => {
      checkbox.checked = (schedulerState.pendingSchedule.relayMask & (1 << index)) !== 0;
    });
  }
}

// Load active schedule to UI
function loadActiveScheduleToUI() {
  if (schedulerState.scheduleCount === 0) {
    debugPrintln("No schedules available to load");
    return;
  }
  
  const scheduleNameInput = document.getElementById("schedule-name");
  const lightsOnInput = document.getElementById("lights-on-time");
  const lightsOffInput = document.getElementById("lights-off-time");
  const relayMaskContainer = document.getElementById("relay-mask");
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  
  if (scheduleNameInput) scheduleNameInput.value = schedule.name;
  if (lightsOnInput) lightsOnInput.value = schedule.lightsOnTime;
  if (lightsOffInput) lightsOffInput.value = schedule.lightsOffTime;
  
  // Update relay checkboxes
  if (relayMaskContainer) {
    const checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
    checkboxes.forEach((checkbox, index) => {
      checkbox.checked = (schedule.relayMask & (1 << index)) !== 0;
    });
  }
}

// Save scheduler state to the server
export async function saveSchedulerState() {
  debugPrintln("Saving scheduler state to server");
  
  // Ensure we have the latest UI values
  updateActiveScheduleFromUI();
  
  if (schedulerState.mode === SchedulerMode.CREATING) {
    // Adding a new schedule
    if (schedulerState.pendingSchedule) {
      schedulerState.schedules.push(schedulerState.pendingSchedule);
      schedulerState.scheduleCount = schedulerState.schedules.length;
      schedulerState.currentScheduleIndex = schedulerState.scheduleCount - 1;
    }
  } else if (schedulerState.mode === SchedulerMode.EDITING) {
    // Updating existing schedule
    if (schedulerState.pendingSchedule && schedulerState.currentScheduleIndex >= 0) {
      schedulerState.schedules[schedulerState.currentScheduleIndex] = schedulerState.pendingSchedule;
    }
  }
  
  // Ensure consistent events array and eventCount for all schedules
  for (let i = 0; i < schedulerState.scheduleCount; i++) {
    const schedule = schedulerState.schedules[i];
    
    // Initialize events array if needed
    if (!schedule.events) {
      schedule.events = [];
    }
    
    // Update eventCount to match events array length
    schedule.eventCount = schedule.events.length;
  }
  
  // Serialize and send the data
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
    
    // Clear pending state and return to view mode
    clearPendingSchedule();
    schedulerState.pendingSchedule = null;
    schedulerState.mode = SchedulerMode.VIEW_ONLY;
    
    // Update UI
    updateUIForMode();
    
    alert("Schedule saved successfully");
    return true;
  } catch (error) {
    console.error("Error saving scheduler state:", error);
    alert("Failed to save schedule: " + error.message);
    return false;
  }
}

// Delete a schedule
export async function deleteSchedule(scheduleIndex) {
  debugPrintln(`Deleting schedule at index ${scheduleIndex}`);
  
  if (scheduleIndex < 0 || scheduleIndex >= schedulerState.scheduleCount) {
    debugPrintln("Invalid schedule index for deletion");
    return false;
  }
  
  if (confirm(`Are you sure you want to delete schedule "${schedulerState.schedules[scheduleIndex].name}"?`)) {
    // Remove the schedule
    schedulerState.schedules.splice(scheduleIndex, 1);
    schedulerState.scheduleCount = schedulerState.schedules.length;
    
    // Adjust current index if needed
    if (schedulerState.currentScheduleIndex >= schedulerState.scheduleCount) {
      schedulerState.currentScheduleIndex = Math.max(0, schedulerState.scheduleCount - 1);
    }
    
    // Save changes
    try {
      const result = await saveSchedulerState();
      
      if (result) {
        alert("Schedule deleted successfully");
        return true;
      }
    } catch (error) {
      console.error("Error deleting schedule:", error);
      alert("Failed to delete schedule: " + error.message);
    }
  }
  
  return false;
}

// Add a new event to the current schedule
export function addEvent(eventTime, duration, repeatCount, repeatInterval) {
  debugPrintln("Adding new event");
  
  const currentSchedule = schedulerState.pendingSchedule || 
                         (schedulerState.scheduleCount > 0 ? 
                          schedulerState.schedules[schedulerState.currentScheduleIndex] : null);
  
  if (!currentSchedule) {
    alert("Please create a schedule first");
    return;
  }
  
  // Initialize events array if needed
  if (!currentSchedule.events) {
    currentSchedule.events = [];
  }
  
  // Parse time
  const [baseHour, baseMinute] = eventTime.split(":").map(Number);
  let baseMinutes = baseHour * 60 + baseMinute;
  
  // Calculate total events we'll be adding
  const totalEventsToAdd = repeatCount + 1;
  
  // Check if adding these events would exceed the maximum
  if (currentSchedule.events.length + totalEventsToAdd > MAX_EVENTS) {
    alert(`Cannot add ${totalEventsToAdd} events. Would exceed maximum of ${MAX_EVENTS} events.`);
    return;
  }
  
  // Create events for each occurrence
  let eventsAdded = 0;
  for (let i = 0; i <= repeatCount; i++) {
    // Calculate time for this occurrence
    const occurrenceMinutes = baseMinutes + (i * repeatInterval);
    
    // Skip if beyond 24 hours
    if (occurrenceMinutes >= 1440) {
      break;
    }
    
    const occurrenceHour = Math.floor(occurrenceMinutes / 60);
    const occurrenceMinute = occurrenceMinutes % 60;
    const occurrenceTime = `${occurrenceHour.toString().padStart(2, "0")}:${occurrenceMinute.toString().padStart(2, "0")}`;
    
    // Create new event
    const newEvent = {
      id: Date.now().toString() + "_" + i,
      time: occurrenceTime,
      duration: duration,
      executedMask: 0
    };
    
    // Add to schedule
    currentSchedule.events.push(newEvent);
    eventsAdded++;
  }
  
  // Update event count
  currentSchedule.eventCount = currentSchedule.events.length;
  
  debugPrintln(`Added ${eventsAdded} events. New count: ${currentSchedule.eventCount}`);
  
  // Update UI
  renderEventList();
  renderTimeline();
  
  // If in edit/create mode, update pending schedule
  if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
    schedulerState.pendingSchedule = currentSchedule;
    storePendingSchedule();
  }
  
  return eventsAdded;
}

// Delete an event
export function deleteEvent(index) {
  debugPrintln(`Deleting event at index ${index}`);
  
  const currentSchedule = schedulerState.pendingSchedule || 
                         (schedulerState.scheduleCount > 0 ? 
                          schedulerState.schedules[schedulerState.currentScheduleIndex] : null);
  
  if (!currentSchedule) {
    return;
  }
  
  // Initialize events array if needed
  if (!currentSchedule.events) {
    currentSchedule.events = [];
    currentSchedule.eventCount = 0;
    return;
  }
  
  if (index < 0 || index >= currentSchedule.events.length) {
    debugPrintln(`Invalid event index: ${index}`);
    return;
  }
  
  // Remove the event
  currentSchedule.events.splice(index, 1);
  currentSchedule.eventCount = currentSchedule.events.length;
  
  debugPrintln(`Event deleted. New count: ${currentSchedule.eventCount}`);
  
  // Update UI
  renderEventList();
  renderTimeline();
  
  // If in edit/create mode, update pending schedule
  if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
    schedulerState.pendingSchedule = currentSchedule;
    storePendingSchedule();
  }
}