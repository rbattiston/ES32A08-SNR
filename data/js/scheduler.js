// scheduler.js

// Global state for the scheduler interface
let schedulerState = {
  lightSchedule: {
    lightsOnTime: "06:00",
    lightsOffTime: "18:00"
  },
  events: [] // Each event: { id, time, duration, relay, repeatCount, repeatInterval, executedMask }
};

// Global variable for current editing index (for the modal)
let currentEditingIndex = null;

// Debug helper functions
function debugPrintln(msg) {
  console.log("[DEBUG] " + msg);
}
function debugPrintf(format, ...args) {
  console.log("[DEBUG] " + format, ...args);
}

// -------------------------------
// UI Rendering for Event List
// -------------------------------
function renderEvents() {
  debugPrintln("renderEvents() called");
  const eventList = document.getElementById('event-list');
  if (!eventList) return;
  eventList.innerHTML = '';
  if (schedulerState.events.length === 0) {
    eventList.innerHTML = '<p>No scheduled events.</p>';
    return;
  }
  schedulerState.events.forEach((event, index) => {
    const div = document.createElement('div');
    div.className = 'event-item';
    div.innerHTML = `
      <span>${event.time} – Duration: ${event.duration}s – Relay: ${parseInt(event.relay)+1} – Repeat: ${event.repeatCount} – Interval: ${event.repeatInterval} min</span>
      <button data-index="${index}" class="delete-event">Delete</button>
    `;
    div.addEventListener('click', () => {
      openEditModal(index);
    });
    eventList.appendChild(div);
  });
  // Attach delete button listeners
  const deleteButtons = document.querySelectorAll('.delete-event');
  deleteButtons.forEach(button => {
    button.addEventListener('click', function(e) {
      e.stopPropagation();
      const idx = parseInt(this.getAttribute('data-index'));
      debugPrintf("Deleting event at index %d", idx);
      deleteEvent(idx);
    });
  });
}

function deleteEvent(index) {
  debugPrintf("deleteEvent(): Removing event at index %d", index);
  schedulerState.events.splice(index, 1);
  renderEvents();
  renderTimeline();
}

// -------------------------------
// Persistence Functions
// -------------------------------
function loadSchedulerState() {
  debugPrintln("loadSchedulerState() called");
  fetch('/api/scheduler/load')
    .then(response => response.json())
    .then(data => {
      debugPrintln("Scheduler state loaded from server");
      if (data.lightSchedule) {
        schedulerState.lightSchedule = data.lightSchedule;
        document.getElementById('lights-on-time').value = schedulerState.lightSchedule.lightsOnTime;
        document.getElementById('lights-off-time').value = schedulerState.lightSchedule.lightsOffTime;
      }
      if (data.events) {
        schedulerState.events = data.events;
        schedulerState.events.forEach(event => {
          if (typeof event.repeatInterval === 'undefined') {
            event.repeatInterval = 60;
          }
          if (typeof event.repeat !== 'undefined') {
            event.repeatCount = event.repeat;
            delete event.repeat;
          }
        });
        renderEvents();
        renderTimeline();
      }
    })
    .catch(err => {
      debugPrintln("Error loading scheduler state: " + err);
      console.error(err);
    });
}

function saveSchedulerState() {
  debugPrintln("saveSchedulerState() called");
  const lightsOn = document.getElementById('lights-on-time');
  const lightsOff = document.getElementById('lights-off-time');
  if (lightsOn && lightsOff) {
    schedulerState.lightSchedule.lightsOnTime = lightsOn.value;
    schedulerState.lightSchedule.lightsOffTime = lightsOff.value;
    debugPrintf("Saving light schedule: on=%s, off=%s", lightsOn.value, lightsOff.value);
  }
  const payload = JSON.stringify(schedulerState);
  debugPrintf("Payload for saving: %s", payload);
  fetch('/api/scheduler/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: payload
  })
  .then(response => response.json())
  .then(data => {
    debugPrintln("Scheduler saved successfully - SPIFFS written");
    alert('Scheduler saved successfully.');
  })
  .catch(err => {
    debugPrintln("Error saving scheduler state: " + err);
    console.error(err);
  });
}

// -------------------------------
// Adding a New Event
// -------------------------------
function addEvent() {
  debugPrintln("addEvent() called");
  const eventTimeEl = document.getElementById('event-time');
  const eventDurationEl = document.getElementById('event-duration');
  const eventRelayEl = document.getElementById('event-relay');
  const eventRepeatEl = document.getElementById('event-repeat');
  const eventRepeatIntervalEl = document.getElementById('event-repeat-interval');
  if (eventTimeEl && eventDurationEl && eventRelayEl && eventRepeatEl && eventRepeatIntervalEl) {
    // Convert the time to GMT.
    const localTime = eventTimeEl.value; // e.g., "15:00"
    const timeGMT = localTimeToGMT(localTime); // convert to GMT
    const duration = parseInt(eventDurationEl.value);
    const relay = eventRelayEl.value;
    const repeatCount = parseInt(eventRepeatEl.value);
    const repeatInterval = parseInt(eventRepeatIntervalEl.value);
    const newEvent = {
      id: Date.now().toString(),
      time: timeGMT, // store time in GMT
      duration: duration,
      relay: relay,
      repeatCount: repeatCount,
      repeatInterval: repeatInterval,
      executedMask: 0
    };
    schedulerState.events.push(newEvent);
    renderEvents();
    renderTimeline();
  } else {
    debugPrintln("addEvent(): One or more input elements not found");
  }
}


// -------------------------------
// Timeline Visualization (Always 1-hour ticks)
// -------------------------------
function updateTickMarks(ticksContainer, intervalInMinutes) {
  ticksContainer.innerHTML = '';
  for (let m = 0; m <= 1440; m += intervalInMinutes) {
    const tick = document.createElement('div');
    tick.className = 'tick-mark';
    tick.style.position = 'absolute';
    tick.style.left = (m / 1440 * 100) + '%';
    const hour = Math.floor(m / 60);
    const labelText = `${hour.toString().padStart(2, '0')}:00`;
    tick.textContent = labelText;
    tick.style.fontSize = '10px';
    tick.style.transform = 'translateX(-50%)';
    ticksContainer.appendChild(tick);
  }
}

function localTimeToGMT(timeStr) {
  // Parse the local time string "HH:MM"
  let [hours, minutes] = timeStr.split(':').map(Number);
  // Compute local minutes since midnight.
  let localMinutes = hours * 60 + minutes;
  // getTimezoneOffset() returns the difference (in minutes) from local time to UTC.
  // For example, if local time is 15:00 and offset is 240 (4 hours behind UTC), then GMT = 15:00 + 240 minutes.
  let offset = new Date().getTimezoneOffset(); 
  // Compute GMT minutes (add offset since getTimezoneOffset is positive if local is behind UTC)
  let gmtMinutes = localMinutes + offset;
  // Normalize between 0 and 1440.
  gmtMinutes = ((gmtMinutes % 1440) + 1440) % 1440;
  let gmtHours = Math.floor(gmtMinutes / 60);
  let gmtMins = gmtMinutes % 60;
  return `${gmtHours.toString().padStart(2, '0')}:${gmtMins.toString().padStart(2, '0')}`;
}

function renderTimeline() {
  debugPrintln("renderTimeline() called");
  const container = document.getElementById('timeline-container');
  if (!container) return;
  container.innerHTML = ''; // clear previous timeline

  // For each relay (0 to 7)
  for (let relay = 0; relay < 8; relay++) {
    const relayContainer = document.createElement('div');
    relayContainer.className = 'timeline-relay';
    
    const label = document.createElement('div');
    label.className = 'timeline-label';
    label.textContent = 'Relay ' + (relay + 1);
    relayContainer.appendChild(label);
    
    // Create bar container for zooming (without tick changes)
    const barContainer = document.createElement('div');
    barContainer.className = 'timeline-bar-container';
    barContainer.style.position = 'relative';
    
    // Create timeline bar (for events)
    const timelineBar = document.createElement('div');
    timelineBar.className = 'timeline-bar';
    timelineBar.dataset.relay = relay;
    
    // Append event blocks for events on this relay
    schedulerState.events.forEach(event => {
      if (parseInt(event.relay) === relay) {
        const [hour, minute] = event.time.split(':').map(Number);
        const startMinute = hour * 60 + minute;
        for (let i = 0; i <= event.repeatCount; i++) {
          const occurrence = startMinute + i * event.repeatInterval;
          if (occurrence >= 1440) break;
          const leftPercent = (occurrence / 1440) * 100;
          const widthPercent = (event.duration / 86400) * 100;
          const eventBlock = document.createElement('div');
          eventBlock.className = 'timeline-event';
          eventBlock.style.left = leftPercent + '%';
          eventBlock.style.width = widthPercent + '%';
          eventBlock.title = `Time: ${event.time}, Duration: ${event.duration}s, Repeat: ${event.repeatCount}, Interval: ${event.repeatInterval} min`;
          eventBlock.addEventListener('click', function(e) {
            e.stopPropagation();
            const index = schedulerState.events.indexOf(event);
            openEditModal(index);
          });
          timelineBar.appendChild(eventBlock);
        }
      }
    });
    
    // Create ticks container (always 1-hour increments)
    const ticksContainer = document.createElement('div');
    ticksContainer.className = 'timeline-ticks';
    ticksContainer.style.position = 'absolute';
    ticksContainer.style.top = '0';
    ticksContainer.style.left = '0';
    ticksContainer.style.width = '100%';
    ticksContainer.style.pointerEvents = 'none';
    updateTickMarks(ticksContainer, 60);
    
    // Attach mouse events for zoom (but do not change tick marks)
    barContainer.addEventListener('mousemove', function(e) {
      const rect = barContainer.getBoundingClientRect();
      const mouseX = e.clientX - rect.left;
      const percent = mouseX / rect.width;
      timelineBar.style.transformOrigin = (percent * 100) + '% center';
      timelineBar.style.transform = 'scaleX(24)';
    });
    barContainer.addEventListener('mouseleave', function() {
      timelineBar.style.transform = 'scaleX(1)';
    });
    
    barContainer.appendChild(timelineBar);
    barContainer.appendChild(ticksContainer);
    relayContainer.appendChild(barContainer);
    container.appendChild(relayContainer);
  }
  updateCurrentTimeLine();
}

// Create or update the current time line
function updateCurrentTimeLine() {
  const container = document.getElementById('timeline-container');
  if (!container) return;
  let currentLine = container.querySelector('.current-time-line');
  if (!currentLine) {
    currentLine = document.createElement('div');
    currentLine.className = 'current-time-line';
    currentLine.style.position = 'absolute';
    currentLine.style.top = '0';
    currentLine.style.bottom = '0';
    currentLine.style.width = '2px';
    currentLine.style.backgroundColor = 'red';
    currentLine.style.zIndex = '10';
    container.appendChild(currentLine);
  }
  const now = new Date();
  const secondsSinceMidnight = now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds();
  const leftPercent = (secondsSinceMidnight / 86400) * 100;
  currentLine.style.left = leftPercent + '%';
}

setInterval(updateCurrentTimeLine, 1000);

// -------------------------------
// Modal for Editing an Event with Drop-Downs
// -------------------------------
function populateTimeSelects() {
  const hourSelect = document.getElementById('edit-hour');
  const minuteSelect = document.getElementById('edit-minute');
  if (!hourSelect || !minuteSelect) return;
  hourSelect.innerHTML = '';
  minuteSelect.innerHTML = '';
  for (let i = 0; i < 24; i++) {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = i.toString().padStart(2, '0');
    hourSelect.appendChild(opt);
  }
  for (let i = 0; i < 60; i++) {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = i.toString().padStart(2, '0');
    minuteSelect.appendChild(opt);
  }
}

function openEditModal(index) {
  currentEditingIndex = index;
  const event = schedulerState.events[index];
  if (!event) return;
  populateTimeSelects();
  const [hourStr, minuteStr] = event.time.split(':');
  document.getElementById('edit-hour').value = parseInt(hourStr);
  document.getElementById('edit-minute').value = parseInt(minuteStr);
  document.getElementById('edit-duration').value = event.duration;
  document.getElementById('edit-repeat').value = event.repeatCount;
  document.getElementById('edit-repeat-interval').value = event.repeatInterval;
  const modal = document.getElementById('edit-modal');
  modal.style.display = 'block';
}

function closeEditModal() {
  const modal = document.getElementById('edit-modal');
  modal.style.display = 'none';
}

function saveEditModal() {
  if (currentEditingIndex === null) return;
  const event = schedulerState.events[currentEditingIndex];
  const hour = document.getElementById('edit-hour').value;
  const minute = document.getElementById('edit-minute').value;
  event.time = `${hour.toString().padStart(2,'0')}:${minute.toString().padStart(2,'0')}`;
  event.startMinute = parseInt(hour) * 60 + parseInt(minute);
  event.duration = parseInt(document.getElementById('edit-duration').value);
  event.repeatCount = parseInt(document.getElementById('edit-repeat').value);
  event.repeatInterval = parseInt(document.getElementById('edit-repeat-interval').value);
  renderEvents();
  renderTimeline();
  saveSchedulerState();
  closeEditModal();
}

// Attach modal event listeners
document.getElementById('modal-save').addEventListener('click', saveEditModal);
document.getElementById('modal-cancel').addEventListener('click', closeEditModal);

// -------------------------------
// Event Listeners and Initialization
// -------------------------------
document.addEventListener('DOMContentLoaded', function() {
  debugPrintln("scheduler.js: DOMContentLoaded event fired");
  document.getElementById('add-event').addEventListener('click', addEvent);
  document.getElementById('save-scheduler').addEventListener('click', saveSchedulerState);
  document.getElementById('activate-scheduler').addEventListener('click', function() {
    fetch('/api/scheduler/activate', { method: 'POST' })
      .then(response => response.json())
      .then(() => { alert('Scheduler activated.'); })
      .catch(err => console.error(err));
  });
  document.getElementById('deactivate-scheduler').addEventListener('click', function() {
    fetch('/api/scheduler/deactivate', { method: 'POST' })
      .then(response => response.json())
      .then(() => { alert('Scheduler deactivated.'); })
      .catch(err => console.error(err));
  });
  loadSchedulerState();
});
