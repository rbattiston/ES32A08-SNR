"use strict";

// Global scheduler state model
let schedulerState = {
  scheduleCount: 0,
  currentScheduleIndex: 0,
  schedules: []
};

// Debug helper functions
function debugPrintln(msg) { console.log("[DEBUG] " + msg); }
function debugPrintf(fmt, ...args) { console.log("[DEBUG] " + fmt, ...args); }

// Utility: Convert local "HH:MM" to GMT "HH:MM"
function localTimeToGMT(timeStr) {
  let [hours, minutes] = timeStr.split(":").map(Number);
  let localMinutes = hours * 60 + minutes;
  let offset = new Date().getTimezoneOffset();
  let gmtMinutes = localMinutes + offset;
  gmtMinutes = ((gmtMinutes % 1440) + 1440) % 1440;
  let gmtHour = Math.floor(gmtMinutes / 60);
  let gmtMin = gmtMinutes % 60;
  return `${gmtHour.toString().padStart(2, "0")}:${gmtMin.toString().padStart(2, "0")}`;
}

// Utility: Convert GMT "HH:MM" to local "HH:MM"
function GMTToLocal(timeStr) {
  let [hours, minutes] = timeStr.split(":").map(Number);
  let gmtMinutes = hours * 60 + minutes;
  let offset = new Date().getTimezoneOffset();
  let localMinutes = gmtMinutes - offset;
  localMinutes = ((localMinutes % 1440) + 1440) % 1440;
  let localHour = Math.floor(localMinutes / 60);
  let localMin = localMinutes % 60;
  return `${localHour.toString().padStart(2, "0")}:${localMin.toString().padStart(2, "0")}`;
}

// UI element references
let scheduleDropdown, scheduleNameInput, relayMaskContainer, lightsOnInput, lightsOffInput;
let eventList, timelineContainer;
let addEventButton, saveScheduleButton, activateSchedulerButton, deactivateSchedulerButton, newScheduleButton;
let timezoneDropdown, setTimezoneButton;

// Initialize UI references
function initUI() {
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
  timezoneDropdown = document.getElementById("timezone-dropdown");
  setTimezoneButton = document.getElementById("set-timezone");
}

// Populate schedule dropdown
function populateScheduleDropdown() {
  scheduleDropdown.innerHTML = "";
  for (let i = 0; i < schedulerState.scheduleCount; i++) {
    let opt = document.createElement("option");
    opt.value = i;
    opt.textContent = schedulerState.schedules[i].name;
    scheduleDropdown.appendChild(opt);
  }
  scheduleDropdown.value = schedulerState.currentScheduleIndex;
}

// Load active schedule into UI
function loadActiveSchedule() {
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  scheduleNameInput.value = sch.name;
  lightsOnInput.value = GMTToLocal(sch.lightsOnTime);
  lightsOffInput.value = GMTToLocal(sch.lightsOffTime);
  let checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
  checkboxes.forEach((cb, idx) => { cb.checked = (sch.relayMask & (1 << idx)) !== 0; });
  renderEventList();
  renderTimeline();
}

// Render event list for active schedule
function renderEventList() {
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  eventList.innerHTML = "";
  if (sch.eventCount === 0) {
    eventList.innerHTML = "<p>No events.</p>";
    return;
  }
  sch.events.forEach((ev, idx) => {
    let div = document.createElement("div");
    div.className = "event-item";
    let localTime = GMTToLocal(ev.time);
    div.innerHTML = `<span>${localTime} - Duration: ${ev.duration}s</span> <button data-index="${idx}" class="delete-event">Delete</button>`;
    div.addEventListener("click", () => openEditModal(idx));
    eventList.appendChild(div);
  });
  let delButtons = eventList.querySelectorAll(".delete-event");
  delButtons.forEach(btn => {
    btn.addEventListener("click", (e) => {
      e.stopPropagation();
      let idx = parseInt(btn.getAttribute("data-index"));
      deleteEvent(idx);
    });
  });
}

function deleteEvent(idx) {
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  sch.events.splice(idx, 1);
  sch.eventCount = sch.events.length;
  renderEventList();
  renderTimeline();
}

// Render a single unified timeline visualization for active schedule
function renderTimeline() {
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  timelineContainer.innerHTML = "";
  
  // Create background showing lights on/off times.
  let bgDiv = document.createElement("div");
  bgDiv.className = "timeline-bg";
  let lightsOnLocal = GMTToLocal(sch.lightsOnTime);
  let lightsOffLocal = GMTToLocal(sch.lightsOffTime);
  let [onH, onM] = lightsOnLocal.split(":").map(Number);
  let [offH, offM] = lightsOffLocal.split(":").map(Number);
  let onMin = onH * 60 + onM;
  let offMin = offH * 60 + offM;
  if (onMin < offMin) {
    bgDiv.style.background = `linear-gradient(to right, lightgrey 0%, lightgrey ${(onMin/1440)*100}%, lightyellow ${(onMin/1440)*100}%, lightyellow ${(offMin/1440)*100}%, lightgrey ${(offMin/1440)*100}%, lightgrey 100%)`;
  } else {
    bgDiv.style.background = `linear-gradient(to right, lightyellow 0%, lightyellow ${(offMin/1440)*100}%, lightgrey ${(offMin/1440)*100}%, lightgrey ${(onMin/1440)*100}%, lightyellow ${(onMin/1440)*100}%, lightyellow 100%)`;
  }
  bgDiv.style.position = "absolute";
  bgDiv.style.top = "0";
  bgDiv.style.left = "0";
  bgDiv.style.width = "100%";
  bgDiv.style.height = "100%";
  timelineContainer.appendChild(bgDiv);
  
  // Create a single timeline bar to draw events.
  let timelineBar = document.createElement("div");
  timelineBar.className = "timeline-bar";
  timelineBar.style.position = "relative";
  timelineBar.style.height = "100%";
  timelineBar.style.width = "100%";
  timelineBar.style.zIndex = "5";
  
  // For each event, draw an event block.
  sch.events.forEach(ev => {
    let [hour, minute] = ev.time.split(":").map(Number);
    let eventMin = hour * 60 + minute;
    let leftPercent = (eventMin / 1440) * 100;
    let widthPercent = (ev.duration / 86400) * 100;
    let eventBlock = document.createElement("div");
    eventBlock.className = "timeline-event";
    eventBlock.style.left = leftPercent + "%";
    eventBlock.style.width = widthPercent + "%";
    eventBlock.title = `Event: ${GMTToLocal(ev.time)} - ${ev.duration}s`;
    eventBlock.addEventListener("click", (e) => {
      e.stopPropagation();
      let idx = sch.events.indexOf(ev);
      openEditModal(idx);
    });
    timelineBar.appendChild(eventBlock);
  });
  
  timelineContainer.appendChild(timelineBar);
  updateCurrentTimeLine();
}

// Update current time line based on local time
function updateCurrentTimeLine() {
  let container = timelineContainer;
  if (!container) return;
  let currentLine = container.querySelector(".current-time-line");
  if (!currentLine) {
    currentLine = document.createElement("div");
    currentLine.className = "current-time-line";
    currentLine.style.position = "absolute";
    currentLine.style.top = "0";
    currentLine.style.bottom = "0";
    currentLine.style.width = "2px";
    currentLine.style.backgroundColor = "red";
    currentLine.style.zIndex = "10";
    container.appendChild(currentLine);
  }
  let now = new Date();
  let localMins = now.getHours() * 60 + now.getMinutes();
  let leftPercent = (localMins / 1440) * 100;
  currentLine.style.left = leftPercent + "%";
}
setInterval(updateCurrentTimeLine, 1000);

// Modal for editing an event
function populateTimeSelects() {
  let hourSelect = document.getElementById("edit-hour");
  let minuteSelect = document.getElementById("edit-minute");
  hourSelect.innerHTML = "";
  minuteSelect.innerHTML = "";
  for (let i = 0; i < 24; i++) {
    let opt = document.createElement("option");
    opt.value = i;
    opt.textContent = i.toString().padStart(2, "0");
    hourSelect.appendChild(opt);
  }
  for (let i = 0; i < 60; i++) {
    let opt = document.createElement("option");
    opt.value = i;
    opt.textContent = i.toString().padStart(2, "0");
    minuteSelect.appendChild(opt);
  }
}
function openEditModal(index) {
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  let ev = sch.events[index];
  currentEditingIndex = index;
  populateTimeSelects();
  let [hourStr, minuteStr] = ev.time.split(":");
  document.getElementById("edit-hour").value = parseInt(hourStr);
  document.getElementById("edit-minute").value = parseInt(minuteStr);
  document.getElementById("edit-duration").value = ev.duration;
  document.getElementById("edit-modal").style.display = "block";
}
function closeEditModal() {
  document.getElementById("edit-modal").style.display = "none";
}
function saveEditModal() {
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  let ev = sch.events[currentEditingIndex];
  let hour = document.getElementById("edit-hour").value;
  let minute = document.getElementById("edit-minute").value;
  ev.time = `${hour.toString().padStart(2,'0')}:${minute.toString().padStart(2,'0')}`;
  ev.startMinute = parseInt(hour) * 60 + parseInt(minute);
  ev.duration = parseInt(document.getElementById("edit-duration").value);
  renderEventList();
  renderTimeline();
  saveSchedule();
  closeEditModal();
}

// Save the entire schedulerState to backend
function saveSchedule() {
  let mask = 0;
  let checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
  checkboxes.forEach((cb, idx) => { if (cb.checked) mask |= (1 << idx); });
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  sch.relayMask = mask;
  sch.name = scheduleNameInput.value;
  sch.lightsOnTime = localTimeToGMT(lightsOnInput.value);
  sch.lightsOffTime = localTimeToGMT(lightsOffInput.value);
  fetch("/api/scheduler/save", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(schedulerState)
  })
  .then(response => response.json())
  .then(data => alert("Schedule saved successfully."))
  .catch(err => console.error(err));
}

// Load schedulerState from backend
function loadSchedule() {
  fetch("/api/scheduler/load")
  .then(response => response.json())
  .then(data => {
    schedulerState = data;
    populateScheduleDropdown();
    loadActiveSchedule();
  })
  .catch(err => console.error(err));
}

// When adding an event, create individual entries for each occurrence.
function addEvent() {
  let eventTimeLocal = document.getElementById("event-time").value;
  let baseTimeGMT = localTimeToGMT(eventTimeLocal);
  let duration = parseInt(document.getElementById("event-duration").value);
  let repeatCount = parseInt(document.getElementById("event-repeat").value);
  let repeatInterval = parseInt(document.getElementById("event-repeat-interval").value);
  let sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
  for (let i = 0; i <= repeatCount; i++) {
    let [baseHour, baseMinute] = baseTimeGMT.split(":").map(Number);
    let baseMins = baseHour * 60 + baseMinute;
    let occurrenceMins = baseMins + i * repeatInterval;
    if (occurrenceMins >= 1440) break;
    let occHour = Math.floor(occurrenceMins / 60);
    let occMinute = occurrenceMins % 60;
    let occurrenceTime = `${occHour.toString().padStart(2,"0")}:${occMinute.toString().padStart(2,"0")}`;
    let newEvent = {
      id: Date.now().toString() + "_" + i,
      time: occurrenceTime,
      duration: duration,
      executedMask: 0
    };
    sch.events.push(newEvent);
  }
  sch.eventCount = sch.events.length;
  renderEventList();
  renderTimeline();
  saveSchedule();
}

// Timezone selection: update the backend timezone (requires backend support)
function setTimezone() {
  let tz = timezoneDropdown.value;
  fetch("/api/timezone/set", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ timezone: tz })
  })
  .then(response => response.json())
  .then(data => alert("Timezone updated."))
  .catch(err => console.error(err));
}

// Global variable for modal editing
let currentEditingIndex = null;

// Initialization
document.addEventListener("DOMContentLoaded", function() {
  debugPrintln("scheduler.js: DOMContentLoaded event fired");
  initUI();
  loadSchedule();
  
  scheduleDropdown.addEventListener("change", function() {
    schedulerState.currentScheduleIndex = parseInt(this.value);
    loadActiveSchedule();
  });
  
  let checkboxes = relayMaskContainer.querySelectorAll("input[type=checkbox]");
  checkboxes.forEach(cb => { cb.addEventListener("change", saveSchedule); });
  
  addEventButton.addEventListener("click", addEvent);
  saveScheduleButton.addEventListener("click", saveSchedule);
  activateSchedulerButton.addEventListener("click", function() {
    fetch("/api/scheduler/activate", { method: "POST" })
      .then(response => response.json())
      .then(() => alert("Scheduler activated."))
      .catch(err => console.error(err));
  });
  deactivateSchedulerButton.addEventListener("click", function() {
    fetch("/api/scheduler/deactivate", { method: "POST" })
      .then(response => response.json())
      .then(() => alert("Scheduler deactivated."))
      .catch(err => console.error(err));
  });
  newScheduleButton.addEventListener("click", function() {
    let now = new Date();
    let defaultName = "Schedule " + now.toLocaleString();
    let newSch = {
      name: defaultName,
      metadata: now.toISOString(),
      relayMask: 0,
      lightsOnTime: localTimeToGMT("06:00"),
      lightsOffTime: localTimeToGMT("18:00"),
      eventCount: 0,
      events: []
    };
    schedulerState.schedules.push(newSch);
    schedulerState.scheduleCount = schedulerState.schedules.length;
    schedulerState.currentScheduleIndex = schedulerState.scheduleCount - 1;
    populateScheduleDropdown();
    loadActiveSchedule();
    saveSchedule();
  });
  
  setTimezoneButton.addEventListener("click", setTimezone);
  
  document.getElementById("modal-save").addEventListener("click", saveEditModal);
  document.getElementById("modal-cancel").addEventListener("click", closeEditModal);
});
