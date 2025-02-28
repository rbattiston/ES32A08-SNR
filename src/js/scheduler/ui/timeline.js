/**
 * Timeline utility functions for the scheduler
 */

// Update the current time line on the timeline
export function updateCurrentTimeLine() {
    const timelineContainer = document.getElementById("timeline-container");
    if (!timelineContainer) return;
    
    // Remove any existing time line
    const existingLine = timelineContainer.querySelector(".current-time-line");
    if (existingLine) {
      existingLine.remove();
    }
    
    // Create new time line
    const currentLine = document.createElement("div");
    currentLine.className = "current-time-line";
    currentLine.style.position = "absolute";
    currentLine.style.top = "0";
    currentLine.style.bottom = "0";
    currentLine.style.width = "2px";
    currentLine.style.backgroundColor = "red";
    currentLine.style.zIndex = "10";
    
    // Calculate position based on current time
    const now = new Date();
    const currentMinutes = now.getHours() * 60 + now.getMinutes();
    const leftPercent = (currentMinutes / 1440) * 100;
    currentLine.style.left = `${leftPercent}%`;
    
    timelineContainer.appendChild(currentLine);
  }
  
  // Start periodic updates of the current time line
  export function startTimelineUpdates() {
    // Initial update
    updateCurrentTimeLine();
    
    // Periodic updates every minute
    setInterval(updateCurrentTimeLine, 60000);
  }