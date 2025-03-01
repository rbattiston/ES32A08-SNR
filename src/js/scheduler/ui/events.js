/**
 * Event-related UI functions for the scheduler
 */

let updateCurrentTimeLine = () => {
  console.log("Timeline update function not loaded yet");
};

//import { updateCurrentTimeLine } from './timeline';
import { MAX_EVENTS, debugPrintln } from '../constants';
import { schedulerState, saveSchedulerState } from '../state';


import('./timeline').then(timeline => {
  updateCurrentTimeLine = timeline.updateCurrentTimeLine;
  timeline.startTimelineUpdates();
});

// Variables for event editing modal
let currentEditingIndex = null;

// Render the event list for the active schedule
export function renderEventList() {
  debugPrintln("Rendering event list");
  
  const eventList = document.getElementById("event-list");
  
  if (!eventList) {
    debugPrintln("Event list element not found");
    return;
  }
  
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

// Render the timeline visualization for the active schedule
export function renderTimeline() {
  debugPrintln("Rendering timeline");
  
  const timelineContainer = document.getElementById("timeline-container");
  
  if (!timelineContainer) {
    debugPrintln("Timeline container not found");
    return;
  }
  
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
/*export function updateCurrentTimeLine() {
  const timelineContainer = document.getElementById("timeline-container");
  if (!timelineContainer) return;
  
  // Remove any existing time line
  const existingLine = timelineContainer.querySelector(".current-time-line");
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
  
  timelineContainer.appendChild(currentLine);
}*/

// Add a new event to the active schedule
export function addEvent() {
  debugPrintln("Adding new event");
  
  const eventTimeInput = document.getElementById("event-time");
  const eventDurationInput = document.getElementById("event-duration");
  const eventRepeatInput = document.getElementById("event-repeat");
  const eventRepeatIntervalInput = document.getElementById("event-repeat-interval");
  
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
    
    // Generate the event ID
    const eventId = Date.now().toString() + "_" + i;
    
    // Log the event ID
    console.log(`Creating event with ID: ${eventId}, time: ${occurrenceTime}, duration: ${duration}`);

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

// Delete an event from the active schedule
export function deleteEvent(index) {
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
  
  // Save changes
  saveSchedulerState();
}

// Populate time selects
export function populateTimeSelects() {
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
export function openEditModal(index) {
  debugPrintln(`Opening edit modal for event ${index}`);
  
  const schedule = schedulerState.schedules[schedulerState.currentScheduleIndex];
  const event = schedule.events[index];
  let currentEditingIndex = index;
  
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
export function closeEditModal() {
  document.getElementById("edit-modal").style.display = "none";
  currentEditingIndex = null;
}

// Save changes from edit modal
export function saveEditModal() {
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

// Initialize event listeners for the modal
export function initModalListeners() {
  document.getElementById("modal-save").addEventListener("click", saveEditModal);
  document.getElementById("modal-cancel").addEventListener("click", closeEditModal);
}

// Update current time line periodically
export function startTimelineUpdates() {
  setInterval(updateCurrentTimeLine, 60000); // Update every minute
}