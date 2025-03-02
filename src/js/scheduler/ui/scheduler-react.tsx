import React, { useState, useEffect } from 'react';

const IrrigationScheduler = () => {
  // Application state
  const [mode, setMode] = useState('view'); // 'view', 'creating', 'editing'
  const [schedules, setSchedules] = useState([]);
  const [activeSchedules, setActiveSchedules] = useState([]);
  const [currentSchedule, setCurrentSchedule] = useState(null);
  const [selectedScheduleId, setSelectedScheduleId] = useState(null);
  const [isLoading, setIsLoading] = useState(true);
  const [statusMessage, setStatusMessage] = useState({ text: '', type: '' });

  // Form state
  const [scheduleName, setScheduleName] = useState('');
  const [lightsOnTime, setLightsOnTime] = useState('06:00');
  const [lightsOffTime, setLightsOffTime] = useState('18:00');
  const [selectedRelays, setSelectedRelays] = useState(Array(8).fill(false));
  const [events, setEvents] = useState([]);
  
  // New event form state
  const [newEventTime, setNewEventTime] = useState('08:00');
  const [newEventDuration, setNewEventDuration] = useState(10);
  const [newEventRepeat, setNewEventRepeat] = useState(0);
  const [newEventInterval, setNewEventInterval] = useState(60);

  // Load schedules on component mount
  useEffect(() => {
    fetchSchedules();
  }, []);

  // Fetch schedules from the server
  const fetchSchedules = async () => {
    setIsLoading(true);
    try {
      const response = await fetch('/api/scheduler/load');
      if (!response.ok) throw new Error('Failed to load schedules');
      const data = await response.json();
      
      // Process the loaded data
      const allSchedules = data.schedules || [];
      
      // Separate active schedules (with assigned relays)
      const active = allSchedules.filter(s => s.relayMask > 0);
      
      setSchedules(allSchedules);
      setActiveSchedules(active);
      setIsLoading(false);
      
      // If there are schedules, select the first one
      if (allSchedules.length > 0) {
        const defaultIndex = data.currentScheduleIndex || 0;
        selectSchedule(defaultIndex);
      }
      
    } catch (error) {
      console.error('Error loading schedules:', error);
      setStatusMessage({
        text: 'Failed to load schedules. Please refresh the page.',
        type: 'error'
      });
      setIsLoading(false);
    }
  };

  // Save the current schedule
  const saveSchedule = async () => {
    try {
      // Update the current schedule
      const updatedSchedule = {
        ...currentSchedule,
        name: scheduleName,
        lightsOnTime,
        lightsOffTime,
        relayMask: calculateRelayMask(selectedRelays),
        events
      };
      
      // Update the schedules array
      const updatedSchedules = [...schedules];
      if (mode === 'editing') {
        // Replace the existing schedule
        const index = updatedSchedules.findIndex(s => s.name === currentSchedule.name);
        if (index !== -1) updatedSchedules[index] = updatedSchedule;
      } else {
        // Add a new schedule
        updatedSchedules.push(updatedSchedule);
      }
      
      // Prepare the data to send to the server
      const dataToSave = {
        scheduleCount: updatedSchedules.length,
        currentScheduleIndex: mode === 'editing' 
          ? schedules.findIndex(s => s.name === currentSchedule.name)
          : updatedSchedules.length - 1,
        schedules: updatedSchedules
      };
      
      // Send to the server
      const response = await fetch('/api/scheduler/save', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(dataToSave)
      });
      
      if (!response.ok) throw new Error('Failed to save schedule');
      
      // Update local state
      setSchedules(updatedSchedules);
      setActiveSchedules(updatedSchedules.filter(s => s.relayMask > 0));
      setMode('view');
      setStatusMessage({
        text: `Schedule ${mode === 'editing' ? 'updated' : 'created'} successfully!`,
        type: 'success'
      });
      
      // Clear the status message after 3 seconds
      setTimeout(() => setStatusMessage({ text: '', type: '' }), 3000);
      
    } catch (error) {
      console.error('Error saving schedule:', error);
      setStatusMessage({
        text: 'Failed to save schedule. Please try again.',
        type: 'error'
      });
    }
  };

  // Calculate relay mask from boolean array
  const calculateRelayMask = (relays) => {
    return relays.reduce((mask, isSelected, index) => {
      return isSelected ? mask | (1 << index) : mask;
    }, 0);
  };

  // Convert relay mask to boolean array
  const getRelaysFromMask = (mask) => {
    return Array(8).fill(false).map((_, index) => (mask & (1 << index)) !== 0);
  };

  // Select a schedule by index
  const selectSchedule = (index) => {
    if (index < 0 || index >= schedules.length) return;
    
    const selected = schedules[index];
    setCurrentSchedule(selected);
    setSelectedScheduleId(index);
    
    // Populate form fields
    setScheduleName(selected.name);
    setLightsOnTime(selected.lightsOnTime);
    setLightsOffTime(selected.lightsOffTime);
    setSelectedRelays(getRelaysFromMask(selected.relayMask));
    setEvents(selected.events || []);
  };

  // Add a new event to the current schedule
  const addEvent = () => {
    if (!newEventTime || newEventDuration <= 0) {
      setStatusMessage({
        text: 'Please enter a valid event time and duration',
        type: 'error'
      });
      return;
    }
    
    // Create new event
    const newEvent = {
      id: Date.now().toString(),
      time: newEventTime,
      duration: newEventDuration,
      executedMask: 0
    };
    
    // Handle repeats
    const newEvents = [newEvent];
    
    if (newEventRepeat > 0 && newEventInterval > 0) {
      const [baseHour, baseMinute] = newEventTime.split(':').map(Number);
      let baseMinutes = baseHour * 60 + baseMinute;
      
      for (let i = 1; i <= newEventRepeat; i++) {
        const occurrenceMinutes = baseMinutes + (i * newEventInterval);
        
        // Skip if beyond 24 hours
        if (occurrenceMinutes >= 1440) break;
        
        const occurrenceHour = Math.floor(occurrenceMinutes / 60);
        const occurrenceMinute = occurrenceMinutes % 60;
        const occurrenceTime = `${occurrenceHour.toString().padStart(2, '0')}:${occurrenceMinute.toString().padStart(2, '0')}`;
        
        // Add the repeated event
        newEvents.push({
          id: Date.now().toString() + '_' + i,
          time: occurrenceTime,
          duration: newEventDuration,
          executedMask: 0
        });
      }
    }
    
    // Add all new events to the events list
    setEvents([...events, ...newEvents]);
    
    // Reset form fields
    setNewEventTime('08:00');
    setNewEventDuration(10);
    setNewEventRepeat(0);
    setNewEventInterval(60);
    
    setStatusMessage({
      text: `Added ${newEvents.length} event${newEvents.length > 1 ? 's' : ''}`,
      type: 'success'
    });
    
    // Clear the status message after 3 seconds
    setTimeout(() => setStatusMessage({ text: '', type: '' }), 3000);
  };

  // Delete an event from the current schedule
  const deleteEvent = (eventId) => {
    setEvents(events.filter(event => event.id !== eventId));
  };

  // Start creating a new schedule
  const createNewSchedule = () => {
    setCurrentSchedule(null);
    setSelectedScheduleId(null);
    setScheduleName(`New Schedule ${new Date().toLocaleString().slice(0, 16)}`);
    setLightsOnTime('06:00');
    setLightsOffTime('18:00');
    setSelectedRelays(Array(8).fill(false));
    setEvents([]);
    setMode('creating');
  };

  // Start editing an existing schedule
  const editSchedule = (index) => {
    selectSchedule(index);
    setMode('editing');
  };

  // Delete a schedule
  const deleteSchedule = async () => {
    if (!currentSchedule) return;
    
    if (!confirm(`Are you sure you want to delete "${currentSchedule.name}"?`)) {
      return;
    }
    
    try {
      // Remove the schedule from the array
      const updatedSchedules = schedules.filter((_, index) => index !== selectedScheduleId);
      
      // Prepare the data to send to the server
      const dataToSave = {
        scheduleCount: updatedSchedules.length,
        currentScheduleIndex: 0,
        schedules: updatedSchedules
      };
      
      // Send to the server
      const response = await fetch('/api/scheduler/save', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(dataToSave)
      });
      
      if (!response.ok) throw new Error('Failed to delete schedule');
      
      // Update local state
      setSchedules(updatedSchedules);
      setActiveSchedules(updatedSchedules.filter(s => s.relayMask > 0));
      setSelectedScheduleId(null);
      setCurrentSchedule(null);
      
      if (updatedSchedules.length > 0) {
        selectSchedule(0);
      }
      
      setStatusMessage({
        text: 'Schedule deleted successfully',
        type: 'success'
      });
      
      // Clear the status message after 3 seconds
      setTimeout(() => setStatusMessage({ text: '', type: '' }), 3000);
      
    } catch (error) {
      console.error('Error deleting schedule:', error);
      setStatusMessage({
        text: 'Failed to delete schedule. Please try again.',
        type: 'error'
      });
    }
  };

  // Cancel editing or creating
  const cancelEdit = () => {
    if (selectedScheduleId !== null) {
      selectSchedule(selectedScheduleId);
    }
    setMode('view');
  };

  // Format duration from seconds to human-readable format
  const formatDuration = (seconds) => {
    if (seconds < 60) return `${seconds}s`;
    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = seconds % 60;
    return `${minutes}m ${remainingSeconds > 0 ? remainingSeconds + 's' : ''}`;
  };

  // Timeline component
  const Timeline = ({ events, lightsOnTime, lightsOffTime }) => {
    // Convert time string to percentage of day
    const timeToPercent = (timeStr) => {
      const [hours, minutes] = timeStr.split(':').map(Number);
      return ((hours * 60 + minutes) / 1440) * 100;
    };
    
    // Parse lights on/off times
    const onTimePercent = timeToPercent(lightsOnTime);
    const offTimePercent = timeToPercent(lightsOffTime);
    
    return (
      <div className="w-full h-16 relative border border-gray-300 rounded overflow-hidden bg-gray-100">
        <div 
          className="absolute h-full bg-yellow-100" 
          style={{ 
            left: `${onTimePercent}%`, 
            width: `${offTimePercent > onTimePercent ? offTimePercent - onTimePercent : 100 - onTimePercent + offTimePercent}%`,
            opacity: 0.7
          }}
        ></div>
        
        {Array.from({ length: 13 }).map((_, i) => (
          <div 
            key={i} 
            className="absolute top-0 h-full border-l border-gray-300 text-xs text-gray-500"
            style={{ left: `${(i * 2) * 4.166}%` }}
          >
            <span className="absolute top-0 left-1">{i * 2}:00</span>
          </div>
        ))}
        
        {events.map(event => {
          const eventPercent = timeToPercent(event.time);
          const widthPercent = Math.max(0.5, Math.min(5, (event.duration / 86400) * 100));
          
          return (
            <div
              key={event.id}
              className="absolute h-2/3 top-1/6 bg-blue-500 rounded-sm hover:bg-blue-600 cursor-pointer"
              style={{ left: `${eventPercent}%`, width: `${widthPercent}%` }}
              title={`${event.time} - ${formatDuration(event.duration)}`}
            ></div>
          );
        })}
        
        <CurrentTimeLine />
      </div>
    );
  };

  // Current time line component
  const CurrentTimeLine = () => {
    const [position, setPosition] = useState(0);
    
    useEffect(() => {
      const updatePosition = () => {
        const now = new Date();
        const minutesOfDay = now.getHours() * 60 + now.getMinutes();
        setPosition((minutesOfDay / 1440) * 100); // 1440 minutes in a day
      };
      
      // Update immediately and then every minute
      updatePosition();
      const interval = setInterval(updatePosition, 60000);
      
      return () => clearInterval(interval);
    }, []);
    
    return (
      <div 
        className="absolute top-0 h-full w-0.5 bg-red-500 z-10"
        style={{ left: `${position}%` }}
      ></div>
    );
  };

  // Return the main component JSX
  return (
    <div className="max-w-6xl mx-auto px-4 py-6">
      <h1 className="text-2xl font-bold text-blue-800 mb-6">Irrigation Scheduler</h1>
      
      {statusMessage.text && (
        <div className={`mb-4 p-3 rounded ${statusMessage.type === 'success' ? 'bg-green-100 text-green-700' : 'bg-red-100 text-red-700'}`}>
          {statusMessage.text}
        </div>
      )}
      
      {isLoading ? (
        <div className="text-center py-8">
          <div className="animate-spin rounded-full h-12 w-12 border-t-2 border-b-2 border-blue-500 mx-auto mb-4"></div>
          <p>Loading schedules...</p>
        </div>
      ) : (
        <>
          <section className={`mb-8 ${mode !== 'view' ? 'opacity-50 pointer-events-none' : ''}`}>
            <h2 className="text-xl font-semibold mb-4 text-gray-700 border-b pb-2">Active Schedules</h2>
            
            {activeSchedules.length > 0 ? (
              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                {activeSchedules.map((schedule, index) => (
                  <div key={index} className="bg-white rounded-lg shadow-md p-4 border-l-4 border-blue-500">
                    <h3 className="font-medium text-lg mb-2">{schedule.name}</h3>
                    <div className="text-sm text-gray-600 mb-2">
                      <div>Lights: {schedule.lightsOnTime} to {schedule.lightsOffTime}</div>
                      <div>
                        Relays: {getRelaysFromMask(schedule.relayMask)
                          .map((isOn, i) => isOn ? i+1 : null)
                          .filter(Boolean)
                          .join(', ')}
                      </div>
                    </div>
                    
                    <h4 className="font-medium mt-3 mb-1">Events:</h4>
                    {schedule.events && schedule.events.length > 0 ? (
                      <ul className="text-sm max-h-32 overflow-y-auto">
                        {schedule.events.map((event, i) => (
                          <li key={i} className="mb-1">
                            {event.time} - {formatDuration(event.duration)}
                          </li>
                        ))}
                      </ul>
                    ) : (
                      <p className="text-sm text-gray-500">No events defined</p>
                    )}
                    
                    <div className="mt-4 text-right">
                      <button 
                        onClick={() => editSchedule(schedules.findIndex(s => s.name === schedule.name))}
                        className="text-blue-600 hover:text-blue-800"
                      >
                        Edit
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            ) : (
              <div className="bg-gray-50 rounded-lg p-4 text-gray-500 text-center">
                No active schedules. Create a schedule and assign relays to make it active.
              </div>
            )}
          </section>
          
          {mode === 'view' && (
            <section className="mb-8">
              <h2 className="text-xl font-semibold mb-4 text-gray-700 border-b pb-2">Select Schedule</h2>
              <div className="flex flex-wrap gap-4 items-center">
                <select 
                  value={selectedScheduleId || ''}
                  onChange={(e) => selectSchedule(Number(e.target.value))}
                  className="border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                >
                  <option value="" disabled>Select a schedule</option>
                  {schedules.map((schedule, index) => (
                    <option key={index} value={index}>
                      {schedule.name} {schedule.relayMask > 0 ? '(Active)' : '(Inactive)'}
                    </option>
                  ))}
                </select>
                
                <button 
                  onClick={createNewSchedule}
                  className="bg-blue-500 hover:bg-blue-600 text-white px-4 py-2 rounded"
                >
                  Create New Schedule
                </button>
                
                {currentSchedule && (
                  <>
                    <button 
                      onClick={() => editSchedule(selectedScheduleId)}
                      className="bg-gray-200 hover:bg-gray-300 px-4 py-2 rounded"
                    >
                      Edit
                    </button>
                    <button 
                      onClick={deleteSchedule}
                      className="bg-red-500 hover:bg-red-600 text-white px-4 py-2 rounded"
                    >
                      Delete
                    </button>
                  </>
                )}
              </div>
            </section>
          )}
          
          {(mode === 'editing' || mode === 'creating') && (
            <section className="mb-8 bg-white rounded-lg shadow-md p-6">
              <h2 className="text-xl font-semibold mb-4 text-gray-700 border-b pb-2">
                {mode === 'creating' ? 'Create New Schedule' : 'Edit Schedule'}
              </h2>
              
              <div className="space-y-6">
                <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
                  <div>
                    <label className="block text-gray-700 mb-2">Schedule Name</label>
                    <input 
                      type="text"
                      value={scheduleName}
                      onChange={(e) => setScheduleName(e.target.value)}
                      className="w-full border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                      placeholder="Enter schedule name"
                      required
                    />
                  </div>
                  
                  <div className="grid grid-cols-2 gap-4">
                    <div>
                      <label className="block text-gray-700 mb-2">Lights On Time</label>
                      <input 
                        type="time"
                        value={lightsOnTime}
                        onChange={(e) => setLightsOnTime(e.target.value)}
                        className="w-full border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                      />
                    </div>
                    
                    <div>
                      <label className="block text-gray-700 mb-2">Lights Off Time</label>
                      <input 
                        type="time"
                        value={lightsOffTime}
                        onChange={(e) => setLightsOffTime(e.target.value)}
                        className="w-full border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                      />
                    </div>
                  </div>
                </div>
                
                <div>
                  <label className="block text-gray-700 mb-2">Relay Assignment</label>
                  <div className="grid grid-cols-2 sm:grid-cols-4 gap-x-4 gap-y-2">
                    {selectedRelays.map((isSelected, index) => (
                      <label key={index} className="flex items-center space-x-2">
                        <input 
                          type="checkbox"
                          checked={isSelected}
                          onChange={() => {
                            const newRelays = [...selectedRelays];
                            newRelays[index] = !newRelays[index];
                            setSelectedRelays(newRelays);
                          }}
                          className="rounded text-blue-500 focus:ring-blue-500"
                        />
                        <span>Relay {index + 1}</span>
                      </label>
                    ))}
                  </div>
                  <p className="text-sm text-gray-500 mt-1">Assign relays to this schedule. A relay can only be assigned to one active schedule.</p>
                </div>
                
                <div>
                  <label className="block text-gray-700 mb-2">Schedule Visualization (24-hour)</label>
                  <Timeline 
                    events={events} 
                    lightsOnTime={lightsOnTime} 
                    lightsOffTime={lightsOffTime} 
                  />
                </div>
                
                <div>
                  <label className="block text-gray-700 mb-2">Events</label>
                  {events.length > 0 ? (
                    <div className="border border-gray-300 rounded max-h-60 overflow-y-auto">
                      {events.map(event => (
                        <div 
                          key={event.id} 
                          className="px-4 py-2 border-b border-gray-200 flex justify-between items-center hover:bg-gray-50"
                        >
                          <span>{event.time} - {formatDuration(event.duration)}</span>
                          <button 
                            onClick={() => deleteEvent(event.id)}
                            className="text-red-500 hover:text-red-700"
                          >
                            Delete
                          </button>
                        </div>
                      ))}
                    </div>
                  ) : (
                    <div className="bg-gray-50 rounded-lg p-4 text-gray-500 text-center">
                      No events defined yet. Add your first event below.
                    </div>
                  )}
                </div>
                
                <div className="bg-gray-50 p-4 rounded-lg">
                  <h3 className="font-medium mb-3">Add New Event</h3>
                  <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
                    <div>
                      <label className="block text-gray-700 mb-1 text-sm">Start Time</label>
                      <input 
                        type="time"
                        value={newEventTime}
                        onChange={(e) => setNewEventTime(e.target.value)}
                        className="w-full border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                      />
                      <p className="text-xs text-gray-500 mt-1">When the relay turns ON</p>
                    </div>
                    
                    <div>
                      <label className="block text-gray-700 mb-1 text-sm">Duration (seconds)</label>
                      <input 
                        type="number"
                        value={newEventDuration}
                        onChange={(e) => setNewEventDuration(Number(e.target.value))}
                        min="1"
                        max="3600"
                        className="w-full border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                      />
                      <p className="text-xs text-gray-500 mt-1">How long the relay stays ON</p>
                    </div>
                    
                    <div>
                      <label className="block text-gray-700 mb-1 text-sm">Repeat Count</label>
                      <input 
                        type="number"
                        value={newEventRepeat}
                        onChange={(e) => setNewEventRepeat(Number(e.target.value))}
                        min="0"
                        max="20"
                        className="w-full border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                      />
                      <p className="text-xs text-gray-500 mt-1">Number of repetitions (0 = no repeats)</p>
                    </div>
                    
                    <div>
                      <label className="block text-gray-700 mb-1 text-sm">Repeat Interval (minutes)</label>
                      <input 
                        type="number"
                        value={newEventInterval}
                        onChange={(e) => setNewEventInterval(Number(e.target.value))}
                        min="1"
                        max="1440"
                        className="w-full border border-gray-300 rounded px-3 py-2 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:border-transparent"
                      />
                      <p className="text-xs text-gray-500 mt-1">Time between repetitions</p>
                    </div>
                  </div>
                  
                  <div className="mt-4">
                    <button 
                      onClick={addEvent}
                      className="bg-blue-500 hover:bg-blue-600 text-white px-4 py-2 rounded"
                    >
                      Add Event
                    </button>
                  </div>
                </div>
                
                <div className="flex justify-end space-x-4 mt-6">
                  <button 
                    onClick={cancelEdit}
                    className="bg-gray-200 hover:bg-gray-300 px-4 py-2 rounded"
                  >
                    Cancel
                  </button>
                  <button 
                    onClick={saveSchedule}
                    className="bg-green-500 hover:bg-green-600 text-white px-4 py-2 rounded"
                  >
                    Save Schedule
                  </button>
                </div>
              </div>
            </section>
          )}
        </>
      )}
    </div>
  );
};

export default IrrigationScheduler;
 