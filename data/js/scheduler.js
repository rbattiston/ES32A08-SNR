// Render active schedules section
function renderActiveSchedules() {
  debugPrintln("Rendering active schedules section");
  
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
      scheduleDropdown.value = i;
      loadActiveSchedule();
    });
    
    // Add to container
    activeSchedulesContainer.appendChild(scheduleBox);
  }
  
  // If no active schedules
  if (activeCount === 0) {
    activeSchedulesContainer.innerHTML = "<p>No active schedules. Create a schedule and assign relays to make it active.</p>";
  }
}

// Update all UI components that show schedule information
function updateAllScheduleViews() {
  renderActiveSchedules();
  populateScheduleDropdown();
  loadActiveSchedule();
}  activeSchedulesContainer = document.getElementById("active-schedules-container");let activeSchedulesContainer;

// UI element references"use strict";

/**
 * Irrigation Scheduler JavaScript
 * 
 * This file handles the frontend functionality for the ESP32-based irrigation scheduler.
 * It allows users to create, edit, save, and activate schedules to control irrigation valves.
 */

// Global constants for scheduler limits
const MAX_SCHEDULES = 8;
const MAX_EVENTS = 50;

// Global scheduler state model
let schedulerState = {
  scheduleCount: 0,
  currentScheduleIndex: 0,
  schedules: []
};

// Debug helper functions
function debugPrintln(msg) { console.log("[DEBUG] " + msg); }
function debugPrintf(fmt, ...args) { console.log("[DEBUG] " + fmt, ...args); }

// UI element references
let scheduleDropdown, scheduleNameInput, relayMaskContainer, lightsOnInput, lightsOffInput;
let eventList, timelineContainer;
let addEventButton, saveScheduleButton, activateSchedulerButton, deactivateSchedulerButton, newScheduleButton;
let eventTimeInput, eventDurationInput, eventRepeatInput, eventRepeatIntervalInput;

// Initialize UI references
function initUI() {
  debugPrintln("Initializing UI references");
  
  scheduleDropdown = document.getElementById("schedule-dropdown");
  scheduleNameInput = document.getElementById("schedule-name");
  relayMaskContainer = document.getElementById("relay-mask");
  lightsOnInput = document.getElementById("lights-on-time");
  lightsOffInput = document.getElementById("lights-off-time");
  eventList = document.getElementById("event-list");
  timelineContainer = document.getElementById("timeline-container");
  
  addEventButton = document.getElementById("add-event");
  saveScheduleButton = document.getElementById("save-schedule");
  activateSchedulerButton = document.getElementById("activate-scheduler");
  deactivateSchedulerButton = document.getElementById("deactivate-scheduler");
  newScheduleButton = document.getElementById("new-schedule");
  
  eventTimeInput = document.getElementById("event-time");
  eventDurationInput = document.getElementById("event-duration");
  eventRepeatInput = document.getElementById("event-repeat");
  eventRepeatIntervalInput = document.getElementById("event-repeat-interval");
}

// Load scheduler state from the server
async function loadSchedulerState() {
  debugPrintln("Loading scheduler state from server");
  
  try {
    const response = await fetch("/api/scheduler/load");
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    schedulerState = await response.json();
    debugPrintln(`Loaded ${schedulerState.scheduleCount} schedules`);
    
    populateScheduleDropdown();
    loadActiveSchedule();
    
    return true;
  } catch (error) {
    console.error("Error loading scheduler state:", error);
    return false;
  }
}

// Save scheduler state to the server
async function saveSchedulerState() {
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
    
    alert("Schedule saved successfully");
    return true;
  } catch (error) {
    console.error("Error saving scheduler state:", error);
    alert("Failed to save schedule: " + error.message);
    return false;
  }
}

// Populate schedule dropdown with available schedules
function populateScheduleDropdown() {
  debugPrintln("Populating schedule dropdown");
  
  scheduleDropdown.innerHTML = "";
  
  for (let i = 0; i < schedulerState.scheduleCount; i++) {
    const opt = document.createElement("option");
    opt.value = i;
    opt.textContent = schedulerState.schedules[i].name;
    
    // Add active/inactive indicator
    const relayMask = schedulerState.schedules[i].relayMask;
    if (relayMask > 0) {
      opt.textContent += " (Active)";
    } else {
      opt.textContent += " (Inactive)";
    }
    
    scheduleDropdown.appendChild(opt);
  }
  
  // Set current selection
  scheduleDropdown.value = schedulerState.currentScheduleIndex;
}

// Load the active schedule into the UI
function loadActiveSchedule() {
  debugPrintln("Loading active schedule to UI");
  
  if (schedulerState.scheduleCount === 0) {
    debugPrintln("No schedules available");
    return;
  }
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  
  // Debug logging
  debugPrintln(`Loading schedule: ${schedule.name}`);
  debugPrintln(`Event count: ${schedule.eventCount}`);
  if (schedule.events) {
    debugPrintln(`Events array length: ${schedule.events.length}`);
  } else {
    debugPrintln("Events array is undefined");
    schedule.events = [];
    schedule.eventCount = 0;
  }
  
  // Update basic info
  scheduleNameInput.value = schedule.name;
  lightsOnInput.value = schedule.lightsOnTime;
  lightsOffInput.value = schedule.lightsOffTime;
  
  // Update relay checkboxes
  const checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
  checkboxes.forEach((checkbox, index) => {
    checkbox.checked = (schedule.relayMask & (1 << index)) !== 0;
  });
  
  // Render events and timeline
  renderEventList();
  renderTimeline();
  updateScheduleStatus();
}

// Update the active schedule from UI values
function updateActiveScheduleFromUI() {
  debugPrintln("Updating active schedule from UI");
  
  if (schedulerState.scheduleCount === 0) {
    debugPrintln("No schedules to update");
    return;
  }
  
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

// Render the event list for the active schedule
function renderEventList() {
  debugPrintln("Rendering event list");
  
  if (schedulerState.scheduleCount === 0) {
    eventList.innerHTML = "<p>No schedules available</p>";
    return;
  }
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  eventList.innerHTML = "";
  
  // Initialize events array if it doesn't exist
  if (!schedule.events) {
    schedule.events = [];
    schedule.eventCount = 0;
  }
  
  if (schedule.eventCount === 0 || schedule.events.length === 0) {
    eventList.innerHTML = "<p>No events in this schedule</p>";
    return;
  }
  
  debugPrintln(`Rendering ${schedule.events.length} events`);
  
  // Render each event
  schedule.events.forEach((event, index) => {
    const eventItem = document.createElement("div");
    eventItem.className = "event-item";
    
    // Format the event description
    const duration = event.duration;
    const durationText = duration >= 60 ? 
                        `${Math.floor(duration / 60)}m ${duration % 60}s` : 
                        `${duration}s`;
    
    eventItem.innerHTML = `
      <span>${event.time} - Duration: ${durationText}</span>
      <button data-index="${index}" class="delete-event">Delete</button>
    `;
    
    // Add click event to edit the event
    eventItem.addEventListener("click", () => openEditModal(index));
    
    // Add event to the list
    eventList.appendChild(eventItem);
  });
  
  // Add event listeners to delete buttons
  const deleteButtons = eventList.querySelectorAll(".delete-event");
  deleteButtons.forEach(button => {
    button.addEventListener("click", (e) => {
      e.stopPropagation();
      const index = parseInt(button.getAttribute("data-index"));
      deleteEvent(index);
    });
  });
}

// Delete an event from the active schedule
function deleteEvent(index) {
  debugPrintln(`Deleting event at index ${index}`);
  
  if (schedulerState.scheduleCount === 0) {
    return;
  }
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  
  // Initialize events array if it doesn't exist
  if (!schedule.events) {
    schedule.events = [];
    schedule.eventCount = 0;
    return;
  }
  
  if (index < 0 || index >= schedule.events.length) {
    debugPrintln(`Invalid event index: ${index}`);
    return;
  }
  
  // Remove the event
  schedule.events.splice(index, 1);
  schedule.eventCount = schedule.events.length;
  
  debugPrintln(`Event deleted. New count: ${schedule.eventCount}`);
  
  // Update UI
  renderEventList();
  renderTimeline();
  renderActiveSchedules();
  
  // Save changes
  saveSchedulerState();
}

// Render the timeline visualization for the active schedule
function renderTimeline() {
  debugPrintln("Rendering timeline");
  
  if (schedulerState.scheduleCount === 0) {
    timelineContainer.innerHTML = "<p>No schedules available</p>";
    return;
  }
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  timelineContainer.innerHTML = "";
  
  // Initialize events array if it doesn't exist
  if (!schedule.events) {
    schedule.events = [];
    schedule.eventCount = 0;
  }
  
  // Create background for lights on/off time
  const bgDiv = document.createElement("div");
  bgDiv.className = "timeline-bg";
  
  // Parse lights on/off times
  let [onHour, onMinute] = schedule.lightsOnTime.split(":").map(Number);
  let [offHour, offMinute] = schedule.lightsOffTime.split(":").map(Number);
  let onMinutes = onHour * 60 + onMinute;
  let offMinutes = offHour * 60 + offMinute;
  
  // Create gradient background for lights on/off times
  if (onMinutes < offMinutes) {
    bgDiv.style.background = `linear-gradient(to right, 
      lightgrey 0%, 
      lightgrey ${(onMinutes/1440)*100}%, 
      lightyellow ${(onMinutes/1440)*100}%, 
      lightyellow ${(offMinutes/1440)*100}%, 
      lightgrey ${(offMinutes/1440)*100}%, 
      lightgrey 100%)`;
  } else {
    // Handle case where "on" time is after "off" time
    bgDiv.style.background = `linear-gradient(to right, 
      lightyellow 0%, 
      lightyellow ${(offMinutes/1440)*100}%, 
      lightgrey ${(offMinutes/1440)*100}%, 
      lightgrey ${(onMinutes/1440)*100}%, 
      lightyellow ${(onMinutes/1440)*100}%, 
      lightyellow 100%)`;
  }
  
  // Position background
  bgDiv.style.position = "absolute";
  bgDiv.style.top = "0";
  bgDiv.style.left = "0";
  bgDiv.style.width = "100%";
  bgDiv.style.height = "100%";
  timelineContainer.appendChild(bgDiv);
  
  // Create timeline bar to show events
  const timelineBar = document.createElement("div");
  timelineBar.className = "timeline-bar";
  timelineBar.style.position = "relative";
  timelineBar.style.height = "100%";
  timelineBar.style.width = "100%";
  timelineBar.style.zIndex = "5";
  
  // Add time markers
  for (let hour = 0; hour < 24; hour += 2) {
    const marker = document.createElement("div");
    marker.className = "tick-mark";
    marker.style.position = "absolute";
    marker.style.top = "5px";
    marker.style.left = `${(hour / 24) * 100}%`;
    marker.textContent = `${hour}:00`;
    timelineBar.appendChild(marker);
  }
  
  // Debug logging
  debugPrintln(`Rendering ${schedule.events.length} events on timeline`);
  
  // Add events to timeline
  if (schedule.events && schedule.events.length > 0) {
    schedule.events.forEach((event, index) => {
      const [hour, minute] = event.time.split(":").map(Number);
      const eventMinutes = hour * 60 + minute;
      const leftPercent = (eventMinutes / 1440) * 100;
      
      // Calculate event width based on duration (max 5% to ensure visibility)
      const widthPercent = Math.max(0.5, Math.min(5, (event.duration / 86400) * 100));
      
      const eventBlock = document.createElement("div");
      eventBlock.className = "timeline-event";
      eventBlock.style.left = `${leftPercent}%`;
      eventBlock.style.width = `${widthPercent}%`;
      eventBlock.title = `Event: ${event.time} - ${event.duration}s`;
      
      // Add click handler to edit event
      eventBlock.addEventListener("click", (e) => {
        e.stopPropagation();
        openEditModal(index);
      });
      
      timelineBar.appendChild(eventBlock);
    });
  }
  
  timelineContainer.appendChild(timelineBar);
  
  // Add current time line
  updateCurrentTimeLine();
}

// Update the current time line on the timeline
function updateCurrentTimeLine() {
  const container = timelineContainer;
  if (!container) return;
  
  // Remove any existing time line
  const existingLine = container.querySelector(".current-time-line");
  if (existingLine) {
    existingLine.remove();
  }
  
  // Create new time line
  const currentLine = document.createElement("div");
  currentLine.className = "current-time-line";
  currentLine.style.position = "absolute";
  currentLine.style.top = "0";
  currentLine.style.bottom = "0";
  currentLine.style.width = "2px";
  currentLine.style.backgroundColor = "red";
  currentLine.style.zIndex = "10";
  
  // Calculate position based on current time
  const now = new Date();
  const currentMinutes = now.getHours() * 60 + now.getMinutes();
  const leftPercent = (currentMinutes / 1440) * 100;
  currentLine.style.left = `${leftPercent}%`;
  
  container.appendChild(currentLine);
}

// Variables for event editing modal
let currentEditingIndex = null;

// Functions for the edit modal
function populateTimeSelects() {
  const hourSelect = document.getElementById("edit-hour");
  const minuteSelect = document.getElementById("edit-minute");
  
  hourSelect.innerHTML = "";
  minuteSelect.innerHTML = "";
  
  // Populate hours (0-23)
  for (let i = 0; i < 24; i++) {
    const option = document.createElement("option");
    option.value = i;
    option.textContent = i.toString().padStart(2, "0");
    hourSelect.appendChild(option);
  }
  
  // Populate minutes (0-59)
  for (let i = 0; i < 60; i++) {
    const option = document.createElement("option");
    option.value = i;
    option.textContent = i.toString().padStart(2, "0");
    minuteSelect.appendChild(option);
  }
}

// Open edit modal for an event
function openEditModal(index) {
  debugPrintln(`Opening edit modal for event ${index}`);
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  const event = schedule.events[index];
  currentEditingIndex = index;
  
  // Populate time selects
  populateTimeSelects();
  
  // Set current values
  const [hourStr, minuteStr] = event.time.split(":");
  document.getElementById("edit-hour").value = parseInt(hourStr);
  document.getElementById("edit-minute").value = parseInt(minuteStr);
  document.getElementById("edit-duration").value = event.duration;
  
  // Show modal
  document.getElementById("edit-modal").style.display = "block";
}

// Close edit modal
function closeEditModal() {
  document.getElementById("edit-modal").style.display = "none";
  currentEditingIndex = null;
}

// Save changes from edit modal
function saveEditModal() {
  debugPrintln("Saving changes from edit modal");
  
  if (currentEditingIndex === null) {
    closeEditModal();
    return;
  }
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  const event = schedule.events[currentEditingIndex];
  
  // Get new values
  const hour = parseInt(document.getElementById("edit-hour").value);
  const minute = parseInt(document.getElementById("edit-minute").value);
  const duration = parseInt(document.getElementById("edit-duration").value);
  
  // Update event
  event.time = `${hour.toString().padStart(2, "0")}:${minute.toString().padStart(2, "0")}`;
  event.duration = duration;
  
  // Update UI
  renderEventList();
  renderTimeline();
  
  // Save changes
  saveSchedulerState();
  
  // Close modal
  closeEditModal();
}

// Add a new event to the active schedule
function addEvent() {
  debugPrintln("Adding new event");
  
  if (schedulerState.scheduleCount === 0) {
    alert("Please create a schedule first");
    return;
  }
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  
  // Debugging - log current events count
  debugPrintln(`Current event count: ${schedule.eventCount}, events array length: ${schedule.events.length}`);
  
  // Get values from form
  const eventTime = eventTimeInput.value;
  const duration = parseInt(eventDurationInput.value);
  const repeatCount = parseInt(eventRepeatInput.value);
  const repeatInterval = parseInt(eventRepeatIntervalInput.value);
  
  // Validate inputs
  if (!eventTime || isNaN(duration) || isNaN(repeatCount) || isNaN(repeatInterval)) {
    alert("Please fill in all fields with valid values");
    return;
  }
  
  if (duration <= 0) {
    alert("Duration must be greater than 0");
    return;
  }
  
  if (repeatCount < 0) {
    alert("Repeat count must be 0 or greater");
    return;
  }
  
  if (repeatInterval <= 0) {
    alert("Repeat interval must be greater than 0");
    return;
  }
  
  // Initialize events array if it doesn't exist
  if (!schedule.events) {
    schedule.events = [];
    schedule.eventCount = 0;
  }
  
  // Parse base time
  const [baseHour, baseMinute] = eventTime.split(":").map(Number);
  let baseMinutes = baseHour * 60 + baseMinute;
  
  // Calculate total events we'll be adding
  const totalEventsToAdd = repeatCount + 1;
  
  // Check if adding these events would exceed the maximum
  if (schedule.eventCount + totalEventsToAdd > MAX_EVENTS) {
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
    schedule.events.push(newEvent);
    eventsAdded++;
  }
  
  // Update event count
  schedule.eventCount = schedule.events.length;
  
  debugPrintln(`Added ${eventsAdded} events. New count: ${schedule.eventCount}`);
  
  // Update UI
  renderEventList();
  renderTimeline();
  
  // Save changes
  saveSchedulerState();
}

// Get the status of the scheduler from the server
async function getSchedulerStatus() {
  try {
    const response = await fetch("/api/scheduler/status");
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    return data;
  } catch (error) {
    console.error("Error getting scheduler status:", error);
    return { isActive: false };
  }
}

// Update the UI based on scheduler status
async function updateScheduleStatus() {
  const status = await getSchedulerStatus();
  
  if (status.isActive) {
    activateSchedulerButton.disabled = true;
    deactivateSchedulerButton.disabled = false;
    activateSchedulerButton.classList.add("disabled");
    deactivateSchedulerButton.classList.remove("disabled");
  } else {
    activateSchedulerButton.disabled = false;
    deactivateSchedulerButton.disabled = true;
    activateSchedulerButton.classList.remove("disabled");
    deactivateSchedulerButton.classList.add("disabled");
  }
}

// Activate the scheduler
async function activateScheduler() {
  try {
    const response = await fetch("/api/scheduler/activate", { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    await updateScheduleStatus();
    alert("Scheduler activated");
  } catch (error) {
    console.error("Error activating scheduler:", error);
    alert("Failed to activate scheduler: " + error.message);
  }
}

// Deactivate the scheduler
async function deactivateScheduler() {
  try {
    const response = await fetch("/api/scheduler/deactivate", { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    await updateScheduleStatus();
    alert("Scheduler deactivated");
  } catch (error) {
    console.error("Error deactivating scheduler:", error);
    alert("Failed to deactivate scheduler: " + error.message);
  }
}

// Create a new schedule
function createNewSchedule() {
  debugPrintln("Creating new schedule");
  
  if (schedulerState.scheduleCount >= MAX_SCHEDULES) {
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
  
  // Update UI
  populateScheduleDropdown();
  loadActiveSchedule();
  
  // Save changes
  saveSchedulerState();
}

// Update current time line periodically
setInterval(updateCurrentTimeLine, 60000); // Update every minute

// Initialize the page
document.addEventListener("DOMContentLoaded", async function() {
  debugPrintln("DOM content loaded, initializing scheduler UI");
  
  // Initialize UI references
  initUI();
  
  // Load scheduler state
  await loadSchedulerState();
  
  // Render active schedules
  renderActiveSchedules();
  
  // Check scheduler status
  await updateScheduleStatus();
  
  // Add event listeners
  scheduleDropdown.addEventListener("change", function() {
    schedulerState.currentScheduleIndex = parseInt(this.value);
    loadActiveSchedule();
  });
  
  // Add event listeners for relay checkboxes
  const checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
  checkboxes.forEach(checkbox => {
    checkbox.addEventListener("change", saveSchedulerState);
  });
  
  // Add event listeners for buttons
  addEventButton.addEventListener("click", addEvent);
  saveScheduleButton.addEventListener("click", saveSchedulerState);
  activateSchedulerButton.addEventListener("click", activateScheduler);
  deactivateSchedulerButton.addEventListener("click", deactivateScheduler);
  newScheduleButton.addEventListener("click", createNewSchedule);
  
  // Add event listeners for modal buttons
  document.getElementById("modal-save").addEventListener("click", saveEditModal);
  document.getElementById("modal-cancel").addEventListener("click", closeEditModal);
  
  debugPrintln("Scheduler UI initialized");
});