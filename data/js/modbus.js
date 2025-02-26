/**
 * ES32A08 MODBUS RTU Tester JavaScript
 */

// Elements
const deviceAddr = document.getElementById('device-addr');
const functionCode = document.getElementById('function-code');
const startAddr = document.getElementById('start-addr');
const quantity = document.getElementById('quantity');
const value = document.getElementById('value');
const coilValue = document.getElementById('coil-value');
const multipleValues = document.getElementById('multiple-values');
const sendButton = document.getElementById('send-request');
const responseStatus = document.getElementById('response-status');
const responseData = document.getElementById('response-data');
const rawResponse = document.getElementById('raw-response');

// Groups for dynamic display
const quantityGroup = document.getElementById('quantity-group');
const valueGroup = document.getElementById('value-group');
const coilValueGroup = document.getElementById('coil-value-group');
const multipleValuesGroup = document.getElementById('multiple-values-group');

// Update form based on function code
function updateFormFields() {
  const code = parseInt(functionCode.value);
  
  // Reset all groups
  quantityGroup.style.display = 'none';
  valueGroup.style.display = 'none';
  coilValueGroup.style.display = 'none';
  multipleValuesGroup.style.display = 'none';
  
  // Show appropriate groups based on function code
  switch (code) {
    case 1: // Read Coils
    case 2: // Read Discrete Inputs
    case 3: // Read Holding Registers
    case 4: // Read Input Registers
      quantityGroup.style.display = 'block';
      break;
      
    case 5: // Write Single Coil
      coilValueGroup.style.display = 'block';
      break;
      
    case 6: // Write Single Register
      valueGroup.style.display = 'block';
      break;
      
    case 15: // Write Multiple Coils
    case 16: // Write Multiple Registers
      multipleValuesGroup.style.display = 'block';
      break;
  }
}

// Parse multiple values from input
function parseMultipleValues(input, isCoils) {
  const values = input.split(',').map(val => val.trim());
  
  if (isCoils) {
    // For coils, convert to booleans
    return values.map(val => val === '1' || val.toLowerCase() === 'true' || val.toLowerCase() === 'on');
  } else {
    // For registers, convert to integers
    return values.map(val => parseInt(val));
  }
}

// Format data for display
function formatResponseData(data, functionCode) {
  // Handle different data formats based on function code
  const code = parseInt(functionCode);
  let formattedData = '';
  
  if (code === 1 || code === 2) {
    // Coils and Discrete Inputs (boolean values)
    formattedData = '<table><thead><tr><th>Address</th><th>Value</th></tr></thead><tbody>';
    
    const startAddrVal = parseInt(startAddr.value);
    data.forEach((value, index) => {
      formattedData += `
        <tr>
          <td>${startAddrVal + index}</td>
          <td>${value ? 'ON (1)' : 'OFF (0)'}</td>
        </tr>
      `;
    });
    
    formattedData += '</tbody></table>';
  } 
  else if (code === 3 || code === 4) {
    // Holding Registers and Input Registers
    formattedData = '<table><thead><tr><th>Address</th><th>Value (Decimal)</th><th>Value (Hex)</th></tr></thead><tbody>';
    
    const startAddrVal = parseInt(startAddr.value);
    data.forEach((value, index) => {
      formattedData += `
        <tr>
          <td>${startAddrVal + index}</td>
          <td>${value}</td>
          <td>0x${value.toString(16).toUpperCase().padStart(4, '0')}</td>
        </tr>
      `;
    });
    
    formattedData += '</tbody></table>';
  }
  else if (code === 5 || code === 6 || code === 15 || code === 16) {
    // Write functions - just show the written address and values
    formattedData = '<div class="success-message">Write operation successful</div>';
    
    if (data.length >= 2) {
      formattedData += `
        <div class="details">
          <p>Start Address: ${data[0]}</p>
          <p>${code === 15 || code === 16 ? 'Quantity' : 'Value'}: ${data[1]}</p>
        </div>
      `;
    }
  }
  
  return formattedData;
}

// Send MODBUS request
async function sendModbusRequest() {
  // Disable send button during request
  sendButton.disabled = true;
  responseStatus.textContent = 'Sending request...';
  responseStatus.className = '';
  
  try {
    const code = parseInt(functionCode.value);
    let requestData = {
      deviceAddr: parseInt(deviceAddr.value),
      functionCode: code,
      startAddr: parseInt(startAddr.value)
    };
    
    // Add function-specific parameters
    if (code === 1 || code === 2 || code === 3 || code === 4) {
      // Read functions
      requestData.quantity = parseInt(quantity.value);
    } 
    else if (code === 5) {
      // Write Single Coil
      requestData.value = coilValue.value === '1';
    }
    else if (code === 6) {
      // Write Single Register
      requestData.value = parseInt(value.value);
    }
    else if (code === 15) {
      // Write Multiple Coils
      requestData.values = parseMultipleValues(multipleValues.value, true);
    }
    else if (code === 16) {
      // Write Multiple Registers
      requestData.values = parseMultipleValues(multipleValues.value, false);
    }
    
    // Send the request
    const response = await fetch('/api/modbus/request', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(requestData)
    });
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    
    // Update response UI
    if (data.success) {
      responseStatus.textContent = 'Success';
      responseStatus.className = 'status-success';
      responseData.innerHTML = formatResponseData(data.data, functionCode.value);
    } else {
      responseStatus.textContent = 'Error: ' + (data.error || 'Unknown error');
      responseStatus.className = 'status-error';
      responseData.innerHTML = '';
    }
    
    // Update raw response
    rawResponse.textContent = JSON.stringify(data, null, 2);
    
  } catch (error) {
    console.error('Error sending MODBUS request:', error);
    responseStatus.textContent = 'Error: ' + error.message;
    responseStatus.className = 'status-error';
    responseData.innerHTML = '';
    rawResponse.textContent = '';
  } finally {
    // Re-enable send button
    sendButton.disabled = false;
  }
}

// Add event listeners
function addEventListeners() {
  // Update form fields when function code changes
  functionCode.addEventListener('change', updateFormFields);
  
  // Send button event
  sendButton.addEventListener('click', sendModbusRequest);
}

// Initialize the MODBUS tester
function initModbusTester() {
  updateFormFields();
  addEventListeners();
}

// Start everything when the DOM is loaded
document.addEventListener('DOMContentLoaded', initModbusTester);