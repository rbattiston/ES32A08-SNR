/**
 * Core UI operations for the scheduler
 */
import { debugPrintln } from '../constants';
import { 
  schedulerState, 
  SchedulerMode,
  loadSchedulerState,
  updateActiveScheduleFromUI,
  checkPendingSchedule,
  initWebSocket,
  updateUIForMode,
  saveSchedulerState,
  cancelEditCreateMode,
  startCreateMode
} from '../state';
import { renderEventList, renderTimeline, initModalListeners, handleAddEvent } from './events';
import { renderActiveSchedules, populateScheduleDropdown, handleScheduleChange } from './active-schedules';

// Initialize UI references and set up event listeners
export function initUI() {
  debugPrintln("Initializing UI references and listeners");
  
  // Schedule dropdown
  const scheduleDropdown = document.getElementById("schedule-dropdown");
  if (scheduleDropdown) {
    scheduleDropdown.addEventListener("change", handleScheduleChange);
  }
  
  // Add event button
  const addEventButton = document.getElementById("add-event");
  if (addEventButton) {
    addEventButton.addEventListener("click", handleAddEvent);
  }
  
  // Create new schedule button
  const newScheduleButton = document.getElementById("new-schedule");
  if (newScheduleButton) {
    newScheduleButton.addEventListener("click", startCreateMode);
  }
  
  // Save button
  const saveButton = document.getElementById("save-schedule");
  if (saveButton) {
    saveButton.addEventListener("click", saveSchedulerState);
  }
  
  // Cancel button
  const cancelButton = document.getElementById("cancel-schedule");
  if (cancelButton) {
    cancelButton.addEventListener("click", cancelEditCreateMode);
  }
  
  // Activate scheduler button
  const activateButton = document.getElementById("activate-scheduler");
  if (activateButton) {
    activateButton.addEventListener("click", activateScheduler);
  }
  
  // Deactivate scheduler button
  const deactivateButton = document.getElementById("deactivate-scheduler");
  if (deactivateButton) {
    deactivateButton.addEventListener("click", deactivateScheduler);
  }
  
  // Schedule name input (validate uniqueness on change)
  const scheduleNameInput = document.getElementById("schedule-name");
  if (scheduleNameInput) {
    scheduleNameInput.addEventListener("change", validateScheduleName);
  }
  
  // Time inputs (validate and store on change)
  const lightsOnInput = document.getElementById("lights-on-time");
  const lightsOffInput = document.getElementById("lights-off-time");
  
  if (lightsOnInput) {
    lightsOnInput.addEventListener("change", handleLightsTimeChange);
  }
  
  if (lightsOffInput) {
    lightsOffInput.addEventListener("change", handleLightsTimeChange);
  }
  
  // Relay checkboxes (validate and store on change)
  const relayContainer = document.getElementById("relay-mask");
  if (relayContainer) {
    const checkboxes = relayContainer.querySelectorAll("input[type=checkbox]");
    checkboxes.forEach(checkbox => {
      checkbox.addEventListener("change", handleRelayCheckboxChange);
    });
  }
  
  // Initialize modal listeners
  initModalListeners();
  
  // Listen for beforeunload to warn about unsaved changes
  window.addEventListener("beforeunload", (event) => {
    if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
      const message = "You have unsaved schedule changes. Are you sure you want to leave?";
      event.returnValue = message;
      return message;
    }
  });
  
  // Listen for schedule selection events
  document.addEventListener('scheduleSelected', handleScheduleSelection);
}

// Initialize the scheduler application
export async function initScheduler() {
  debugPrintln("Initializing scheduler application");
  
  // Initialize WebSocket connection
  initWebSocket();
  
  // Load scheduler state
  const success = await loadSchedulerState();
  
  if (success) {
    // Check for pending schedule
    const hasPendingSchedule = checkPendingSchedule();
    
    // Update UI based on mode
    updateUIForMode();
    
    // Check scheduler status
    await updateSchedulerStatus();
  } else {
    debugPrintln("Failed to load scheduler state");
  }
}

// Update UI when a schedule is selected
function handleScheduleSelection(event) {
  debugPrintln("Schedule selected event received");
  
  if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
    debugPrintln("Cannot change selection in edit/create mode");
    return;
  }
  
  // Update UI for the selected schedule
  updateUIForMode();
}

// Validate schedule name (must be unique)
function validateScheduleName(event) {
  const newName = event.target.value.trim();
  
  // Empty name check
  if (!newName) {
    alert("Schedule name cannot be empty");
    event.target.value = schedulerState.pendingSchedule.name || "New Schedule";
    return;
  }
  
  // Check for duplicates (only in create mode or if name has changed in edit mode)
  if (schedulerState.mode === SchedulerMode.CREATING ||
      (schedulerState.mode === SchedulerMode.EDITING && 
       newName !== schedulerState.schedules[schedulerState.currentScheduleIndex].name)) {
    
    const isDuplicate = schedulerState.schedules.some(s => s.name === newName);
    
    if (isDuplicate) {
      alert(`A schedule with the name "${newName}" already exists. Please choose a different name.`);
      // Restore the original name
      if (schedulerState.mode === SchedulerMode.CREATING) {
        event.target.value = schedulerState.pendingSchedule.name || "New Schedule";
      } else {
        event.target.value = schedulerState.schedules[schedulerState.currentScheduleIndex].name;
      }
      return;
    }
  }
  
  // Update the pending schedule
  if (schedulerState.pendingSchedule) {
    schedulerState.pendingSchedule.name = newName;
    updateActiveScheduleFromUI();
  }
}

// Handle lights on/off time changes
function handleLightsTimeChange(event) {
  // Update the pending schedule
  updateActiveScheduleFromUI();
  
  // Update timeline visualization
  renderTimeline();
}

// Handle relay checkbox changes
function handleRelayCheckboxChange(event) {
  // Update the pending schedule
  updateActiveScheduleFromUI();
}

// Activate the scheduler
async function activateScheduler() {
  debugPrintln("Activating scheduler");
  
  try {
    const response = await fetch("/api/scheduler/activate", { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    await updateSchedulerStatus();
    alert("Scheduler activated");
  } catch (error) {
    console.error("Error activating scheduler:", error);
    alert("Failed to activate scheduler: " + error.message);
  }
}

// Deactivate the scheduler
async function deactivateScheduler() {
  debugPrintln("Deactivating scheduler");
  
  try {
    const response = await fetch("/api/scheduler/deactivate", { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    await updateSchedulerStatus();
    alert("Scheduler deactivated");
  } catch (error) {
    console.error("Error deactivating scheduler:", error);
    alert("Failed to deactivate scheduler: " + error.message);
  }
}

// Get the scheduler status and update UI
export async function updateSchedulerStatus() {
  debugPrintln("Updating scheduler status");
  
  try {
    const response = await fetch("/api/scheduler/status");
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    updateSchedulerStatusUI(data.isActive);
    
    return data;
  } catch (error) {
    console.error("Error getting scheduler status:", error);
    return { isActive: false };
  }
}

// Update scheduler status UI elements
function updateSchedulerStatusUI(isActive) {
  const activateButton = document.getElementById("activate-scheduler");
  const deactivateButton = document.getElementById("deactivate-scheduler");
  
  if (activateButton && deactivateButton) {
    if (isActive) {
      activateButton.disabled = true;
      deactivateButton.disabled = false;
      activateButton.classList.add("disabled");
      deactivateButton.classList.remove("disabled");
    } else {
      activateButton.disabled = false;
      deactivateButton.disabled = true;
      activateButton.classList.remove("disabled");
      deactivateButton.classList.add("disabled");
    }
  }
}

// Update all UI components that show schedule information
export function updateAllScheduleViews() {
  renderActiveSchedules();
  populateScheduleDropdown();
  renderEventList();
  renderTimeline();
}