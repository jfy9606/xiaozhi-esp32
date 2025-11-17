/**
 * Xiaozhi ESP32 Vehicle Control System
 * Camera Module
 */

// Camera system
class CameraSystem {
    constructor(viewerId, controlsId) {
        // DOM Elements
        this.viewerContainer = document.getElementById(viewerId);
        this.controlsContainer = document.getElementById(controlsId);
        
        // Camera state
        this.isStreaming = false;
        this.streamUrl = null;
        this.streamType = 'mjpeg'; // 'mjpeg' or 'jpeg' or 'webrtc'
        this.resolution = {
            width: 640,
            height: 480
        };
        this.frameRate = 15;
        
        // Vision state
        this.visionEnabled = false;
        this.detectionEnabled = false;
        this.trackedObjects = [];
        this.detectionBoxes = [];
        
        // UI elements
        this.streamImg = null;
        this.streamVideo = null;
        this.detectionCanvas = null;
        this.detectionOverlay = null;
        
        // Stream refresh
        this.streamRefreshInterval = null;
        this.streamRefreshMs = 100; // JPEG mode refresh rate
        
        // Error handling
        this.errorCount = 0;
        this.maxErrors = 5;
        this.reconnectTimeout = null;
        
        // Initialize
        this.init();
    }
    
    /**
     * Initialize camera system
     */
    init() {
        this.createCameraViewer();
        this.createCameraControls();
        
        // Register for WebSocket messages
        if (window.app) {
            window.app.registerWebSocketHandler('camera_status', this.handleStatusMessage.bind(this));
            window.app.registerWebSocketHandler('detection_result', this.handleDetectionResult.bind(this));
        }
        
        // Set as global instance
        window.cameraSystem = this;
        
        // Start with default camera feed
        this.startStream();
        
        console.log('Camera system initialized');
    }
    
    /**
     * Create camera viewer element
     */
    createCameraViewer() {
        if (!this.viewerContainer) return;
        
        // Clear container
        this.viewerContainer.innerHTML = '';
        
        // Create viewer container with relative positioning
        this.viewerContainer.className = 'camera-view';
        
        // Add placeholder text until stream starts
        const placeholder = document.createElement('div');
        placeholder.className = 'camera-placeholder';
        placeholder.innerHTML = `
            <div class="camera-placeholder-icon">📷</div>
            <div class="camera-placeholder-text">连接摄像头中...</div>
        `;
        this.viewerContainer.appendChild(placeholder);
        
        // Create detection overlay container
        this.detectionOverlay = document.createElement('div');
        this.detectionOverlay.className = 'detections-container';
        this.viewerContainer.appendChild(this.detectionOverlay);
    }
    
    /**
     * Create camera controls UI elements
     */
    createCameraControls() {
        if (!this.controlsContainer) return;
        
        // Clear container
        this.controlsContainer.innerHTML = '';
        
        // Create control buttons
        const controlButtons = [
            {
                id: 'stream-toggle',
                icon: '📹',
                label: '开始视频',
                action: this.toggleStream.bind(this)
            },
            {
                id: 'detection-toggle',
                icon: '🔍',
                label: '目标检测',
                action: this.toggleDetection.bind(this)
            },
            {
                id: 'snapshot',
                icon: '📸',
                label: '截图',
                action: this.takeSnapshot.bind(this)
            },
            {
                id: 'resolution-toggle',
                icon: '🖼️',
                label: '分辨率',
                action: this.cycleResolution.bind(this)
            },
            {
                id: 'camera-settings',
                icon: '⚙️',
                label: '设置',
                action: this.showSettings.bind(this)
            }
        ];
        
        // Add buttons to controls container
        controlButtons.forEach(button => {
            const btnElement = document.createElement('button');
            btnElement.id = button.id;
            btnElement.className = 'btn btn-primary mr-2';
            btnElement.innerHTML = `${button.icon} ${button.label}`;
            btnElement.addEventListener('click', button.action);
            this.controlsContainer.appendChild(btnElement);
        });
    }
    
    /**
     * Start camera stream
     * @param {string} type - Stream type ('mjpeg', 'jpeg', 'webrtc')
     */
    startStream(type = 'mjpeg') {
        // Stop any existing stream
        this.stopStream();
        
        // Set stream type
        this.streamType = type || 'mjpeg';
        
        // Get URL based on stream type
        switch (this.streamType) {
            case 'mjpeg':
                this.streamUrl = `/stream?width=${this.resolution.width}&height=${this.resolution.height}`;
                break;
                
            case 'jpeg':
                this.streamUrl = `/capture?width=${this.resolution.width}&height=${this.resolution.height}`;
                break;
                
            case 'webrtc':
                this.streamUrl = null; // Will be handled by WebRTC signaling
                break;
                
            default:
                console.error(`Unsupported stream type: ${this.streamType}`);
                return;
        }
        
        // Update UI based on stream type
        if (this.streamType === 'mjpeg' || this.streamType === 'jpeg') {
            // Create or get image element
            if (!this.streamImg) {
                if (this.streamVideo) {
                    this.streamVideo.remove();
                    this.streamVideo = null;
                }
                
                this.streamImg = document.createElement('img');
                this.streamImg.className = 'camera-stream-img';
                this.streamImg.alt = 'Camera Stream';
                
                // Remove placeholder and add stream image
                this.viewerContainer.querySelector('.camera-placeholder')?.remove();
                this.viewerContainer.insertBefore(this.streamImg, this.detectionOverlay);
                
                // Add error handler
                this.streamImg.onerror = () => this.handleStreamError();
            }
            
            // Start stream based on type
            if (this.streamType === 'mjpeg') {
                this.streamImg.src = this.streamUrl;
                this.isStreaming = true;
            } else if (this.streamType === 'jpeg') {
                // Start JPEG refresh interval
                this.refreshJpegStream();
                this.streamRefreshInterval = setInterval(() => {
                    this.refreshJpegStream();
                }, this.streamRefreshMs);
                this.isStreaming = true;
            }
        } else if (this.streamType === 'webrtc') {
            // Handle WebRTC streaming
            this.startWebRTCStream();
        }
        
        // Update UI
        this.updateStreamingUI(true);
        
        // Send WebSocket message about stream start
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'camera_control',
                action: 'start',
                stream_type: this.streamType,
                resolution: this.resolution,
                frame_rate: this.frameRate
            });
        }
    }
    
    /**
     * Stop camera stream
     */
    stopStream() {
        // Clear any intervals
        if (this.streamRefreshInterval) {
            clearInterval(this.streamRefreshInterval);
            this.streamRefreshInterval = null;
        }
        
        // Clear reconnect timeout
        if (this.reconnectTimeout) {
            clearTimeout(this.reconnectTimeout);
            this.reconnectTimeout = null;
        }
        
        // Stop streaming based on type
        if (this.streamType === 'mjpeg' || this.streamType === 'jpeg') {
            if (this.streamImg) {
                this.streamImg.src = '';
            }
        } else if (this.streamType === 'webrtc') {
            this.stopWebRTCStream();
        }
        
        this.isStreaming = false;
        
        // Update UI
        this.updateStreamingUI(false);
        
        // Send WebSocket message about stream stop
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'camera_control',
                action: 'stop'
            });
        }
    }
    
    /**
     * Refresh JPEG stream by loading a new image
     */
    refreshJpegStream() {
        if (!this.streamImg || !this.isStreaming || this.streamType !== 'jpeg') return;
        
        // Add timestamp to prevent caching
        const timestamp = new Date().getTime();
        this.streamImg.src = `${this.streamUrl}&t=${timestamp}`;
    }
    
    /**
     * Start WebRTC stream
     */
    startWebRTCStream() {
        // Remove placeholder and image stream if present
        this.viewerContainer.querySelector('.camera-placeholder')?.remove();
        if (this.streamImg) {
            this.streamImg.remove();
            this.streamImg = null;
        }
        
        // Create video element for WebRTC
        this.streamVideo = document.createElement('video');
        this.streamVideo.className = 'camera-stream-video';
        this.streamVideo.autoplay = true;
        this.streamVideo.playsInline = true;
        this.streamVideo.muted = true;
        
        // Add to container
        this.viewerContainer.insertBefore(this.streamVideo, this.detectionOverlay);
        
        // Initialize WebRTC connection
        this.initWebRTC();
    }
    
    /**
     * Stop WebRTC stream
     */
    stopWebRTCStream() {
        if (this.streamVideo) {
            // Stop tracks
            if (this.streamVideo.srcObject) {
                const tracks = this.streamVideo.srcObject.getTracks();
                tracks.forEach(track => track.stop());
            }
            
            this.streamVideo.srcObject = null;
        }
    }
    
    /**
     * Initialize WebRTC connection
     */
    initWebRTC() {
        // This would be implemented according to your server's WebRTC implementation
        console.log('WebRTC initialization would happen here');
        
        // Typically would involve:
        // 1. Create RTCPeerConnection
        // 2. Add event listeners
        // 3. Create data channel
        // 4. Handle ICE candidates
        // 5. Create and handle offer/answer
    }
    
    /**
     * Toggle camera stream on/off
     */
    toggleStream() {
        if (this.isStreaming) {
            this.stopStream();
        } else {
            this.startStream();
        }
    }
    
    /**
     * Toggle object detection
     */
    toggleDetection() {
        this.detectionEnabled = !this.detectionEnabled;
        
        // Update UI
        const detectionButton = document.getElementById('detection-toggle');
        if (detectionButton) {
            if (this.detectionEnabled) {
                detectionButton.classList.add('active');
                detectionButton.innerHTML = '🔍 停止检测';
            } else {
                detectionButton.classList.remove('active');
                detectionButton.innerHTML = '🔍 目标检测';
                
                // Clear any detection boxes
                this.clearDetectionDisplay();
            }
        }
        
        // Send WebSocket message
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'vision_control',
                action: this.detectionEnabled ? 'start_detection' : 'stop_detection'
            });
        }
    }
    
    /**
     * Take snapshot from current camera feed
     */
    takeSnapshot() {
        if (!this.isStreaming) {
            window.app?.showNotification('摄像头未开启', 'error');
            return;
        }
        
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'camera_control',
                action: 'snapshot',
                resolution: this.resolution
            });
            
            window.app.showNotification('截图已保存', 'success');
        }
    }
    
    /**
     * Cycle through available resolutions
     */
    cycleResolution() {
        // Define available resolutions
        const availableResolutions = [
            { width: 320, height: 240, label: 'QVGA (320x240)' },
            { width: 640, height: 480, label: 'VGA (640x480)' },
            { width: 800, height: 600, label: 'SVGA (800x600)' },
            { width: 1280, height: 720, label: 'HD (1280x720)' }
        ];
        
        // Find current resolution index
        let currentIndex = availableResolutions.findIndex(
            res => res.width === this.resolution.width && res.height === this.resolution.height
        );
        
        // Move to next resolution or back to first
        currentIndex = (currentIndex + 1) % availableResolutions.length;
        this.resolution = availableResolutions[currentIndex];
        
        // Restart stream with new resolution if streaming
        if (this.isStreaming) {
            this.startStream(this.streamType);
        }
        
        // Show notification
        window.app?.showNotification(`分辨率已更改为 ${this.resolution.label}`, 'info');
    }
    
    /**
     * Show camera settings modal
     */
    showSettings() {
        // Create settings modal if it doesn't exist
        let settingsModal = document.getElementById('camera-settings-modal');
        
        if (!settingsModal) {
            // Create modal element
            settingsModal = document.createElement('div');
            settingsModal.id = 'camera-settings-modal';
            settingsModal.className = 'modal';
            
            // Create modal content
            settingsModal.innerHTML = `
                <div class="modal-content">
                    <div class="modal-header">
                        <h3>摄像头设置</h3>
                        <span class="close-modal">&times;</span>
                    </div>
                    <div class="modal-body">
                        <div class="form-group">
                            <label for="stream-type">流类型</label>
                            <select id="stream-type" class="form-control">
                                <option value="mjpeg" ${this.streamType === 'mjpeg' ? 'selected' : ''}>MJPEG</option>
                                <option value="jpeg" ${this.streamType === 'jpeg' ? 'selected' : ''}>JPEG</option>
                                <option value="webrtc" ${this.streamType === 'webrtc' ? 'selected' : ''}>WebRTC</option>
                            </select>
                        </div>
                        
                        <div class="form-group">
                            <label for="resolution">分辨率</label>
                            <select id="resolution" class="form-control">
                                <option value="320x240" ${this.resolution.width === 320 ? 'selected' : ''}>QVGA (320x240)</option>
                                <option value="640x480" ${this.resolution.width === 640 ? 'selected' : ''}>VGA (640x480)</option>
                                <option value="800x600" ${this.resolution.width === 800 ? 'selected' : ''}>SVGA (800x600)</option>
                                <option value="1280x720" ${this.resolution.width === 1280 ? 'selected' : ''}>HD (1280x720)</option>
                            </select>
                        </div>
                        
                        <div class="form-group">
                            <label for="frame-rate">帧率</label>
                            <select id="frame-rate" class="form-control">
                                <option value="5" ${this.frameRate === 5 ? 'selected' : ''}>5 fps</option>
                                <option value="10" ${this.frameRate === 10 ? 'selected' : ''}>10 fps</option>
                                <option value="15" ${this.frameRate === 15 ? 'selected' : ''}>15 fps</option>
                                <option value="20" ${this.frameRate === 20 ? 'selected' : ''}>20 fps</option>
                                <option value="30" ${this.frameRate === 30 ? 'selected' : ''}>30 fps</option>
                            </select>
                        </div>
                        
                        <div class="form-group">
                            <label for="jpeg-quality">JPEG质量</label>
                            <input type="range" id="jpeg-quality" class="form-control" min="1" max="100" value="80">
                            <div class="range-value"><span id="jpeg-quality-value">80</span>%</div>
                        </div>
                        
                        <div class="form-group">
                            <label>图像调整</label>
                            <div class="image-adjustments">
                                <div class="slider-container">
                                    <label>亮度 <span id="brightness-value">0</span></label>
                                    <input type="range" id="brightness" min="-2" max="2" step="0.1" value="0" class="slider">
                                </div>
                                
                                <div class="slider-container">
                                    <label>对比度 <span id="contrast-value">0</span></label>
                                    <input type="range" id="contrast" min="-2" max="2" step="0.1" value="0" class="slider">
                                </div>
                                
                                <div class="slider-container">
                                    <label>饱和度 <span id="saturation-value">0</span></label>
                                    <input type="range" id="saturation" min="-2" max="2" step="0.1" value="0" class="slider">
                                </div>
                            </div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button id="apply-settings" class="btn btn-primary">应用</button>
                        <button id="close-settings" class="btn btn-outline-primary">关闭</button>
                    </div>
                </div>
            `;
            
            // Add to document body
            document.body.appendChild(settingsModal);
            
            // Add event listeners
            settingsModal.querySelector('.close-modal').addEventListener('click', () => {
                settingsModal.style.display = 'none';
            });
            
            document.getElementById('close-settings').addEventListener('click', () => {
                settingsModal.style.display = 'none';
            });
            
            document.getElementById('apply-settings').addEventListener('click', () => {
                this.applySettings();
                settingsModal.style.display = 'none';
            });
            
            // Update range value displays
            document.getElementById('jpeg-quality').addEventListener('input', (e) => {
                document.getElementById('jpeg-quality-value').textContent = e.target.value;
            });
            
            document.getElementById('brightness').addEventListener('input', (e) => {
                document.getElementById('brightness-value').textContent = e.target.value;
            });
            
            document.getElementById('contrast').addEventListener('input', (e) => {
                document.getElementById('contrast-value').textContent = e.target.value;
            });
            
            document.getElementById('saturation').addEventListener('input', (e) => {
                document.getElementById('saturation-value').textContent = e.target.value;
            });
        }
        
        // Show modal
        settingsModal.style.display = 'block';
    }
    
    /**
     * Apply camera settings from modal
     */
    applySettings() {
        // Get values from settings form
        const streamType = document.getElementById('stream-type').value;
        const resolution = document.getElementById('resolution').value;
        const frameRate = parseInt(document.getElementById('frame-rate').value);
        const jpegQuality = parseInt(document.getElementById('jpeg-quality').value);
        const brightness = parseFloat(document.getElementById('brightness').value);
        const contrast = parseFloat(document.getElementById('contrast').value);
        const saturation = parseFloat(document.getElementById('saturation').value);
        
        // Parse resolution
        const [width, height] = resolution.split('x').map(Number);
        this.resolution.width = width;
        this.resolution.height = height;
        
        // Update other settings
        this.frameRate = frameRate;
        
        // Send settings to server
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'camera_control',
                action: 'update_settings',
                settings: {
                    resolution: { width, height },
                    frame_rate: frameRate,
                    jpeg_quality: jpegQuality,
                    brightness,
                    contrast,
                    saturation
                }
            });
        }
        
        // Restart stream with new settings if streaming
        if (this.isStreaming) {
            this.startStream(streamType);
        }
    }
    
    /**
     * Handle stream error
     */
    handleStreamError() {
        this.errorCount++;
        
        console.error(`Stream error occurred (${this.errorCount}/${this.maxErrors})`);
        
        // If exceeded max errors, stop trying
        if (this.errorCount >= this.maxErrors) {
            this.stopStream();
            window.app?.showNotification('摄像头连接失败，请检查设备', 'error');
            return;
        }
        
        // Otherwise, try to reconnect after a delay
        if (this.isStreaming) {
            this.reconnectTimeout = setTimeout(() => {
                if (this.isStreaming) {
                    console.log('Attempting to reconnect camera stream...');
                    this.startStream(this.streamType);
                }
            }, 2000);
        }
    }
    
    /**
     * Update streaming UI state
     * @param {boolean} isStreaming - Whether stream is active
     */
    updateStreamingUI(isStreaming) {
        const streamToggle = document.getElementById('stream-toggle');
        
        if (streamToggle) {
            if (isStreaming) {
                streamToggle.classList.add('active');
                streamToggle.innerHTML = '📹 停止视频';
            } else {
                streamToggle.classList.remove('active');
                streamToggle.innerHTML = '📹 开始视频';
            }
        }
        
        // Reset error count when stream state changes
        this.errorCount = 0;
    }
    
    /**
     * Handle camera status message from WebSocket
     * @param {Object} message - Status message
     */
    handleStatusMessage(message) {
        console.log('Camera status:', message);
        
        // Update camera status from server
        if (message.streaming !== undefined) {
            // Only update if not matching current state
            if (this.isStreaming !== message.streaming) {
                if (message.streaming) {
                    this.startStream(message.stream_type || this.streamType);
                } else {
                    this.stopStream();
                }
            }
        }
        
        // Update camera settings
        if (message.resolution) {
            this.resolution = message.resolution;
        }
        
        if (message.frame_rate) {
            this.frameRate = message.frame_rate;
        }
        
        // Update detection status
        if (message.detection_enabled !== undefined) {
            // Only update if not matching current state
            if (this.detectionEnabled !== message.detection_enabled) {
                this.detectionEnabled = message.detection_enabled;
                
                // Update detection toggle button
                const detectionButton = document.getElementById('detection-toggle');
                if (detectionButton) {
                    if (this.detectionEnabled) {
                        detectionButton.classList.add('active');
                        detectionButton.innerHTML = '🔍 停止检测';
                    } else {
                        detectionButton.classList.remove('active');
                        detectionButton.innerHTML = '🔍 目标检测';
                    }
                }
            }
        }
    }
    
    /**
     * Handle detection result message from WebSocket
     * @param {Object} message - Detection result message
     */
    handleDetectionResult(message) {
        if (!this.detectionEnabled) return;
        
        // Update tracked objects
        this.trackedObjects = message.detections || [];
        
        // Update detection display
        this.updateDetectionDisplay();
    }
    
    /**
     * Update detection display with bounding boxes
     */
    updateDetectionDisplay() {
        if (!this.detectionOverlay) return;
        
        // Clear previous detection boxes
        this.clearDetectionDisplay();
        
        // Add new detection boxes
        this.trackedObjects.forEach(obj => {
            const box = document.createElement('div');
            box.className = 'detection-box';
            
            // Calculate display position based on stream dimensions
            const streamElement = this.streamImg || this.streamVideo;
            if (!streamElement) return;
            
            const scaleX = streamElement.offsetWidth / this.resolution.width;
            const scaleY = streamElement.offsetHeight / this.resolution.height;
            
            // Set position and size
            box.style.left = `${obj.bbox.x * scaleX}px`;
            box.style.top = `${obj.bbox.y * scaleY}px`;
            box.style.width = `${obj.bbox.width * scaleX}px`;
            box.style.height = `${obj.bbox.height * scaleY}px`;
            
            // Set color based on confidence
            const hue = Math.min(120, Math.max(0, obj.confidence * 120));
            box.style.borderColor = `hsl(${hue}, 100%, 50%)`;
            
            // Add label
            const label = document.createElement('div');
            label.className = 'detection-label';
            label.textContent = `${obj.class} ${Math.round(obj.confidence * 100)}%`;
            label.style.backgroundColor = `hsla(${hue}, 100%, 50%, 0.8)`;
            box.appendChild(label);
            
            // Add to overlay
            this.detectionOverlay.appendChild(box);
            this.detectionBoxes.push(box);
        });
    }
    
    /**
     * Clear all detection boxes
     */
    clearDetectionDisplay() {
        if (!this.detectionOverlay) return;
        
        // Remove all existing detection boxes
        this.detectionBoxes.forEach(box => {
            box.remove();
        });
        
        this.detectionBoxes = [];
    }
}

/**
 * Initialize camera system
 * @param {string} viewerId - ID of the camera viewer element
 * @param {string} controlsId - ID of the camera controls element
 */
function initCamera(viewerId, controlsId) {
    return new CameraSystem(viewerId, controlsId);
}