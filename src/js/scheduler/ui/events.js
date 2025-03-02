/**
 * Event-related UI functions for the scheduler
 */
import { MAX_EVENTS, debugPrintln } from '../constants';
import { 
  schedulerState, 
  addEvent as addEventToSchedule, 
  deleteEvent as deleteEventFromSchedule, 
  storePendingSchedule 
} from '../state-model';

// Define SchedulerMode here since it's not being exported from state-model
const SchedulerMode = {
  VIEW_ONLY: 'view-only',
  CREATING: 'creating',
  EDITING: 'editing'
};

// Variables for event editing modal
let currentEditingIndex = null;

// Render the event list for the currently active or pending schedule
export function renderEventList() {
  debugPrintln("Rendering event list");
  
  const eventList = document.getElementById("event-list");
  
  if (!eventList) {
    debugPrintln("Event list element not found");
    return;
  }
  
  // Determine which schedule to show events for
  const schedule = schedulerState.mode !== SchedulerMode.VIEW_ONLY && schedulerState.pendingSchedule
    ? schedulerState.pendingSchedule
    : (schedulerState.scheduleCount > 0 ? schedulerState.schedules[schedulerState.currentScheduleIndex] : null);
  
  // Clear the list
  eventList.innerHTML = "";
  
  if (!schedule) {
    eventList.innerHTML = "<p>No schedules available</p>";
    return;
  }
  
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
      ${schedulerState.mode !== SchedulerMode.VIEW_ONLY ? 
        `<button data-index="${index}" class="delete-event">Delete</button>` : ''}
    `;
    
    // Add click event to edit the event (only in edit/create mode)
    if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
      eventItem.addEventListener("click", () => openEditModal(index));
    }
    
    // Add event to the list
    eventList.appendChild(eventItem);
  });
  
  // Add event listeners to delete buttons
  if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
    const deleteButtons = eventList.querySelectorAll(".delete-event");
    deleteButtons.forEach(button => {
      button.addEventListener("click", (e) => {
        e.stopPropagation();
        const index = parseInt(button.getAttribute("data-index"));
        deleteEventFromSchedule(index);
      });
    });
  }
}

// Render the timeline visualization
export function renderTimeline() {
  debugPrintln("Rendering timeline");
  
  const timelineContainer = document.getElementById("timeline-container");
  
  if (!timelineContainer) {
    debugPrintln("Timeline container not found");
    return;
  }
  
  // Determine which schedule to show
  const schedule = schedulerState.mode !== SchedulerMode.VIEW_ONLY && schedulerState.pendingSchedule
    ? schedulerState.pendingSchedule
    : (schedulerState.scheduleCount > 0 ? schedulerState.schedules[schedulerState.currentScheduleIndex] : null);
  
  // Clear the container
  timelineContainer.innerHTML = "";
  
  if (!schedule) {
    timelineContainer.innerHTML = "<p>No schedules available</p>";
    return;
  }
  
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
    // Handle case where "on" time is after "off" time (overnight)
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
  
  // Add time markers every 2 hours
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
      
      // Add click handler to edit event (only in edit/create mode)
      if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
        eventBlock.addEventListener("click", (e) => {
          e.stopPropagation();
          openEditModal(index);
        });
      }
      
      timelineBar.appendChild(eventBlock);
    });
  }
  
  timelineContainer.appendChild(timelineBar);
  
  // Add current time line
  updateCurrentTimeLine();
}

// Update the current time line on the timeline
export function updateCurrentTimeLine() {
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
}

// Start periodic updates of the current time line
export function startTimelineUpdates() {
  // Initial update
  updateCurrentTimeLine();
  
  // Periodic updates every minute
  setInterval(updateCurrentTimeLine, 60000);
}

// Handle the add event button click
export function handleAddEvent() {
  debugPrintln("Handling add event button click");
  
  if (schedulerState.mode === SchedulerMode.VIEW_ONLY) {
    debugPrintln("Cannot add events in view-only mode");
    return;
  }
  
  const eventTimeInput = document.getElementById("event-time");
  const eventDurationInput = document.getElementById("event-duration");
  const eventRepeatInput = document.getElementById("event-repeat");
  const eventRepeatIntervalInput = document.getElementById("event-repeat-interval");
  
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
  
  // Add the event(s)
  addEventToSchedule(eventTime, duration, repeatCount, repeatInterval);
}

// Open edit modal for an event
export function openEditModal(index) {
  debugPrintln(`Opening edit modal for event ${index}`);
  
  if (schedulerState.mode === SchedulerMode.VIEW_ONLY) {
    debugPrintln("Cannot edit events in view-only mode");
    return;
  }
  
  // Get the current schedule
  const schedule = schedulerState.pendingSchedule || 
                  (schedulerState.scheduleCount > 0 ? 
                   schedulerState.schedules[schedulerState.currentScheduleIndex] : null);
  
  if (!schedule || !schedule.events || index >= schedule.events.length) {
    debugPrintln("Invalid schedule or event index");
    return;
  }
  
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

// Populate hour and minute selects in the edit modal
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

// Close edit modal
export function closeEditModal() {
  document.getElementById("edit-modal").style.display = "none";
  currentEditingIndex = null;
}

// Save changes from edit modal
export function saveEditModal() {
  debugPrintln("Saving changes from edit modal");
  
  if (currentEditingIndex === null || schedulerState.mode === SchedulerMode.VIEW_ONLY) {
    closeEditModal();
    return;
  }
  
  // Get the current schedule
  const schedule = schedulerState.pendingSchedule || 
                  (schedulerState.scheduleCount > 0 ? 
                   schedulerState.schedules[schedulerState.currentScheduleIndex] : null);
  
  if (!schedule || !schedule.events || currentEditingIndex >= schedule.events.length) {
    debugPrintln("Invalid schedule or event index");
    closeEditModal();
    return;
  }
  
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
  
  // If in edit/create mode, update pending schedule
  if (schedulerState.mode !== SchedulerMode.VIEW_ONLY) {
    schedulerState.pendingSchedule = schedule;
    storePendingSchedule();
  }
  
  // Close modal
  closeEditModal();
}

// Initialize modal event listeners
export function initModalListeners() {
  const saveButton = document.getElementById("modal-save");
  const cancelButton = document.getElementById("modal-cancel");
  
  if (saveButton) {
    saveButton.addEventListener("click", saveEditModal);
  }
  
  if (cancelButton) {
    cancelButton.addEventListener("click", closeEditModal);
  }
}