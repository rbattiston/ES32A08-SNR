// scheduler.js

// Global state for the scheduler interface
let schedulerState = {
  lightSchedule: {
    lightsOnTime: "06:00",
    lightsOffTime: "18:00"
  },
  events: [] // Each event: { id, time, duration, relay, repeat, repeatInterval }
};

// Debug helper functions (using your current style)
function debugPrintln(msg) {
  console.log("[DEBUG] " + msg);
}
function debugPrintf(format, ...args) {
  console.log("[DEBUG] " + format, ...args);
}

// Render the list of scheduled events (text list)
function renderEvents() {
  debugPrintln("renderEvents() called");
  const eventList = document.getElementById('event-list');
  if (!eventList) {
    debugPrintln("renderEvents(): event-list element not found");
    return;
  }
  eventList.innerHTML = '';
  if (schedulerState.events.length === 0) {
    eventList.innerHTML = '<p>No scheduled events.</p>';
    return;
  }
  schedulerState.events.forEach((event, index) => {
    const div = document.createElement('div');
    div.className = 'event-item';
    div.innerHTML = `
      <span>${event.time} – Duration: ${event.duration}s – Relay: ${parseInt(event.relay) + 1} – Repeat Count: ${event.repeat} – Interval: ${event.repeatInterval} min</span>
      <button data-index="${index}" class="delete-event">Delete</button>
    `;
    eventList.appendChild(div);
  });
  // Attach delete event listeners
  const deleteButtons = document.querySelectorAll('.delete-event');
  deleteButtons.forEach(button => {
    button.addEventListener('click', function() {
      const idx = parseInt(this.getAttribute('data-index'));
      debugPrintf("Deleting event at index %d", idx);
      deleteEvent(idx);
    });
  });
}

// Delete an event from the state and update the view
function deleteEvent(index) {
  debugPrintf("deleteEvent(): Removing event at index %d", index);
  schedulerState.events.splice(index, 1);
  renderEvents();
  renderTimeline();
}

// Load scheduler state from the server
function loadSchedulerState() {
  debugPrintln("loadSchedulerState() called");
  fetch('/api/scheduler/load')
    .then(response => response.json())
    .then(data => {
      debugPrintln("Scheduler state loaded from server");
      if (data.lightSchedule) {
        schedulerState.lightSchedule = data.lightSchedule;
        const lightsOn = document.getElementById('lights-on-time');
        const lightsOff = document.getElementById('lights-off-time');
        if (lightsOn) lightsOn.value = schedulerState.lightSchedule.lightsOnTime;
        if (lightsOff) lightsOff.value = schedulerState.lightSchedule.lightsOffTime;
        debugPrintf("Light schedule: on=%s, off=%s", schedulerState.lightSchedule.lightsOnTime, schedulerState.lightSchedule.lightsOffTime);
      }
      if (data.events) {
        schedulerState.events = data.events;
        schedulerState.events.forEach(event => {
          if (typeof event.repeatInterval === 'undefined') {
            event.repeatInterval = 60;
          }
        });
        debugPrintf("Loaded %d events", schedulerState.events.length);
        renderEvents();
        renderTimeline();
      }
    })
    .catch(err => {
      debugPrintln("Error loading scheduler state: " + err);
      console.error('Error loading scheduler state:', err);
    });
}

// Save scheduler state to the server
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
    console.error('Error saving scheduler state:', err);
  });
}

// Add a new event based on form inputs
function addEvent() {
  debugPrintln("addEvent() called");
  const eventTimeEl = document.getElementById('event-time');
  const eventDurationEl = document.getElementById('event-duration');
  const eventRelayEl = document.getElementById('event-relay');
  const eventRepeatEl = document.getElementById('event-repeat');
  const eventRepeatIntervalEl = document.getElementById('event-repeat-interval');
  if (eventTimeEl && eventDurationEl && eventRelayEl && eventRepeatEl && eventRepeatIntervalEl) {
    const time = eventTimeEl.value;
    const duration = parseInt(eventDurationEl.value);
    const relay = eventRelayEl.value;
    const repeat = parseInt(eventRepeatEl.value);
    const repeatInterval = parseInt(eventRepeatIntervalEl.value);
    
    const newEvent = {
      id: Date.now().toString(),
      time: time,
      duration: duration,
      relay: relay,
      repeat: repeat,
      repeatInterval: repeatInterval
    };
    debugPrintf("New event added: %s", JSON.stringify(newEvent));
    schedulerState.events.push(newEvent);
    renderEvents();
    renderTimeline();
  } else {
    debugPrintln("addEvent(): One or more input elements not found");
  }
}

// Activate the scheduler via API
function activateScheduler() {
  debugPrintln("activateScheduler() called");
  fetch('/api/scheduler/activate', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      debugPrintln("Scheduler activated via API");
      alert('Scheduler activated.');
    })
    .catch(err => {
      debugPrintln("Error activating scheduler: " + err);
      console.error('Error activating scheduler:', err);
    });
}

// Deactivate the scheduler via API
function deactivateScheduler() {
  debugPrintln("deactivateScheduler() called");
  fetch('/api/scheduler/deactivate', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      debugPrintln("Scheduler deactivated via API");
      alert('Scheduler deactivated.');
    })
    .catch(err => {
      debugPrintln("Error deactivating scheduler: " + err);
      console.error('Error deactivating scheduler:', err);
    });
}

/* --------------------------
   Timeline Visualization
---------------------------*/

// Render the 24-hour timeline for each relay
function renderTimeline() {
  debugPrintln("renderTimeline() called");
  const container = document.getElementById('timeline-container');
  if (!container) {
    debugPrintln("renderTimeline(): timeline-container not found");
    return;
  }
  container.innerHTML = ''; // clear existing content

  // For each relay (0 to 7)
  for (let relay = 0; relay < 8; relay++) {
    // Create a container for this relay's timeline
    const relayContainer = document.createElement('div');
    relayContainer.className = 'timeline-relay';
    
    // Label for the relay
    const label = document.createElement('div');
    label.className = 'timeline-label';
    label.textContent = 'Relay ' + (relay + 1);
    relayContainer.appendChild(label);
    
    // Create the timeline bar for 24 hours
    const timelineBar = document.createElement('div');
    timelineBar.className = 'timeline-bar';
    timelineBar.dataset.relay = relay;
    timelineBar.style.position = 'relative';
    
    // For each event in schedulerState for this relay, add occurrences
    schedulerState.events.forEach(event => {
      if (parseInt(event.relay) === relay) {
        // Convert event time (HH:MM) to minutes from midnight
        const [hour, minute] = event.time.split(':').map(Number);
        const startMinute = hour * 60 + minute;
        // For each repeat occurrence (0 to repeat)
        for (let i = 0; i <= event.repeat; i++) {
          const occurrenceMinute = startMinute + i * event.repeatInterval;
          if (occurrenceMinute >= 1440) break; // outside 24 hours
          // Calculate left position and width in percentage
          const leftPercent = (occurrenceMinute / 1440) * 100;
          const widthPercent = (event.duration / 86400) * 100; // duration in seconds / total seconds in day
          const eventBlock = document.createElement('div');
          eventBlock.className = 'timeline-event';
          eventBlock.style.position = 'absolute';
          eventBlock.style.left = leftPercent + '%';
          eventBlock.style.width = widthPercent + '%';
          eventBlock.style.height = '100%';
          eventBlock.style.backgroundColor = 'rgba(0, 123, 255, 0.5)';
          eventBlock.style.border = '1px solid #007bff';
          eventBlock.style.cursor = 'pointer';
          eventBlock.title = `Event: ${event.time}, Duration: ${event.duration}s, Repeat: ${event.repeat}, Interval: ${event.repeatInterval} min`;
          // Attach click listener for editing event
          eventBlock.addEventListener('click', function(e) {
            e.stopPropagation();
            editEvent(event);
          });
          timelineBar.appendChild(eventBlock);
        }
      }
    });
    
    relayContainer.appendChild(timelineBar);
    container.appendChild(relayContainer);
  }
  // Create or update the current time line
  updateCurrentTimeLine();
}

// Update the moving current time line on the timeline visualization
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

// Periodically update current time line every second
setInterval(updateCurrentTimeLine, 1000);

// Edit event: prompt user to modify event details
function editEvent(event) {
  debugPrintf("Editing event: %s", JSON.stringify(event));
  const input = prompt("Enter new values (time, duration, repeat, repeatInterval) separated by commas", 
    `${event.time},${event.duration},${event.repeat},${event.repeatInterval}`);
  if (input) {
    const parts = input.split(',').map(s => s.trim());
    if (parts.length >= 4) {
      event.time = parts[0];
      event.duration = parseInt(parts[1]);
      event.repeat = parseInt(parts[2]);
      event.repeatInterval = parseInt(parts[3]);
      renderEvents();
      renderTimeline();
      saveSchedulerState();
    } else {
      alert("Invalid input. Please enter 4 values separated by commas.");
    }
  }
}

/* --------------------------
   End Timeline Visualization
--------------------------- */

// Initialization: attach event listeners after DOM content is loaded.
document.addEventListener('DOMContentLoaded', function() {
  debugPrintln("scheduler.js: DOMContentLoaded event fired");
  const addEventBtn = document.getElementById('add-event');
  if (addEventBtn) {
    addEventBtn.addEventListener('click', addEvent);
    debugPrintln("add-event button listener attached");
  }
  const saveSchedulerBtn = document.getElementById('save-scheduler');
  if (saveSchedulerBtn) {
    saveSchedulerBtn.addEventListener('click', saveSchedulerState);
    debugPrintln("save-scheduler button listener attached");
  }
  const activateSchedulerBtn = document.getElementById('activate-scheduler');
  if (activateSchedulerBtn) {
    activateSchedulerBtn.addEventListener('click', activateScheduler);
    debugPrintln("activate-scheduler button listener attached");
  }
  const deactivateSchedulerBtn = document.getElementById('deactivate-scheduler');
  if (deactivateSchedulerBtn) {
    deactivateSchedulerBtn.addEventListener('click', deactivateScheduler);
    debugPrintln("deactivate-scheduler button listener attached");
  }
  
  // Load the initial scheduler state from the server.
  loadSchedulerState();
});
