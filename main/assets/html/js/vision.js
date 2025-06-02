// Vision page JavaScript

// Configuration and state
const visionConfig = {
  streaming: false,
  camera: {
    brightness: 0,
    contrast: 0,
    saturation: 0,
    hmirror: false,
    vflip: false,
    ledIntensity: 0,
  },
  ws: null
};

// Initialize when document is ready
document.addEventListener('DOMContentLoaded', function() {
  initWebSocket();
  initCameraControls();
  
  // Button handlers
  document.getElementById('start-stream')?.addEventListener('click', startStreaming);
  document.getElementById('stop-stream')?.addEventListener('click', stopStreaming);
  document.getElementById('capture-image')?.addEventListener('click', captureImage);
  document.getElementById('detect-objects')?.addEventListener('click', detectObjects);
});

// Initialize WebSocket connection
function initWebSocket() {
  if (visionConfig.ws) {
    visionConfig.ws.close();
  }
  
  const wsUrl = `ws://${window.location.host}/ws/camera`;
  visionConfig.ws = new WebSocket(wsUrl);
  
  visionConfig.ws.onopen = function() {
    console.log("WebSocket connection established");
    // Request initial camera status
    sendWebSocketMessage({ cmd: 'get_status' });
  };
  
  visionConfig.ws.onmessage = function(event) {
    try {
      const message = JSON.parse(event.data);
      handleWebSocketMessage(message);
    } catch (e) {
      console.error("Error parsing WebSocket message:", e);
    }
  };
  
  visionConfig.ws.onclose = function() {
    console.log("WebSocket connection closed");
    // Try to reconnect after a delay
    setTimeout(initWebSocket, 5000);
  };
  
  visionConfig.ws.onerror = function(error) {
    console.error("WebSocket error:", error);
  };
}

// Send message through WebSocket
function sendWebSocketMessage(message) {
  if (visionConfig.ws && visionConfig.ws.readyState === WebSocket.OPEN) {
    visionConfig.ws.send(JSON.stringify(message));
  } else {
    console.warn("WebSocket not connected");
  }
}

// Handle incoming WebSocket messages
function handleWebSocketMessage(message) {
  console.log("Received message:", message);
  
  if (message.type === 'status') {
    updateCameraStatus(message.data);
  } else if (message.type === 'detection') {
    showDetectionResults(message.data);
  }
}

// Update UI with camera status
function updateCameraStatus(status) {
  // Update UI elements with camera status
  const cameraPlaceholder = document.getElementById('camera-placeholder');
  const cameraControls = document.getElementById('camera-controls');
  
  if (status.is_streaming) {
    if (cameraPlaceholder) cameraPlaceholder.style.display = 'none';
    if (cameraControls) cameraControls.style.display = 'flex';
  } else {
    if (cameraPlaceholder) cameraPlaceholder.style.display = 'flex';
    if (cameraControls) cameraControls.style.display = 'none';
  }
  
  // Update control values
  document.getElementById('brightness-slider')?.value = status.brightness;
  document.getElementById('contrast-slider')?.value = status.contrast;
  document.getElementById('saturation-slider')?.value = status.saturation;
  document.getElementById('hmirror-checkbox')?.checked = status.hmirror;
  document.getElementById('vflip-checkbox')?.checked = status.vflip;
  
  // Update internal state
  visionConfig.camera = {
    brightness: status.brightness,
    contrast: status.contrast,
    saturation: status.saturation,
    hmirror: status.hmirror,
    vflip: status.vflip,
    ledIntensity: status.flash_level || 0
  };
  
  visionConfig.streaming = status.is_streaming;
}

// Initialize camera control UI elements
function initCameraControls() {
  // Brightness control
  document.getElementById('brightness-slider')?.addEventListener('input', function() {
    sendWebSocketMessage({ 
      cmd: 'set_brightness',
      value: parseInt(this.value)
    });
  });
  
  // Contrast control
  document.getElementById('contrast-slider')?.addEventListener('input', function() {
    sendWebSocketMessage({ 
      cmd: 'set_contrast',
      value: parseInt(this.value)
    });
  });
  
  // Saturation control
  document.getElementById('saturation-slider')?.addEventListener('input', function() {
    sendWebSocketMessage({ 
      cmd: 'set_saturation',
      value: parseInt(this.value)
    });
  });
  
  // H-Mirror control
  document.getElementById('hmirror-checkbox')?.addEventListener('change', function() {
    sendWebSocketMessage({ 
      cmd: 'set_hmirror',
      value: this.checked
    });
  });
  
  // V-Flip control
  document.getElementById('vflip-checkbox')?.addEventListener('change', function() {
    sendWebSocketMessage({ 
      cmd: 'set_vflip',
      value: this.checked
    });
  });
  
  // LED Intensity control
  document.getElementById('led-slider')?.addEventListener('input', function() {
    sendWebSocketMessage({ 
      cmd: 'set_flash',
      value: parseInt(this.value)
    });
  });
}

// Start camera streaming
function startStreaming() {
  const cameraView = document.getElementById('camera-feed');
  if (cameraView) {
    cameraView.src = `/api/camera/stream?t=${new Date().getTime()}`;
  }
  
  sendWebSocketMessage({ cmd: 'start_stream' });
}

// Stop camera streaming
function stopStreaming() {
  const cameraView = document.getElementById('camera-feed');
  if (cameraView) {
    cameraView.src = '';
  }
  
  sendWebSocketMessage({ cmd: 'stop_stream' });
}

// Capture a still image
function captureImage() {
  const imageUrl = `/api/camera/capture?t=${new Date().getTime()}`;
  
  // Show captured image in a modal or new window
  window.open(imageUrl, '_blank');
  
  sendWebSocketMessage({ cmd: 'capture' });
}

// Run object detection
function detectObjects() {
  sendWebSocketMessage({ 
    cmd: 'run_detection',
    model: 'object_detection'
  });
}

// Show detection results
function showDetectionResults(data) {
  const resultsContainer = document.getElementById('detection-results');
  if (!resultsContainer) return;
  
  if (!data.results || data.results.length === 0) {
    resultsContainer.innerHTML = '<p>No objects detected</p>';
    return;
  }
  
  let html = '<h3>Detection Results:</h3><ul>';
  data.results.forEach(result => {
    html += `<li class="result-item">
      <span>${result.label || 'Unknown'}</span>
      <span>${Math.round((result.confidence || 0) * 100)}%</span>
    </li>`;
  });
  html += '</ul>';
  
  resultsContainer.innerHTML = html;
} 