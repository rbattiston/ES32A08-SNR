/**
 * ES32A08 WiFi Configuration JavaScript
 */

// DOM Elements
const wifiEnabledToggle = document.getElementById('wifi-enabled');
const wifiSsidInput = document.getElementById('wifi-ssid');
const wifiPasswordInput = document.getElementById('wifi-password');
const showPasswordButton = document.getElementById('show-password');
const saveWifiConfigButton = document.getElementById('save-wifi-config');
const testConnectionButton = document.getElementById('test-connection');
const statusMessage = document.getElementById('status-message');
const staStatusElement = document.getElementById('sta-status');
const staIpElement = document.getElementById('sta-ip');
const timeSyncStatusElement = document.getElementById('time-sync-status');

// Show/hide password functionality
showPasswordButton.addEventListener('click', () => {
  if (wifiPasswordInput.type === 'password') {
    wifiPasswordInput.type = 'text';
    showPasswordButton.textContent = '🔒';
  } else {
    wifiPasswordInput.type = 'password';
    showPasswordButton.textContent = '👁️';
  }
});

// Function to display status messages
function showStatus(message, type = 'info') {
  statusMessage.textContent = message;
  statusMessage.className = 'status-message status-' + type;
  statusMessage.style.display = 'block';
  
  // Auto-hide after 5 seconds if it's a success message
  if (type === 'success') {
    setTimeout(() => {
      statusMessage.style.display = 'none';
    }, 5000);
  }
}

// Function to clear status messages
function clearStatus() {
  statusMessage.style.display = 'none';
}

// Function to load current WiFi configuration
async function loadWifiConfig() {
  try {
    const response = await fetch('/api/wifi/status');
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    
    // Update UI with current configuration
    wifiEnabledToggle.checked = data.staEnabled || false;
    wifiSsidInput.value = data.staSsid || '';
    wifiPasswordInput.value = data.staPassword || '';
    
    // Update status display
    if (data.staConnected) {
      staStatusElement.textContent = `Connected to ${data.staSsid}`;
      staStatusElement.className = 'status-on';
      staIpElement.textContent = data.staIp || 'N/A';
    } else {
      staStatusElement.textContent = data.staEnabled ? 'Trying to connect...' : 'Disabled';
      staStatusElement.className = data.staEnabled ? 'status-warning' : '';
      staIpElement.textContent = 'N/A';
    }
    
    // Update time sync status
    if (data.timeSync) {
      timeSyncStatusElement.textContent = 'Synchronized';
      timeSyncStatusElement.className = 'status-on';
    } else {
      timeSyncStatusElement.textContent = 'Not Synchronized';
      timeSyncStatusElement.className = '';
    }
    
    console.log('WiFi configuration loaded:', data);
  } catch (error) {
    console.error('Error loading WiFi configuration:', error);
    showStatus('Failed to load WiFi configuration. Please try again.', 'error');
  }
}

// Function to save WiFi configuration
async function saveWifiConfig() {
  const ssid = wifiSsidInput.value.trim();
  const password = wifiPasswordInput.value;
  const enabled = wifiEnabledToggle.checked;
  
  // Basic validation
  if (enabled && !ssid) {
    showStatus('Please enter a WiFi network name (SSID).', 'error');
    return;
  }
  
  // Show loading status
  showStatus('Saving WiFi configuration...', 'info');
  
  try {
    const response = await fetch('/api/wifi/config', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        ssid,
        password,
        enabled
      })
    });
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    
    if (data.status === 'success') {
      showStatus('WiFi configuration saved successfully. ' + 
                (enabled ? 'The device will attempt to connect to the specified network.' : 'Station mode has been disabled.'), 
                'success');
      
      // Refresh status after a short delay to allow connection attempt
      setTimeout(loadWifiConfig, 3000);
    } else {
      showStatus(`Failed to save WiFi configuration: ${data.message}`, 'error');
    }
  } catch (error) {
    console.error('Error saving WiFi configuration:', error);
    showStatus('Failed to save WiFi configuration. Please try again.', 'error');
  }
}

// Function to test WiFi connection
async function testConnection() {
  try {
    const ssid = document.getElementById('wifi-ssid').value.trim();
    const password = document.getElementById('wifi-password').value;
    
    showStatus('Testing WiFi connection...', 'info');
    
    const response = await fetch('/api/wifi/test', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        ssid,
        password
      })
    });
    
    const data = await response.json();
    
    if (data.status === 'pending') {
      // Start polling for status
      showStatus('Test in progress, please wait...', 'info');
      pollWiFiStatus();
    } else if (data.status === 'success') {
      showStatus(`Connection successful! IP Address: ${data.ip}`, 'success');
      updateStatusDisplay(data);
    } else {
      showStatus(`Connection failed: ${data.message}`, 'error');
    }
  } catch (error) {
    console.error('Error testing WiFi connection:', error);
    showStatus('Failed to test WiFi connection. Please try again.', 'error');
  }
}

// Poll for WiFi status changes
function pollWiFiStatus() {
  const pollInterval = setInterval(async () => {
    try {
      const response = await fetch('/api/wifi/status');
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      
      const data = await response.json();
      
      // If connected to a new network
      if (data.staConnected) {
        clearInterval(pollInterval);
        showStatus(`Connection successful! IP Address: ${data.staIp}`, 'success');
        updateStatusDisplay(data);
      }
    } catch (error) {
      console.warn('Error polling WiFi status:', error);
      // Don't stop polling on error
    }
  }, 2000); // Poll every 2 seconds
  
  // Stop polling after 30 seconds
  setTimeout(() => {
    clearInterval(pollInterval);
    showStatus('WiFi test timed out. Please try again.', 'error');
  }, 30000);
}
// Add event listeners
saveWifiConfigButton.addEventListener('click', saveWifiConfig);
testConnectionButton.addEventListener('click', testConnection);

// Load current WiFi configuration on page load
document.addEventListener('DOMContentLoaded', loadWifiConfig);

// Refresh status periodically (every 30 seconds)
setInterval(loadWifiConfig, 30000);
