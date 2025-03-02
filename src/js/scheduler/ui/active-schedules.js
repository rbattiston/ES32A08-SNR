/**
 * Active schedules UI for the scheduler
 */
import { debugPrintln } from '../constants';
import { schedulerState } from '../state-model';

// Render the active schedules section
export function renderActiveSchedules() {
  debugPrintln("Rendering active schedules section");
  
  // Get container directly from DOM instead of relying on module variable
  const activeSchedulesContainer = document.getElementById("active-schedules-container");
  
  if (!activeSchedulesContainer) {
    debugPrintln("Active schedules container not found");
    return;
  }
  
  // Clear the container
  activeSchedulesContainer.innerHTML = "";
  
  // Check if there are any schedules
  if (schedulerState.scheduleCount === 0) {
    activeSchedulesContainer.innerHTML = "<p>No schedules available</p>";
    return;
  }
  
  // Count active schedules
  let activeCount = 0;
  
  // Render each active schedule
  for (let i = 0; i < schedulerState.scheduleCount; i++) {
    const schedule = schedulerState.schedules[i];
    
    // Only show schedules with assigned relays
    if (schedule.relayMask === 0) {
      continue;
    }
    
    activeCount++;
    
    // Create schedule box
    const scheduleBox = document.createElement("div");
    scheduleBox.className = "active-schedule-box";
    
    // Get list of relays this schedule controls
    const relaysList = [];
    for (let r = 0; r < 8; r++) {
      if (schedule.relayMask & (1 << r)) {
        relaysList.push(r + 1);
      }
    }
    
    // Format events list
    let eventsHTML = "";
    if (schedule.events && schedule.events.length > 0) {
      const eventItems = schedule.events.map(event => {
        const duration = event.duration >= 60 ? 
          `${Math.floor(event.duration / 60)}m ${event.duration % 60}s` : 
          `${event.duration}s`;
        return `<li>${event.time} - ${duration}</li>`;
      }).join("");
      eventsHTML = `<ul class="schedule-events-list">${eventItems}</ul>`;
    } else {
      eventsHTML = "<p>No events defined</p>";
    }
    
    // Populate schedule box
    scheduleBox.innerHTML = `
      <h3>${schedule.name}</h3>
      <div class="schedule-metadata">
        <div>On: ${schedule.lightsOnTime}, Off: ${schedule.lightsOffTime}</div>
        <div>Relays: ${relaysList.join(", ")}</div>
      </div>
      <div class="schedule-events">
        <h4>Events</h4>
        ${eventsHTML}
      </div>
      <div class="schedule-controls">
        <button class="btn-secondary edit-schedule" data-index="${i}">Edit</button>
      </div>
    `;
    
    // Add event listener for edit button
    const editButton = scheduleBox.querySelector(".edit-schedule");
    editButton.addEventListener("click", () => {
      schedulerState.currentScheduleIndex = i;
      
      // Update dropdown selection
      const scheduleDropdown = document.getElementById("schedule-dropdown");
      if (scheduleDropdown) {
        scheduleDropdown.value = i;
      }
      
      // Load the selected schedule to the UI
      const event = new CustomEvent('scheduleSelected', { detail: { index: i } });
      document.dispatchEvent(event);
    });
    
    // Add to container
    activeSchedulesContainer.appendChild(scheduleBox);
  }
  
  // If no active schedules
  if (activeCount === 0) {
    activeSchedulesContainer.innerHTML = "<p>No active schedules. Create a schedule and assign relays to make it active.</p>";
  }
}

// Populate the schedule dropdown
export function populateScheduleDropdown() {
  debugPrintln("Populating schedule dropdown");
  
  const dropdown = document.getElementById("schedule-dropdown");
  if (!dropdown) {
    debugPrintln("Schedule dropdown not found");
    return;
  }
  
  // Clear existing options
  dropdown.innerHTML = "";
  
  // Check if there are any schedules
  if (schedulerState.scheduleCount === 0) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "No schedules available";
    option.disabled = true;
    option.selected = true;
    dropdown.appendChild(option);
    return;
  }
  
  // Add each schedule as an option
  for (let i = 0; i < schedulerState.scheduleCount; i++) {
    const schedule = schedulerState.schedules[i];
    const option = document.createElement("option");
    option.value = i;
    option.textContent = schedule.name;
    
    // Mark active schedules
    if (schedule.relayMask > 0) {
      option.textContent += " (Active)";
    }
    
    dropdown.appendChild(option);
  }
  
  // Set current selection
  dropdown.value = schedulerState.currentScheduleIndex;
}

// Handle schedule change from dropdown
export function handleScheduleChange(event) {
  const selectedIndex = parseInt(event.target.value);
  
  debugPrintln(`Schedule changed to index ${selectedIndex}`);
  
  if (schedulerState.mode !== "view-only") {
    debugPrintln("Cannot change schedule in edit/create mode");
    // Revert to previous selection
    event.target.value = schedulerState.currentScheduleIndex;
    return;
  }
  
  // Update current schedule index
  schedulerState.currentScheduleIndex = selectedIndex;
  
  // Trigger schedule selection event
  const selectionEvent = new CustomEvent('scheduleSelected', { detail: { index: selectedIndex } });
  document.dispatchEvent(selectionEvent);
}