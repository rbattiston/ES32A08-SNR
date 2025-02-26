/**
 * ES32A08 Controller Dashboard JavaScript
 */

// Elements
const relayGrid = document.querySelector('.relay-grid');
const buttonGrid = document.querySelector('.button-grid');
const inputGrid = document.querySelector('.input-grid');
const currentInputs = document.getElementById('current-inputs');
const voltageInputs = document.getElementById('voltage-inputs');
const allOnButton = document.getElementById('all-on');
const allOffButton = document.getElementById('all-off');

// State
let ioState = {
  relays: [],
  buttons: [],
  inputs: [],
  currentInputs: [],
  voltageInputs: []
};

// Create UI elements
function createUIElements() {
  // Create relay toggles
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
    relayGrid.appendChild(relayElement);
  }

  // Create button indicators
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
    buttonGrid.appendChild(buttonElement);
  }

  // Create input indicators
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
    inputGrid.appendChild(inputElement);
  }

  // Create voltage input indicators
  for (let i = 0; i < 4; i++) {
    const voltageElement = document.createElement('div');
    voltageElement.className = 'indicator';
    voltageElement.innerHTML = `
      <div class="label">Channel ${i + 1}</div>
      <div class="value" id="voltage-value-${i}">0.00 V</div>
    `;
    voltageInputs.appendChild(voltageElement);
  }
  
  // Create current input indicators
  for (let i = 0; i < 4; i++) {
    const currentElement = document.createElement('div');
    currentElement.className = 'indicator';
    currentElement.innerHTML = `
      <div class="label">Channel ${i + 1}</div>
      <div class="value" id="current-value-${i}">0.00 mA</div>
    `;
    currentInputs.appendChild(currentElement);
  }
}

// Fetch IO status from the API
async function fetchIOStatus() {
  try {
    const response = await fetch('/api/io/status');
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    updateUIState(data);
  } catch (error) {
    console.error('Error fetching IO status:', error);
  }
}

// Update UI with new state
function updateUIState(data) {
  // Update internal state
  ioState = data;
  
  // Update relay toggles
  ioState.relays.forEach(relay => {
    const toggle = document.querySelector(`.relay-toggle[data-relay="${relay.id}"]`);
    if (toggle) {
      toggle.checked = relay.state;
    }
  });
  
  // Update button indicators
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
  
  // Update input indicators
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
  
  // Update voltage inputs
  ioState.voltageInputs.forEach(input => {
    const value = document.getElementById(`voltage-value-${input.id}`);
    
    if (value) {
      value.textContent = `${input.value.toFixed(2)} V`;
    }
  });
  
  // Update current inputs
  ioState.currentInputs.forEach(input => {
    const value = document.getElementById(`current-value-${input.id}`);
    
    if (value) {
      value.textContent = `${input.value.toFixed(2)} mA`;
    }
  });
}

// Set a single relay state
async function setRelay(relay, state) {
  try {
    const response = await fetch('/api/io/relay', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        relay: relay,
        state: state
      })
    });
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    // Refetch status to update UI
    fetchIOStatus();
  } catch (error) {
    console.error('Error setting relay:', error);
  }
}

// Set all relay states
async function setAllRelays(states) {
  try {
    const response = await fetch('/api/io/relays', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        states: states
      })
    });
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    // Refetch status to update UI
    fetchIOStatus();
  } catch (error) {
    console.error('Error setting all relays:', error);
  }
}

// Add event listeners
function addEventListeners() {
  // Relay toggle events
  relayGrid.addEventListener('change', (event) => {
    if (event.target.classList.contains('relay-toggle')) {
      const relay = parseInt(event.target.dataset.relay);
      const state = event.target.checked;
      setRelay(relay, state);
    }
  });
  
  // All relays on/off events
  allOnButton.addEventListener('click', () => {
    setAllRelays([true, true, true, true, true, true, true, true]);
  });
  
  allOffButton.addEventListener('click', () => {
    setAllRelays([false, false, false, false, false, false, false, false]);
  });
}

// Initialize the dashboard
function initDashboard() {
  createUIElements();
  addEventListeners();
  fetchIOStatus();
  
  // Periodically update IO status
  setInterval(fetchIOStatus, 500); // Update every 500ms
}

// Start everything when the DOM is loaded
document.addEventListener('DOMContentLoaded', initDashboard);