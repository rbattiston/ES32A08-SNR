/**
 * API interactions for the scheduler
 */
import { debugPrintln } from './constants';

// Get the status of the scheduler from the server
export async function getSchedulerStatus() {
  try {
    const response = await fetch("/api/scheduler/status");
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    const data = await response.json();
    return data;
  } catch (error) {
    console.error("Error getting scheduler status:", error);
    return { isActive: false };
  }
}

// Update the UI based on scheduler status
export async function updateSchedulerStatus() {
  const activateButton = document.getElementById("activate-scheduler");
  const deactivateButton = document.getElementById("deactivate-scheduler");
  
  const status = await getSchedulerStatus();
  
  if (status.isActive) {
    activateButton.disabled = true;
    deactivateButton.disabled = false;
    activateButton.classList.add("disabled");
    deactivateButton.classList.remove("disabled");
  } else {
    activateButton.disabled = false;
    deactivateButton.disabled = true;
    activateButton.classList.remove("disabled");
    deactivateButton.classList.add("disabled");
  }
}

// Activate the scheduler
export async function activateScheduler() {
  try {
    const response = await fetch("/api/scheduler/activate", { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    await updateSchedulerStatus();
    alert("Scheduler activated");
  } catch (error) {
    console.error("Error activating scheduler:", error);
    alert("Failed to activate scheduler: " + error.message);
  }
}

// Deactivate the scheduler
export async function deactivateScheduler() {
  try {
    const response = await fetch("/api/scheduler/deactivate", { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    
    await updateSchedulerStatus();
    alert("Scheduler deactivated");
  } catch (error) {
    console.error("Error deactivating scheduler:", error);
    alert("Failed to deactivate scheduler: " + error.message);
  }
}