/**
 * ES32A08 Controller Dashboard JavaScript
 */

// Debug helper functions
function debugPrintln(msg) {
  console.log("[DEBUG] " + msg);
}
function debugPrintf(format, ...args) {
  console.log("[DEBUG] " + format, ...args);
}

// Elements – these will be assigned in createUIElements()
let relayGrid, buttonGrid, inputGrid, currentInputs, voltageInputs, allOnButton, allOffButton;

// Time elements
let currentTimeDisplay, firstSyncTimeDisplay, timezoneSelector, setTimezoneButton;

// Filesystem elements
let totalSpaceDisplay, usedSpaceDisplay, freeSpaceDisplay, fileCountDisplay, fileListDisplay, refreshFsButton;

// IO state
let ioState = {
  relays: [],
  buttons: [],
  inputs: [],
  currentInputs: [],
  voltageInputs: []
};

// Create UI elements and assign global element variables
function createUIElements() {
  debugPrintln("Creating UI elements...");
  
  // Time UI elements
  currentTimeDisplay = document.getElementById('current-time');
  firstSyncTimeDisplay = document.getElementById('first-sync-time');
  timezoneSelector = document.getElementById('timezone-selector');
  setTimezoneButton = document.getElementById('set-timezone');
  
  // Filesystem UI elements
  totalSpaceDisplay = document.getElementById('total-space');
  usedSpaceDisplay = document.getElementById('used-space');
  freeSpaceDisplay = document.getElementById('free-space');
  fileCountDisplay = document.getElementById('file-count');
  fileListDisplay = document.getElementById('file-list');
  refreshFsButton = document.getElementById('refresh-fs-info');
  
  // Original UI elements
  relayGrid = document.querySelector('.relay-grid');
  buttonGrid = document.querySelector('.button-grid');
  inputGrid = document.querySelector('.input-grid');
  currentInputs = document.getElementById('current-inputs');
  voltageInputs = document.getElementById('voltage-inputs');
  allOnButton = document.getElementById('all-on');
  allOffButton = document.getElementById('all-off');
  
  if (!relayGrid || !buttonGrid || !inputGrid || !currentInputs || !voltageInputs) {
    debugPrintln("One or more UI container elements were not found.");
  }
  
  // Create relay toggles (8 relays)
  for (let i = 0; i < 8; i++) {
    const relayElement = document.createElement('div');
    relayElement.className = 'indicator';
    relayElement.innerHTML = `
      <div class="label">Relay ${i + 1}</div>
      <label class="switch">
        <input type="checkbox" data-relay="${i}" class="relay-toggle">
        <span class="slider"></span>
      </label>
    `;
    relayGrid && relayGrid.appendChild(relayElement);
  }
  
  // Create button indicators (4 buttons)
  for (let i = 0; i < 4; i++) {
    const buttonElement = document.createElement('div');
    buttonElement.className = 'indicator';
    buttonElement.innerHTML = `
      <div class="label">Button ${i + 1}</div>
      <div class="status">
        <span class="status-indicator" id="button-${i}"></span>
        <span class="value" id="button-value-${i}">OFF</span>
      </div>
    `;
    buttonGrid && buttonGrid.appendChild(buttonElement);
  }
  
  // Create input indicators (8 inputs)
  for (let i = 0; i < 8; i++) {
    const inputElement = document.createElement('div');
    inputElement.className = 'indicator';
    inputElement.innerHTML = `
      <div class="label">Input ${i + 1}</div>
      <div class="status">
        <span class="status-indicator" id="input-${i}"></span>
        <span class="value" id="input-value-${i}">OFF</span>
      </div>
    `;
    inputGrid && inputGrid.appendChild(inputElement);
  }
  
  // Create voltage input indicators (4 channels)
  for (let i = 0; i < 4; i++) {
    const voltageElement = document.createElement('div');
    voltageElement.className = 'indicator';
    voltageElement.innerHTML = `
      <div class="label">Channel ${i + 1}</div>
      <div class="value" id="voltage-value-${i}">0.00 V</div>
    `;
    voltageInputs && voltageInputs.appendChild(voltageElement);
  }
  
  // Create current input indicators (4 channels)
  for (let i = 0; i < 4; i++) {
    const currentElement = document.createElement('div');
    currentElement.className = 'indicator';
    currentElement.innerHTML = `
      <div class="label">Channel ${i + 1}</div>
      <div class="value" id="current-value-${i}">0.00 mA</div>
    `;
    currentInputs && currentInputs.appendChild(currentElement);
  }
  debugPrintln("UI elements created.");
}

// Fetch IO status from the API and update the UI
async function fetchIOStatus() {
  try {
    const response = await fetch('/api/io/status');
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    const data = await response.json();
    debugPrintf("IO Status received: %o", data);
    updateUIState(data);
  } catch (error) {
    debugPrintln("Error fetching IO status: " + error);
    console.error('Error fetching IO status:', error);
  }
}

// Fetch time status from the API and update the time UI
async function fetchTimeStatus() {
  try {
    const response = await fetch('/api/time/status');
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    const data = await response.json();
    debugPrintf("Time Status received: %o", data);
    updateTimeUI(data);
  } catch (error) {
    debugPrintln("Error fetching time status: " + error);
    console.error('Error fetching time status:', error);
  }
}

// Fetch filesystem information and update the UI
async function fetchFilesystemInfo() {
  try {
    const response = await fetch('/api/fs/info');
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    const data = await response.json();
    debugPrintf("Filesystem info received: %o", data);
    updateFilesystemUI(data);
  } catch (error) {
    debugPrintln("Error fetching filesystem info: " + error);
    console.error('Error fetching filesystem info:', error);
    
    // Show error in UI
    if (totalSpaceDisplay) totalSpaceDisplay.textContent = 'Error fetching data';
    if (usedSpaceDisplay) usedSpaceDisplay.textContent = 'Error fetching data';
    if (freeSpaceDisplay) freeSpaceDisplay.textContent = 'Error fetching data';
    if (fileCountDisplay) fileCountDisplay.textContent = 'Error fetching data';
    if (fileListDisplay) fileListDisplay.innerHTML = '<p>Error fetching file list</p>';
  }
}

// Update Filesystem UI with the received data
function updateFilesystemUI(data) {
  if (!data) return;
  
  // Format numbers with commas and appropriate units
  const formatBytes = (bytes) => {
    if (bytes < 1024) return bytes + ' bytes';
    else if (bytes < 1048576) return (bytes / 1024).toFixed(2) + ' KB';
    else return (bytes / 1048576).toFixed(2) + ' MB';
  };
  
  if (totalSpaceDisplay) {
    totalSpaceDisplay.textContent = formatBytes(data.totalBytes);
  }
  
  if (usedSpaceDisplay) {
    usedSpaceDisplay.textContent = formatBytes(data.usedBytes);
  }
  
  if (freeSpaceDisplay) {
    freeSpaceDisplay.textContent = formatBytes(data.freeBytes);
  }
  
  if (fileCountDisplay) {
    fileCountDisplay.textContent = data.fileCount + ' files';
  }
  
  if (fileListDisplay) {
    if (data.files && data.files.length > 0) {
      let fileListHTML = '<ul class="file-item-list">';
      data.files.forEach(file => {
        fileListHTML += `<li class="file-item">
          <span class="file-name">${file.name}</span>
          <span class="file-size">${formatBytes(file.size)}</span>
        </li>`;
      });
      fileListHTML += '</ul>';
      fileListDisplay.innerHTML = fileListHTML;
    } else {
      fileListDisplay.innerHTML = '<p>No files found</p>';
    }
  }
}

// Update Time UI with the received data
function updateTimeUI(data) {
  if (currentTimeDisplay) {
    currentTimeDisplay.textContent = data.currentTime || 'Unknown';
  }
  
  if (firstSyncTimeDisplay) {
    firstSyncTimeDisplay.textContent = data.firstSyncTime || 'Not synchronized yet';
  }
  
  if (timezoneSelector && data.timezone) {
    // Find and select the appropriate option
    const options = timezoneSelector.options;
    let found = false;
    
    for (let i = 0; i < options.length; i++) {
      if (options[i].value === data.timezone) {
        timezoneSelector.selectedIndex = i;
        found = true;
        break;
      }
    }
    
    // If not found in the dropdown, add a new option
    if (!found) {
      const option = document.createElement('option');
      option.value = data.timezone;
      option.textContent = `Custom (${data.timezone})`;
      timezoneSelector.appendChild(option);
      timezoneSelector.selectedIndex = timezoneSelector.options.length - 1;
    }
  }
}

// Set timezone via API
async function setTimezone(timezone) {
  try {
    const response = await fetch('/api/time/timezone', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ timezone: timezone })
    });
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    debugPrintln("Timezone set response: " + JSON.stringify(data));
    
    // Refresh time status
    fetchTimeStatus();
    
    return data.status === 'success';
  } catch (error) {
    debugPrintln("Error setting timezone: " + error);
    console.error('Error setting timezone:', error);
    return false;
  }
}

// Update UI with new IO state data
function updateUIState(data) {
  ioState = data;
  
  // Update relay toggles
  if (ioState.relays && ioState.relays.length > 0) {
    ioState.relays.forEach(relay => {
      const toggle = document.querySelector(`.relay-toggle[data-relay="${relay.id}"]`);
      if (toggle) {
        toggle.checked = relay.state;
      }
    });
  }
  
  // Update button indicators
  if (ioState.buttons && ioState.buttons.length > 0) {
    ioState.buttons.forEach(button => {
      const indicator = document.getElementById(`button-${button.id}`);
      const value = document.getElementById(`button-value-${button.id}`);
      if (indicator && value) {
        if (button.state) {
          indicator.className = 'status-indicator status-on';
          value.textContent = 'ON';
        } else {
          indicator.className = 'status-indicator status-off';
          value.textContent = 'OFF';
        }
      }
    });
  }
  
  // Update input indicators
  if (ioState.inputs && ioState.inputs.length > 0) {
    ioState.inputs.forEach(input => {
      const indicator = document.getElementById(`input-${input.id}`);
      const value = document.getElementById(`input-value-${input.id}`);
      if (indicator && value) {
        if (input.state) {
          indicator.className = 'status-indicator status-on';
          value.textContent = 'ON';
        } else {
          indicator.className = 'status-indicator status-off';
          value.textContent = 'OFF';
        }
      }
    });
  }
  
  // Update voltage inputs
  if (ioState.voltageInputs && ioState.voltageInputs.length > 0) {
    ioState.voltageInputs.forEach(input => {
      const value = document.getElementById(`voltage-value-${input.id}`);
      if (value) {
        value.textContent = `${input.value.toFixed(2)} V`;
      } else {
        debugPrintln(`Element voltage-value-${input.id} not found`);
      }
    });
  } else {
    debugPrintln("No voltage inputs data available");
  }
  
  // Update current inputs
  if (ioState.currentInputs && ioState.currentInputs.length > 0) {
    ioState.currentInputs.forEach(input => {
      const value = document.getElementById(`current-value-${input.id}`);
      if (value) {
        value.textContent = `${input.value.toFixed(2)} mA`;
      } else {
        debugPrintln(`Element current-value-${input.id} not found`);
      }
    });
  } else {
    debugPrintln("No current inputs data available");
  }
}

// Set a single relay state via API
async function setRelay(relay, state) {
  try {
    const response = await fetch('/api/io/relay', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ relay: relay, state: state })
    });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    fetchIOStatus();
  } catch (error) {
    debugPrintln("Error setting relay: " + error);
    console.error('Error setting relay:', error);
  }
}

// Set all relay states via API
async function setAllRelays(states) {
  try {
    const response = await fetch('/api/io/relays', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ states: states })
    });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    fetchIOStatus();
  } catch (error) {
    debugPrintln("Error setting all relays: " + error);
    console.error('Error setting all relays:', error);
  }
}

// Add event listeners for relay toggles, all-on/off buttons, and timezone
function addEventListeners() {
  debugPrintln("Adding event listeners...");
  
  // Relay controls event listeners
  if (relayGrid) {
    relayGrid.addEventListener('change', (event) => {
      if (event.target.classList.contains('relay-toggle')) {
        const relay = parseInt(event.target.dataset.relay);
        const state = event.target.checked;
        debugPrintf("Relay %d toggled to %s", relay, state ? "ON" : "OFF");
        setRelay(relay, state);
      }
    });
  }
  
  if (allOnButton) {
    allOnButton.addEventListener('click', () => {
      debugPrintln("All ON button clicked");
      setAllRelays([true, true, true, true, true, true, true, true]);
    });
  }
  
  if (allOffButton) {
    allOffButton.addEventListener('click', () => {
      debugPrintln("All OFF button clicked");
      setAllRelays([false, false, false, false, false, false, false, false]);
    });
  }
  
  // Timezone button event listener
  if (setTimezoneButton && timezoneSelector) {
    setTimezoneButton.addEventListener('click', () => {
      const selectedTimezone = timezoneSelector.value;
      debugPrintln(`Set timezone button clicked with timezone: ${selectedTimezone}`);
      setTimezone(selectedTimezone);
    });
  }
  
  // Filesystem refresh button event listener
  if (refreshFsButton) {
    refreshFsButton.addEventListener('click', () => {
      debugPrintln("Refresh filesystem info button clicked");
      fetchFilesystemInfo();
    });
  }
  
  debugPrintln("Event listeners added.");
}

// Initialize the dashboard: create UI, attach listeners, and start status updates
function initDashboard() {
  debugPrintln("Initializing dashboard...");
  createUIElements();
  addEventListeners();
  
  // Fetch initial status
  fetchIOStatus();
  fetchTimeStatus();
  fetchFilesystemInfo();
  
  // Update IO status every 500ms
  setInterval(fetchIOStatus, 500);
  
  // Update time status every 5 seconds
  setInterval(fetchTimeStatus, 5000);
  
  // Update filesystem info every 30 seconds
  setInterval(fetchFilesystemInfo, 30000);
}

// Start dashboard initialization when DOM is ready
document.addEventListener('DOMContentLoaded', initDashboard);