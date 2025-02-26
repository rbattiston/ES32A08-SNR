/**
 * ES32A08 Irrigation Scheduler JavaScript
 */

// State
let schedulerState = {
  lightSchedule: {
    lightsOnTime: "06:00",
    lightsOffTime: "18:00"
  },
  lightsOnSchedules: [],
  lightsOffSchedules: [],
  customEvents: [],
  templates: [],
  calendarSchedule: [],
  isActive: false,
  currentLightCondition: "Unknown",
  nextEvent: null
};

// DOM Elements - Light Settings
const lightsOnTimeInput = document.getElementById('lights-on-time');
const lightsOffTimeInput = document.getElementById('lights-off-time');
const saveLightScheduleBtn = document.getElementById('save-light-schedule');

// DOM Elements - Tabs
const tabButtons = document.querySelectorAll('.tab-button');
const tabContents = document.querySelectorAll('.tab-content');

// DOM Elements - Lights On Schedule
const lightsOnSchedulesList = document.getElementById('lights-on-schedules');
const lightsOnFrequencyInput = document.getElementById('lights-on-frequency');
const lightsOnFrequencyUnitSelect = document.getElementById('lights-on-frequency-unit');
const lightsOnDurationInput = document.getElementById('lights-on-duration');
const lightsOnRelaySelect = document.getElementById('lights-on-relay');
const addLightsOnScheduleBtn = document.getElementById('add-lights-on-schedule');

// DOM Elements - Lights Off Schedule
const lightsOffSchedulesList = document.getElementById('lights-off-schedules');
const lightsOffFrequencyInput = document.getElementById('lights-off-frequency');
const lightsOffFrequencyUnitSelect = document.getElementById('lights-off-frequency-unit');
const lightsOffDurationInput = document.getElementById('lights-off-duration');
const lightsOffRelaySelect = document.getElementById('lights-off-relay');
const addLightsOffScheduleBtn = document.getElementById('add-lights-off-schedule');

// DOM Elements - Custom Events
const customEventsList = document.getElementById('custom-events-list');
const customEventTimeInput = document.getElementById('custom-event-time');
const customEventDurationInput = document.getElementById('custom-event-duration');
const customEventRelaySelect = document.getElementById('custom-event-relay');
const addCustomEventBtn = document.getElementById('add-custom-event');

// DOM Elements - Templates
const templateNameInput = document.getElementById('template-name');
const saveTemplateBtn = document.getElementById('save-template');
const loadTemplateBtn = document.getElementById('load-template');
const deleteTemplateBtn = document.getElementById('delete-template');
const savedTemplatesList = document.getElementById('saved-templates');

// DOM Elements - Calendar
const calendarWeeksInput = document.getElementById('calendar-weeks');
const generateCalendarBtn = document.getElementById('generate-calendar');
const scheduleCalendarGrid = document.getElementById('schedule-calendar-grid');

// DOM Elements - Status
const schedulerStatusText = document.getElementById('scheduler-status');
const nextEventText = document.getElementById('next-event');
const lightConditionText = document.getElementById('light-condition');
const activateSchedulerBtn = document.getElementById('activate-scheduler');
const deactivateSchedulerBtn = document.getElementById('deactivate-scheduler');
const manualWateringBtn = document.getElementById('manual-watering');

// DOM Elements - Modals
const templateModal = document.getElementById('template-modal');
const templateSelectionList = document.getElementById('template-selection-list');
const confirmTemplateSelectionBtn = document.getElementById('confirm-template-selection');
const cancelTemplateSelectionBtn = document.getElementById('cancel-template-selection');
const closeModalButtons = document.querySelectorAll('.close-modal');

const manualWateringModal = document.getElementById('manual-watering-modal');
const manualWateringRelaySelect = document.getElementById('manual-watering-relay');
const manualWateringDurationInput = document.getElementById('manual-watering-duration');
const startManualWateringBtn = document.getElementById('start-manual-watering');
const cancelManualWateringBtn = document.getElementById('cancel-manual-watering');

// Helper function to format time
function formatTime(timeString) {
  const [hours, minutes] = timeString.split(':');
  return `${hours}:${minutes}`;
}

// Helper function to format minutes as hours and minutes
function formatMinutes(minutes) {
  if (minutes < 60) {
    return `${minutes} minute${minutes !== 1 ? 's' : ''}`;
  } else {
    const hours = Math.floor(minutes / 60);
    const remainingMinutes = minutes % 60;
    if (remainingMinutes === 0) {
      return `${hours} hour${hours !== 1 ? 's' : ''}`;
    } else {
      return `${hours} hour${hours !== 1 ? 's' : ''} ${remainingMinutes} minute${remainingMinutes !== 1 ? 's' : ''}`;
    }
  }
}

// Helper function to generate a unique ID
function generateId() {
  return Date.now().toString(36) + Math.random().toString(36).substr(2);
}

// Helper function to save state to localStorage
function saveState() {
  localStorage.setItem('schedulerState', JSON.stringify(schedulerState));
  
  // Also send to server for persistent storage
  fetch('/api/scheduler/save', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(schedulerState)
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    return response.json();
  })
  .then(data => {
    console.log('Scheduler state saved to server:', data);
  })
  .catch(error => {
    console.error('Error saving scheduler state to server:', error);
  });
}

// Helper function to load state from localStorage or server
function loadState() {
  const savedState = localStorage.getItem('schedulerState');
  
  if (savedState) {
    // First load from localStorage for immediate display
    schedulerState = JSON.parse(savedState);
    updateUI();
  }
  
  // Then try to load from server (which may have more up-to-date data)
  fetch('/api/scheduler/load')
    .then(response => {
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      return response.json();
    })
    .then(data => {
      console.log('Scheduler state loaded from server:', data);
      if (data && data.lightSchedule) {
        schedulerState = data;
        updateUI();
        // Update localStorage with server data
        localStorage.setItem('schedulerState', JSON.stringify(schedulerState));
      }
    })
    .catch(error => {
      console.error('Error loading scheduler state from server:', error);
    });
}

// Update UI based on current state
function updateUI() {
  // Update Light Schedule
  lightsOnTimeInput.value = schedulerState.lightSchedule.lightsOnTime;
  lightsOffTimeInput.value = schedulerState.lightSchedule.lightsOffTime;
  
  // Update Lights On Schedules
  renderScheduleList(lightsOnSchedulesList, schedulerState.lightsOnSchedules, 'lightsOn');
  
  // Update Lights Off Schedules
  renderScheduleList(lightsOffSchedulesList, schedulerState.lightsOffSchedules, 'lightsOff');
  
  // Update Custom Events
  renderEventList(customEventsList, schedulerState.customEvents);
  
  // Update Templates
  renderTemplateList();
  
  // Update Calendar
  renderCalendar();
  
  // Update Status
  updateStatusDisplay();
}

function renderScheduleList(container, schedules, type) {
  container.innerHTML = '';
  
  if (schedules.length === 0) {
    const emptyMessage = document.createElement('div');
    emptyMessage.className = 'empty-message';
    emptyMessage.textContent = 'No schedules added yet.';
    container.appendChild(emptyMessage);
    return;
  }
  
  schedules.forEach(schedule => {
    const item = document.createElement('div');
    item.className = 'schedule-item';
    
    const details = document.createElement('div');
    details.className = 'schedule-details';
    
    const title = document.createElement('div');
    title.className = 'schedule-title';
    title.textContent = `Relay ${parseInt(schedule.relay) + 1} - Every ${formatMinutes(schedule.frequency)}`;
    
    const subtitle = document.createElement('div');
    subtitle.className = 'schedule-subtitle';
    subtitle.textContent = `Duration: ${schedule.duration} seconds`;
    
    details.appendChild(title);
    details.appendChild(subtitle);
    
    const actions = document.createElement('div');
    actions.className = 'schedule-actions';
    
    const editBtn = document.createElement('button');
    editBtn.textContent = 'Edit';
    editBtn.addEventListener('click', () => {
      editSchedule(schedule.id, type);
    });
    
    const deleteBtn = document.createElement('button');
    deleteBtn.textContent = 'Delete';
    deleteBtn.addEventListener('click', () => {
      deleteSchedule(schedule.id, type);
    });
    
    actions.appendChild(editBtn);
    actions.appendChild(deleteBtn);
    
    item.appendChild(details);
    item.appendChild(actions);
    
    container.appendChild(item);
  });
}

// Render custom event list
function renderEventList(container, events) {
  container.innerHTML = '';
  
  if (events.length === 0) {
    const emptyMessage = document.createElement('div');
    emptyMessage.className = 'empty-message';
    emptyMessage.textContent = 'No custom events added yet.';
    container.appendChild(emptyMessage);
    return;
  }
  
  events.forEach(event => {
    const item = document.createElement('div');
    item.className = 'schedule-item';
    
    const details = document.createElement('div');
    details.className = 'schedule-details';
    
    const title = document.createElement('div');
    title.className = 'schedule-title';
    title.textContent = `Relay ${parseInt(event.relay) + 1} - At ${event.time}`;
    
    const subtitle = document.createElement('div');
    subtitle.className = 'schedule-subtitle';
    subtitle.textContent = `Duration: ${event.duration} seconds`;
    
    details.appendChild(title);
    details.appendChild(subtitle);
    
    const actions = document.createElement('div');
    actions.className = 'schedule-actions';
    
    const editBtn = document.createElement('button');
    editBtn.textContent = 'Edit';
    editBtn.addEventListener('click', () => {
      editCustomEvent(event.id);
    });
    
    const deleteBtn = document.createElement('button');
    deleteBtn.textContent = 'Delete';
    deleteBtn.addEventListener('click', () => {
      deleteCustomEvent(event.id);
    });
    
    actions.appendChild(editBtn);
    actions.appendChild(deleteBtn);
    
    item.appendChild(details);
    item.appendChild(actions);
    
    container.appendChild(item);
  });
}

// Render template list
function renderTemplateList() {
  savedTemplatesList.innerHTML = '';
  
  if (schedulerState.templates.length === 0) {
    const emptyMessage = document.createElement('div');
    emptyMessage.textContent = 'No templates saved yet.';
    savedTemplatesList.appendChild(emptyMessage);
    return;
  }
  
  schedulerState.templates.forEach(template => {
    const item = document.createElement('div');
    item.className = 'template-item';
    item.textContent = template.name;
    item.dataset.id = template.id;
    
    item.addEventListener('click', () => {
      selectTemplate(template.id);
    });
    
    savedTemplatesList.appendChild(item);
  });
}

// Render template selection in modal
function renderTemplateSelection() {
  templateSelectionList.innerHTML = '';
  
  schedulerState.templates.forEach(template => {
    const item = document.createElement('div');
    item.className = 'schedule-item';
    
    const details = document.createElement('div');
    details.className = 'schedule-details';
    details.textContent = template.name;
    
    item.appendChild(details);
    item.dataset.id = template.id;
    
    item.addEventListener('click', () => {
      // Deselect all items
      document.querySelectorAll('.schedule-item').forEach(el => {
        el.classList.remove('selected');
      });
      
      // Select this item
      item.classList.add('selected');
    });
    
    templateSelectionList.appendChild(item);
  });
}

// Render calendar
function renderCalendar() {
  scheduleCalendarGrid.innerHTML = '';
  
  if (schedulerState.calendarSchedule.length === 0) {
    const emptyMessage = document.createElement('div');
    emptyMessage.textContent = 'No calendar generated yet. Use the "Generate Calendar" button to create a schedule.';
    scheduleCalendarGrid.appendChild(emptyMessage);
    return;
  }
  
  // Create header
  const header = document.createElement('div');
  header.className = 'calendar-header';
  
  const days = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  days.forEach(day => {
    const dayHeader = document.createElement('div');
    dayHeader.textContent = day;
    header.appendChild(dayHeader);
  });
  
  scheduleCalendarGrid.appendChild(header);
  
  // Create weeks
  schedulerState.calendarSchedule.forEach(week => {
    const weekRow = document.createElement('div');
    weekRow.className = 'calendar-week';
    
    week.forEach(day => {
      const dayCell = document.createElement('div');
      dayCell.className = 'calendar-day';
      
      const dateDiv = document.createElement('div');
      dateDiv.className = 'calendar-date';
      dateDiv.textContent = day.date;
      dayCell.appendChild(dateDiv);
      
      const eventsDiv = document.createElement('div');
      eventsDiv.className = 'calendar-events';
      
      day.events.forEach(event => {
        const eventDiv = document.createElement('div');
        eventDiv.className = 'calendar-event';
        eventDiv.textContent = `R${parseInt(event.relay) + 1} - ${event.time} (${event.duration}s)`;
        eventsDiv.appendChild(eventDiv);
      });
      
      dayCell.appendChild(eventsDiv);
      weekRow.appendChild(dayCell);
    });
    
    scheduleCalendarGrid.appendChild(weekRow);
  });
}

// Update status display
function updateStatusDisplay() {
  // Update scheduler status
  schedulerStatusText.textContent = schedulerState.isActive ? 'Active' : 'Inactive';
  schedulerStatusText.style.color = schedulerState.isActive ? 'var(--success-color)' : 'var(--danger-color)';
  
  // Update next event
  if (schedulerState.nextEvent) {
    nextEventText.textContent = `Relay ${parseInt(schedulerState.nextEvent.relay) + 1} at ${schedulerState.nextEvent.time}`;
  } else {
    nextEventText.textContent = 'None scheduled';
  }
  
  // Update light condition
  lightConditionText.textContent = schedulerState.currentLightCondition;
  lightConditionText.style.color = 
    schedulerState.currentLightCondition === 'Lights On' ? 'var(--success-color)' : 
    schedulerState.currentLightCondition === 'Lights Off' ? 'var(--secondary-color)' : 
    'var(--text-color)';
  
  // Update buttons based on active state
  if (schedulerState.isActive) {
    activateSchedulerBtn.style.display = 'none';
    deactivateSchedulerBtn.style.display = 'inline-block';
  } else {
    activateSchedulerBtn.style.display = 'inline-block';
    deactivateSchedulerBtn.style.display = 'none';
  }
}

// Add a lights on schedule
function addLightsOnSchedule() {
  const frequency = parseInt(lightsOnFrequencyInput.value) * parseInt(lightsOnFrequencyUnitSelect.value);
  const duration = parseInt(lightsOnDurationInput.value);
  const relay = lightsOnRelaySelect.value;
  
  // Validate inputs
  if (isNaN(frequency) || frequency < 1 || frequency > 1440) {
    alert('Invalid frequency. Please enter a value between 1 and 1440 minutes.');
    return;
  }
  
  if (isNaN(duration) || duration < 10 || duration > frequency * 60) {
    alert(`Invalid duration. Please enter a value between 10 and ${frequency * 60} seconds.`);
    return;
  }
  
  const newSchedule = {
    id: generateId(),
    frequency,
    duration,
    relay
  };
  
  schedulerState.lightsOnSchedules.push(newSchedule);
  saveState();
  renderScheduleList(lightsOnSchedulesList, schedulerState.lightsOnSchedules, 'lightsOn');
}

// Add a lights off schedule
function addLightsOffSchedule() {
  const frequency = parseInt(lightsOffFrequencyInput.value) * parseInt(lightsOffFrequencyUnitSelect.value);
  const duration = parseInt(lightsOffDurationInput.value);
  const relay = lightsOffRelaySelect.value;
  
  // Validate inputs
  if (isNaN(frequency) || frequency < 1 || frequency > 1440) {
    alert('Invalid frequency. Please enter a value between 1 and 1440 minutes.');
    return;
  }
  
  if (isNaN(duration) || duration < 10 || duration > frequency * 60) {
    alert(`Invalid duration. Please enter a value between 10 and ${frequency * 60} seconds.`);
    return;
  }
  
  const newSchedule = {
    id: generateId(),
    frequency,
    duration,
    relay
  };
  
  schedulerState.lightsOffSchedules.push(newSchedule);
  saveState();
  renderScheduleList(lightsOffSchedulesList, schedulerState.lightsOffSchedules, 'lightsOff');
}

// Add a custom event
function addCustomEvent() {
  const time = customEventTimeInput.value;
  const duration = parseInt(customEventDurationInput.value);
  const relay = customEventRelaySelect.value;
  
  // Validate inputs
  if (!time) {
    alert('Please select a time for the event.');
    return;
  }
  
  if (isNaN(duration) || duration < 10 || duration > 3600) {
    alert('Invalid duration. Please enter a value between 10 and 3600 seconds.');
    return;
  }
  
  const newEvent = {
    id: generateId(),
    time,
    duration,
    relay
  };
  
  schedulerState.customEvents.push(newEvent);
  saveState();
  renderEventList(customEventsList, schedulerState.customEvents);
}

// Edit a schedule
function editSchedule(id, type) {
  let schedule;
  let scheduleArray;
  let frequencyInput;
  let frequencyUnitSelect;
  let durationInput;
  let relaySelect;
  
  if (type === 'lightsOn') {
    scheduleArray = schedulerState.lightsOnSchedules;
    frequencyInput = lightsOnFrequencyInput;
    frequencyUnitSelect = lightsOnFrequencyUnitSelect;
    durationInput = lightsOnDurationInput;
    relaySelect = lightsOnRelaySelect;
  } else {
    scheduleArray = schedulerState.lightsOffSchedules;
    frequencyInput = lightsOffFrequencyInput;
    frequencyUnitSelect = lightsOffFrequencyUnitSelect;
    durationInput = lightsOffDurationInput;
    relaySelect = lightsOffRelaySelect;
  }
  
  schedule = scheduleArray.find(s => s.id === id);
  
  if (!schedule) {
    console.error(`Schedule with ID ${id} not found`);
    return;
  }
  
  // Determine appropriate frequency unit (minutes or hours)
  if (schedule.frequency < 60) {
    frequencyInput.value = schedule.frequency;
    frequencyUnitSelect.value = '1'; // minutes
  } else if (schedule.frequency % 60 === 0) {
    frequencyInput.value = schedule.frequency / 60;
    frequencyUnitSelect.value = '60'; // hours
  } else {
    frequencyInput.value = schedule.frequency;
    frequencyUnitSelect.value = '1'; // minutes
  }
  
  durationInput.value = schedule.duration;
  relaySelect.value = schedule.relay;
  
  // Delete the old schedule
  deleteSchedule(id, type, false); // false = don't refresh UI yet
  
  // Switch to the appropriate tab
  switchTab(type === 'lightsOn' ? 'lights-on' : 'lights-off');
}

// Delete a schedule
function deleteSchedule(id, type, refresh = true) {
  if (type === 'lightsOn') {
    schedulerState.lightsOnSchedules = schedulerState.lightsOnSchedules.filter(s => s.id !== id);
    if (refresh) {
      renderScheduleList(lightsOnSchedulesList, schedulerState.lightsOnSchedules, 'lightsOn');
    }
  } else {
    schedulerState.lightsOffSchedules = schedulerState.lightsOffSchedules.filter(s => s.id !== id);
    if (refresh) {
      renderScheduleList(lightsOffSchedulesList, schedulerState.lightsOffSchedules, 'lightsOff');
    }
  }
  
  saveState();
}

// Edit a custom event
function editCustomEvent(id) {
  const event = schedulerState.customEvents.find(e => e.id === id);
  
  if (!event) {
    console.error(`Event with ID ${id} not found`);
    return;
  }
  
  customEventTimeInput.value = event.time;
  customEventDurationInput.value = event.duration;
  customEventRelaySelect.value = event.relay;
  
  // Delete the old event
  deleteCustomEvent(id, false); // false = don't refresh UI yet
  
  // Switch to the custom events tab
  switchTab('custom-events');
}

// Delete a custom event
function deleteCustomEvent(id, refresh = true) {
  schedulerState.customEvents = schedulerState.customEvents.filter(e => e.id !== id);
  
  if (refresh) {
    renderEventList(customEventsList, schedulerState.customEvents);
  }
  
  saveState();
}

// Save current schedule as template
function saveAsTemplate() {
  const name = templateNameInput.value.trim();
  
  if (!name) {
    alert('Please enter a name for the template.');
    return;
  }
  
  // Check if template with this name already exists
  const existingTemplate = schedulerState.templates.find(t => t.name === name);
  if (existingTemplate) {
    if (!confirm(`A template with the name "${name}" already exists. Do you want to replace it?`)) {
      return;
    }
    
    // Remove existing template
    schedulerState.templates = schedulerState.templates.filter(t => t.name !== name);
  }
  
  const template = {
    id: generateId(),
    name,
    lightSchedule: { ...schedulerState.lightSchedule },
    lightsOnSchedules: [...schedulerState.lightsOnSchedules],
    lightsOffSchedules: [...schedulerState.lightsOffSchedules],
    customEvents: [...schedulerState.customEvents]
  };
  
  schedulerState.templates.push(template);
  saveState();
  renderTemplateList();
  
  templateNameInput.value = '';
  alert(`Template "${name}" has been saved.`);
}

// Select a template (for highlighting in the list)
function selectTemplate(id) {
  const templateItems = document.querySelectorAll('.template-item');
  
  templateItems.forEach(item => {
    if (item.dataset.id === id) {
      item.classList.add('selected');
    } else {
      item.classList.remove('selected');
    }
  });
}

// Load selected template
function loadTemplate() {
  if (schedulerState.templates.length === 0) {
    alert('No templates available.');
    return;
  }
  
  // Show template selection modal
  templateModal.style.display = 'block';
  renderTemplateSelection();
}

// Confirm template selection
function confirmTemplateSelection() {
  const selectedItem = document.querySelector('.schedule-item.selected');
  
  if (!selectedItem) {
    alert('Please select a template to load.');
    return;
  }
  
  const templateId = selectedItem.dataset.id;
  const template = schedulerState.templates.find(t => t.id === templateId);
  
  if (!template) {
    console.error(`Template with ID ${templateId} not found`);
    return;
  }
  
  if (!confirm(`Are you sure you want to load the template "${template.name}"? This will replace your current schedules.`)) {
    return;
  }
  
  // Load template data
  schedulerState.lightSchedule = { ...template.lightSchedule };
  schedulerState.lightsOnSchedules = [...template.lightsOnSchedules];
  schedulerState.lightsOffSchedules = [...template.lightsOffSchedules];
  schedulerState.customEvents = [...template.customEvents];
  
  saveState();
  updateUI();
  
  // Close modal
  templateModal.style.display = 'none';
  
  alert(`Template "${template.name}" has been loaded.`);
}

// Delete selected template
function deleteTemplate() {
  const selectedItems = document.querySelectorAll('.template-item.selected');
  
  if (selectedItems.length === 0) {
    alert('Please select a template to delete.');
    return;
  }
  
  const templateId = selectedItems[0].dataset.id;
  const template = schedulerState.templates.find(t => t.id === templateId);
  
  if (!template) {
    console.error(`Template with ID ${templateId} not found`);
    return;
  }
  
  if (!confirm(`Are you sure you want to delete the template "${template.name}"?`)) {
    return;
  }
  
  schedulerState.templates = schedulerState.templates.filter(t => t.id !== templateId);
  saveState();
  renderTemplateList();
  
  alert(`Template "${template.name}" has been deleted.`);
}

// Generate calendar
function generateCalendar() {
  const weeks = parseInt(calendarWeeksInput.value);
  
  if (isNaN(weeks) || weeks < 1 || weeks > 12) {
    alert('Please enter a valid number of weeks (1-12).');
    return;
  }
  
  // Create calendar structure
  const calendar = [];
  const startDate = new Date();
  startDate.setHours(0, 0, 0, 0); // Start from today at midnight
  
  for (let week = 0; week < weeks; week++) {
    const weekDays = [];
    
    for (let day = 0; day < 7; day++) {
      const currentDate = new Date(startDate);
      currentDate.setDate(startDate.getDate() + (week * 7) + day);
      
      const events = [];
      
      // Add all custom events for this day
      schedulerState.customEvents.forEach(event => {
        events.push({
          time: event.time,
          duration: event.duration,
          relay: event.relay,
          type: 'custom'
        });
      });
      
      // Add periodic events based on light schedule
      // First determine if it's a lights-on or lights-off period
      const dateStr = currentDate.toISOString().split('T')[0];
      const lightsOnTime = new Date(`${dateStr}T${schedulerState.lightSchedule.lightsOnTime}:00`);
      const lightsOffTime = new Date(`${dateStr}T${schedulerState.lightSchedule.lightsOffTime}:00`);
      
      // Add lights-on schedules
      schedulerState.lightsOnSchedules.forEach(schedule => {
        const frequencyInMinutes = schedule.frequency;
        const minutesInLightPeriod = (lightsOffTime - lightsOnTime) / (1000 * 60);
        
        for (let minute = 0; minute < minutesInLightPeriod; minute += frequencyInMinutes) {
          const eventTime = new Date(lightsOnTime);
          eventTime.setMinutes(lightsOnTime.getMinutes() + minute);
          
          events.push({
            time: eventTime.toTimeString().substr(0, 5),
            duration: schedule.duration,
            relay: schedule.relay,
            type: 'lightsOn'
          });
        }
      });
      
      // Add lights-off schedules
      schedulerState.lightsOffSchedules.forEach(schedule => {
        const frequencyInMinutes = schedule.frequency;
        // Calculate minutes from lights-off to midnight
        const minutesToMidnight = (24 * 60) - ((lightsOffTime.getHours() * 60) + lightsOffTime.getMinutes());
        // And minutes from midnight to lights-on
        const minutesFromMidnight = (lightsOnTime.getHours() * 60) + lightsOnTime.getMinutes();
        
        // Handle lights-off to midnight
        for (let minute = 0; minute < minutesToMidnight; minute += frequencyInMinutes) {
          const eventTime = new Date(lightsOffTime);
          eventTime.setMinutes(lightsOffTime.getMinutes() + minute);
          
          events.push({
            time: eventTime.toTimeString().substr(0, 5),
            duration: schedule.duration,
            relay: schedule.relay,
            type: 'lightsOff'
          });
        }
        
        // Handle midnight to lights-on
        for (let minute = frequencyInMinutes - (minutesToMidnight % frequencyInMinutes); 
             minute < minutesFromMidnight; 
             minute += frequencyInMinutes) {
          const eventTime = new Date(currentDate);
          eventTime.setHours(0, minute, 0, 0);
          
          events.push({
            time: eventTime.toTimeString().substr(0, 5),
            duration: schedule.duration,
            relay: schedule.relay,
            type: 'lightsOff'
          });
        }
      });
      
      // Sort events by time
      events.sort((a, b) => {
        return a.time.localeCompare(b.time);
      });
      
      weekDays.push({
        date: currentDate.toLocaleDateString(),
        day: currentDate.getDay(),
        events
      });
    }
    
    calendar.push(weekDays);
  }
  
  schedulerState.calendarSchedule = calendar;
  saveState();
  renderCalendar();
}

// Activate scheduler
function activateScheduler() {
  fetch('/api/scheduler/activate', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    return response.json();
  })
  .then(data => {
    console.log('Scheduler activated:', data);
    schedulerState.isActive = true;
    updateStatusDisplay();
    saveState();
  })
  .catch(error => {
    console.error('Error activating scheduler:', error);
    alert('Failed to activate scheduler. Please check console for details.');
  });
}

// Deactivate scheduler
function deactivateScheduler() {
  fetch('/api/scheduler/deactivate', {
    method: 'POST'
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    return response.json();
  })
  .then(data => {
    console.log('Scheduler deactivated:', data);
    schedulerState.isActive = false;
    updateStatusDisplay();
    saveState();
  })
  .catch(error => {
    console.error('Error deactivating scheduler:', error);
    alert('Failed to deactivate scheduler. Please check console for details.');
  });
}

// Show manual watering modal
function showManualWateringModal() {
  manualWateringModal.style.display = 'block';
}

// Start manual watering
function startManualWatering() {
  const relay = parseInt(manualWateringRelaySelect.value);
  const duration = parseInt(manualWateringDurationInput.value);
  
  // Validate inputs
  if (isNaN(duration) || duration < 5 || duration > 300) {
    alert('Invalid duration. Please enter a value between 5 and 300 seconds.');
    return;
  }
  
  fetch('/api/relay/manual', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      relay,
      duration
    })
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    return response.json();
  })
  .then(data => {
    console.log('Manual watering started:', data);
    manualWateringModal.style.display = 'none';
    alert(`Manual watering started on Relay ${relay + 1} for ${duration} seconds.`);
  })
  .catch(error => {
    console.error('Error starting manual watering:', error);
    alert('Failed to start manual watering. Please check console for details.');
  });
}

// Switch tab
function switchTab(tabId) {
  // Deactivate all tabs
  tabButtons.forEach(button => {
    button.classList.remove('active');
  });
  
  tabContents.forEach(content => {
    content.classList.remove('active');
  });
  
  // Activate selected tab
  document.querySelector(`[data-tab="${tabId}"]`).classList.add('active');
  document.getElementById(`${tabId}-tab`).classList.add('active');
}

// Save light schedule
function saveLightSchedule() {
  const lightsOnTime = lightsOnTimeInput.value;
  const lightsOffTime = lightsOffTimeInput.value;
  
  // Validate inputs
  if (!lightsOnTime || !lightsOffTime) {
    alert('Please set both lights on and lights off times.');
    return;
  }
  
  schedulerState.lightSchedule = {
    lightsOnTime,
    lightsOffTime
  };
  
  saveState();
  alert('Light schedule saved.');
}

// Check scheduler status
function checkSchedulerStatus() {
  fetch('/api/scheduler/status')
    .then(response => {
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      return response.json();
    })
    .then(data => {
      console.log('Scheduler status:', data);
      
      schedulerState.isActive = data.isActive;
      schedulerState.currentLightCondition = data.lightCondition;
      
      if (data.nextEvent) {
        schedulerState.nextEvent = data.nextEvent;
      }
      
      updateStatusDisplay();
    })
    .catch(error => {
      console.error('Error checking scheduler status:', error);
    });
}

// Close modal
function closeModal() {
  templateModal.style.display = 'none';
  manualWateringModal.style.display = 'none';
}

// Event Listeners
function addEventListeners() {
  // Light schedule
  saveLightScheduleBtn.addEventListener('click', saveLightSchedule);
  
  // Tabs
  tabButtons.forEach(button => {
    button.addEventListener('click', () => {
      switchTab(button.dataset.tab);
    });
  });
  
  // Lights On schedules
  addLightsOnScheduleBtn.addEventListener('click', addLightsOnSchedule);
  
  // Lights Off schedules
  addLightsOffScheduleBtn.addEventListener('click', addLightsOffSchedule);
  
  // Custom events
  addCustomEventBtn.addEventListener('click', addCustomEvent);
  
  // Templates
  saveTemplateBtn.addEventListener('click', saveAsTemplate);
  loadTemplateBtn.addEventListener('click', loadTemplate);
  deleteTemplateBtn.addEventListener('click', deleteTemplate);
  
  // Calendar
  generateCalendarBtn.addEventListener('click', generateCalendar);
  
  // Scheduler controls
  activateSchedulerBtn.addEventListener('click', activateScheduler);
  deactivateSchedulerBtn.addEventListener('click', deactivateScheduler);
  manualWateringBtn.addEventListener('click', showManualWateringModal);
  
  // Modal
  confirmTemplateSelectionBtn.addEventListener('click', confirmTemplateSelection);
  cancelTemplateSelectionBtn.addEventListener('click', closeModal);
  closeModalButtons.forEach(button => {
    button.addEventListener('click', closeModal);
  });
  
  // Manual watering
  startManualWateringBtn.addEventListener('click', startManualWatering);
  cancelManualWateringBtn.addEventListener('click', closeModal);
  
  // Close modal on clicking outside
  window.addEventListener('click', (event) => {
    if (event.target === templateModal) {
      templateModal.style.display = 'none';
    }
    
    if (event.target === manualWateringModal) {
      manualWateringModal.style.display = 'none';
    }
  });
}

// Initialize
function init() {
  addEventListeners();
  loadState();
  
  // Check scheduler status every 10 seconds
  setInterval(checkSchedulerStatus, 10000);
}

// Start app when DOM is loaded
document.addEventListener('DOMContentLoaded', init);
