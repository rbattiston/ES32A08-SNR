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

// Add event listeners for relay toggles and all-on/off buttons
function addEventListeners() {
  debugPrintln("Adding event listeners...");
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
  debugPrintln("Event listeners added.");
}

// Initialize the dashboard: create UI, attach listeners, and start status updates
function initDashboard() {
  debugPrintln("Initializing dashboard...");
  createUIElements();
  addEventListeners();
  fetchIOStatus();
  // Update IO status every 500ms
  setInterval(fetchIOStatus, 500);
}

// Start dashboard initialization when DOM is ready
document.addEventListener('DOMContentLoaded', initDashboard);
