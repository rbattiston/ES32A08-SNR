/**
 * Core UI operations for the scheduler
 */
import { debugPrintln } from '../constants';
import { schedulerState } from '../state';
import { renderEventList, renderTimeline } from './events';
import { renderActiveSchedules } from './active-schedules';

// UI element references
let scheduleDropdown;
let scheduleNameInput;
let relayMaskContainer;
let lightsOnInput;
let lightsOffInput;
let eventList;
let timelineContainer;
let activeSchedulesContainer; // Properly declared at module level
let addEventButton;
let saveScheduleButton;
let activateSchedulerButton;
let deactivateSchedulerButton;
let newScheduleButton;
let eventTimeInput;
let eventDurationInput;
let eventRepeatInput;
let eventRepeatIntervalInput;

// Initialize UI references
export function initUI() {
  debugPrintln("Initializing UI references");
  
  scheduleDropdown = document.getElementById("schedule-dropdown");
  scheduleNameInput = document.getElementById("schedule-name");
  relayMaskContainer = document.getElementById("relay-mask");
  lightsOnInput = document.getElementById("lights-on-time");
  lightsOffInput = document.getElementById("lights-off-time");
  eventList = document.getElementById("event-list");
  timelineContainer = document.getElementById("timeline-container");
  activeSchedulesContainer = document.getElementById("active-schedules-container");
  
  addEventButton = document.getElementById("add-event");
  saveScheduleButton = document.getElementById("save-schedule");
  activateSchedulerButton = document.getElementById("activate-scheduler");
  deactivateSchedulerButton = document.getElementById("deactivate-scheduler");
  newScheduleButton = document.getElementById("new-schedule");
  
  eventTimeInput = document.getElementById("event-time");
  eventDurationInput = document.getElementById("event-duration");
  eventRepeatInput = document.getElementById("event-repeat");
  eventRepeatIntervalInput = document.getElementById("event-repeat-interval");
  
  if (!scheduleDropdown || !scheduleNameInput || !relayMaskContainer || 
      !lightsOnInput || !lightsOffInput || !eventList || !timelineContainer || 
      !activeSchedulesContainer) {
    debugPrintln("ERROR: Some required UI elements not found!");
  }
}

// Populate schedule dropdown with available schedules
export function populateScheduleDropdown() {
  debugPrintln("Populating schedule dropdown");
  
  if (!scheduleDropdown) {
    debugPrintln("Schedule dropdown not found");
    return;
  }
  
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
export function loadActiveSchedule() {
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
}

// Update all UI components that show schedule information
export function updateAllScheduleViews() {
  renderActiveSchedules();
  populateScheduleDropdown();
  loadActiveSchedule();
}