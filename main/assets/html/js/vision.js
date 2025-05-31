// DOM Elements
const videoStream = document.getElementById('video-stream');
const startButton = document.getElementById('start-stream');
const stopButton = document.getElementById('stop-stream');
const takeSnapshotButton = document.getElementById('take-snapshot');
const snapshotGallery = document.getElementById('snapshot-gallery');
const connectionStatus = document.getElementById('connection-status');
const statusText = document.getElementById('status-text');
const resolutionSelect = document.getElementById('resolution-select');
const qualitySlider = document.getElementById('quality-slider');
const qualityValue = document.getElementById('quality-value');
const brightnessSlider = document.getElementById('brightness-slider');
const brightnessValue = document.getElementById('brightness-value');
const contrastSlider = document.getElementById('contrast-slider');
const contrastValue = document.getElementById('contrast-value');
const saturationSlider = document.getElementById('saturation-slider');
const saturationValue = document.getElementById('saturation-value');
const detectObjectsButton = document.getElementById('detect-objects');
const detectFacesButton = document.getElementById('detect-faces');
const detectQRButton = document.getElementById('detect-qr');
const detectARButton = document.getElementById('detect-ar');
const detectionsContainer = document.getElementById('detections-container');
const detectionResults = document.getElementById('detection-results');

// Stream state
let isStreaming = false;
let isDetecting = false;
let currentDetectionMode = null;
let detectionInterval = null;
let snapshots = [];

// Initialize page
document.addEventListener('DOMContentLoaded', initPage);

function initPage() {
    // Setup event listeners
    setupEventListeners();
    
    // Initial connection status - disconnected
    updateConnectionStatus(false);
    
    // Connect to WebSocket
    connectWebSocket();
    
    // Load snapshots if available
    loadSnapshots();
    
    // Disconnect WebSocket when page closes
    window.addEventListener('beforeunload', () => {
        window.xiaozhi.api.disconnectAllWebSockets();
        if (detectionInterval) {
            clearInterval(detectionInterval);
        }
    });
}

function setupEventListeners() {
    // Video stream controls
    startButton.addEventListener('click', startStream);
    stopButton.addEventListener('click', stopStream);
    takeSnapshotButton.addEventListener('click', takeSnapshot);
    
    // Camera settings
    if (resolutionSelect) {
        resolutionSelect.addEventListener('change', updateResolution);
    }
    
    if (qualitySlider) {
        qualitySlider.addEventListener('input', () => {
            const value = qualitySlider.value;
            qualityValue.textContent = value;
        });
        
        qualitySlider.addEventListener('change', () => {
            updateCameraSetting('quality', parseInt(qualitySlider.value));
        });
    }
    
    if (brightnessSlider) {
        brightnessSlider.addEventListener('input', () => {
            const value = brightnessSlider.value;
            brightnessValue.textContent = value;
        });
        
        brightnessSlider.addEventListener('change', () => {
            updateCameraSetting('brightness', parseInt(brightnessSlider.value));
        });
    }
    
    if (contrastSlider) {
        contrastSlider.addEventListener('input', () => {
            const value = contrastSlider.value;
            contrastValue.textContent = value;
        });
        
        contrastSlider.addEventListener('change', () => {
            updateCameraSetting('contrast', parseInt(contrastSlider.value));
        });
    }
    
    if (saturationSlider) {
        saturationSlider.addEventListener('input', () => {
            const value = saturationSlider.value;
            saturationValue.textContent = value;
        });
        
        saturationSlider.addEventListener('change', () => {
            updateCameraSetting('saturation', parseInt(saturationSlider.value));
        });
    }
    
    // Object detection buttons
    if (detectObjectsButton) {
        detectObjectsButton.addEventListener('click', () => toggleDetectionMode('objects'));
    }
    
    if (detectFacesButton) {
        detectFacesButton.addEventListener('click', () => toggleDetectionMode('faces'));
    }
    
    if (detectQRButton) {
        detectQRButton.addEventListener('click', () => toggleDetectionMode('qr'));
    }
    
    if (detectARButton) {
        detectARButton.addEventListener('click', () => toggleDetectionMode('ar'));
    }
}

function connectWebSocket() {
    try {
        window.xiaozhi.api.connectCamera({
            onOpen: () => {
                updateConnectionStatus(true);
                addLogMessage('已连接到相机服务', 'success');
                
                // Get camera settings
                getCameraSettings();
            },
            onMessage: (data) => {
                handleCameraMessage(data);
            },
            onClose: () => {
                updateConnectionStatus(false);
                addLogMessage('与相机服务断开连接', 'error');
                stopStream();
            },
            onError: (error) => {
                addLogMessage(`WebSocket错误: ${error}`, 'error');
            }
        });
    } catch (error) {
        addLogMessage(`连接相机服务失败: ${error.message}`, 'error');
    }
}

function updateConnectionStatus(connected) {
    connectionStatus.className = `status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`;
    statusText.textContent = connected ? '已连接' : '未连接';
    
    // Enable/disable controls
    startButton.disabled = !connected;
    stopButton.disabled = true;
    takeSnapshotButton.disabled = true;
    
    if (detectObjectsButton) detectObjectsButton.disabled = true;
    if (detectFacesButton) detectFacesButton.disabled = true;
    if (detectQRButton) detectQRButton.disabled = true;
    if (detectARButton) detectARButton.disabled = true;
    
    const sliders = document.querySelectorAll('input[type="range"]');
    sliders.forEach(slider => {
        slider.disabled = !connected;
    });
    
    if (resolutionSelect) {
        resolutionSelect.disabled = !connected;
    }
}

function startStream() {
    if (isStreaming) return;
    
    try {
        window.xiaozhi.api.startCameraStream();
        addLogMessage('正在启动视频流...', 'info');
    } catch (error) {
        addLogMessage(`启动视频流失败: ${error.message}`, 'error');
    }
}

function stopStream() {
    if (!isStreaming) return;
    
    try {
        window.xiaozhi.api.stopCameraStream();
        addLogMessage('正在停止视频流...', 'info');
        
        // Stop detection if active
        if (isDetecting) {
            stopDetection();
        }
    } catch (error) {
        addLogMessage(`停止视频流失败: ${error.message}`, 'error');
    }
}

function handleCameraMessage(data) {
    if (data.status === 'ok') {
        if (data.cmd === 'stream_started') {
            isStreaming = true;
            startButton.disabled = true;
            stopButton.disabled = false;
            takeSnapshotButton.disabled = false;
            
            // Enable detection buttons
            if (detectObjectsButton) detectObjectsButton.disabled = false;
            if (detectFacesButton) detectFacesButton.disabled = false;
            if (detectQRButton) detectQRButton.disabled = false;
            if (detectARButton) detectARButton.disabled = false;
            
            // Update video source if provided
            if (data.stream_url) {
                videoStream.src = data.stream_url;
                videoStream.onerror = () => {
                    addLogMessage('视频流加载失败', 'error');
                };
            }
            
            addLogMessage('视频流已启动', 'success');
        } else if (data.cmd === 'stream_stopped') {
            isStreaming = false;
            startButton.disabled = false;
            stopButton.disabled = true;
            takeSnapshotButton.disabled = true;
            
            // Disable detection buttons
            if (detectObjectsButton) detectObjectsButton.disabled = true;
            if (detectFacesButton) detectFacesButton.disabled = true;
            if (detectQRButton) detectQRButton.disabled = true;
            if (detectARButton) detectARButton.disabled = true;
            
            // Clear video source
            videoStream.src = '';
            
            addLogMessage('视频流已停止', 'success');
        } else if (data.cmd === 'settings') {
            updateCameraSettingsUI(data.settings);
            addLogMessage('已更新相机设置', 'success');
        } else if (data.cmd === 'snapshot') {
            handleSnapshotResult(data);
        } else if (data.cmd === 'detection') {
            handleDetectionResult(data);
        }
    } else if (data.status === 'error') {
        addLogMessage(`错误: ${data.message}`, 'error');
    }
}

function updateCameraSettingsUI(settings) {
    // Update resolution select
    if (resolutionSelect && settings.available_resolutions) {
        // Clear existing options
        resolutionSelect.innerHTML = '';
        
        // Add new options
        settings.available_resolutions.forEach(res => {
            const option = document.createElement('option');
            option.value = res;
            option.textContent = res;
            if (settings.resolution === res) {
                option.selected = true;
            }
            resolutionSelect.appendChild(option);
        });
    }
    
    // Update sliders
    if (qualitySlider && settings.quality !== undefined) {
        qualitySlider.value = settings.quality;
        qualityValue.textContent = settings.quality;
    }
    
    if (brightnessSlider && settings.brightness !== undefined) {
        brightnessSlider.value = settings.brightness;
        brightnessValue.textContent = settings.brightness;
    }
    
    if (contrastSlider && settings.contrast !== undefined) {
        contrastSlider.value = settings.contrast;
        contrastValue.textContent = settings.contrast;
    }
    
    if (saturationSlider && settings.saturation !== undefined) {
        saturationSlider.value = settings.saturation;
        saturationValue.textContent = settings.saturation;
    }
}

function updateResolution() {
    const resolution = resolutionSelect.value;
    updateCameraSetting('resolution', resolution);
}

function updateCameraSetting(setting, value) {
    try {
        window.xiaozhi.api.updateCameraSetting(setting, value);
        addLogMessage(`正在更新相机设置: ${setting} = ${value}`, 'info');
    } catch (error) {
        addLogMessage(`更新相机设置失败: ${error.message}`, 'error');
    }
}

function getCameraSettings() {
    try {
        window.xiaozhi.api.getCameraSettings();
        addLogMessage('正在获取相机设置...', 'info');
    } catch (error) {
        addLogMessage(`获取相机设置失败: ${error.message}`, 'error');
    }
}

function takeSnapshot() {
    if (!isStreaming) return;
    
    try {
        window.xiaozhi.api.takeSnapshot();
        addLogMessage('正在拍摄快照...', 'info');
    } catch (error) {
        addLogMessage(`拍摄快照失败: ${error.message}`, 'error');
    }
}

function handleSnapshotResult(data) {
    if (data.image_data) {
        // Create new snapshot object
        const snapshot = {
            id: Date.now().toString(),
            data: data.image_data,
            timestamp: Date.now()
        };
        
        // Add to snapshots array
        snapshots.push(snapshot);
        
        // Save to storage
        saveSnapshots();
        
        // Add to gallery
        addSnapshotToGallery(snapshot);
        
        addLogMessage('快照拍摄成功', 'success');
    }
}

function addSnapshotToGallery(snapshot) {
    if (!snapshotGallery) return;
    
    const snapshotItem = document.createElement('div');
    snapshotItem.className = 'snapshot-item';
    snapshotItem.id = `snapshot-${snapshot.id}`;
    
    const img = document.createElement('img');
    img.className = 'snapshot-image';
    img.src = `data:image/jpeg;base64,${snapshot.data}`;
    img.alt = `快照 ${new Date(snapshot.timestamp).toLocaleString()}`;
    
    const actions = document.createElement('div');
    actions.className = 'snapshot-actions';
    
    const downloadBtn = document.createElement('button');
    downloadBtn.className = 'snapshot-action-button';
    downloadBtn.innerHTML = '<i class="fas fa-download"></i>';
    downloadBtn.title = '下载图片';
    downloadBtn.onclick = () => downloadSnapshot(snapshot);
    
    const deleteBtn = document.createElement('button');
    deleteBtn.className = 'snapshot-action-button';
    deleteBtn.innerHTML = '<i class="fas fa-trash"></i>';
    deleteBtn.title = '删除快照';
    deleteBtn.onclick = () => deleteSnapshot(snapshot.id);
    
    actions.appendChild(downloadBtn);
    actions.appendChild(deleteBtn);
    
    snapshotItem.appendChild(img);
    snapshotItem.appendChild(actions);
    
    snapshotGallery.prepend(snapshotItem);
}

function downloadSnapshot(snapshot) {
    const link = document.createElement('a');
    link.href = `data:image/jpeg;base64,${snapshot.data}`;
    link.download = `snapshot-${snapshot.timestamp}.jpg`;
    link.click();
}

function deleteSnapshot(id) {
    // Remove from array
    const index = snapshots.findIndex(s => s.id === id);
    if (index !== -1) {
        snapshots.splice(index, 1);
        
        // Save updated array
        saveSnapshots();
        
        // Remove from DOM
        const snapshotElement = document.getElementById(`snapshot-${id}`);
        if (snapshotElement) {
            snapshotElement.remove();
        }
        
        addLogMessage('快照已删除', 'info');
    }
}

function saveSnapshots() {
    try {
        // Limit data size by only keeping the last 20 snapshots
        const limitedSnapshots = snapshots.slice(-20);
        localStorage.setItem('snapshots', JSON.stringify(limitedSnapshots));
    } catch (error) {
        console.error('保存快照失败:', error);
        addLogMessage('保存快照失败，可能是由于存储空间限制', 'error');
    }
}

function loadSnapshots() {
    try {
        const savedSnapshots = localStorage.getItem('snapshots');
        if (savedSnapshots) {
            snapshots = JSON.parse(savedSnapshots);
            
            // Add to gallery
            snapshots.forEach(snapshot => {
                addSnapshotToGallery(snapshot);
            });
            
            addLogMessage(`加载了 ${snapshots.length} 个快照`, 'info');
        }
    } catch (error) {
        console.error('加载快照失败:', error);
    }
}

function toggleDetectionMode(mode) {
    if (currentDetectionMode === mode) {
        stopDetection();
    } else {
        startDetection(mode);
    }
}

function startDetection(mode) {
    if (!isStreaming) return;
    
    try {
        // Stop any existing detection
        stopDetection();
        
        // Set new detection mode
        currentDetectionMode = mode;
        isDetecting = true;
        
        // Update button states
        updateDetectionButtonStates();
        
        // Start detection
        window.xiaozhi.api.startDetection(mode);
        addLogMessage(`开始${getDetectionModeName(mode)}检测`, 'info');
        
        // Clear any existing detection results
        clearDetectionResults();
    } catch (error) {
        addLogMessage(`启动检测失败: ${error.message}`, 'error');
    }
}

function stopDetection() {
    if (!isDetecting) return;
    
    try {
        window.xiaozhi.api.stopDetection();
        isDetecting = false;
        currentDetectionMode = null;
        
        // Update button states
        updateDetectionButtonStates();
        
        // Clear detection results
        clearDetectionResults();
        
        addLogMessage('停止检测', 'info');
    } catch (error) {
        addLogMessage(`停止检测失败: ${error.message}`, 'error');
    }
}

function updateDetectionButtonStates() {
    // Reset all buttons
    if (detectObjectsButton) {
        detectObjectsButton.classList.remove('active');
        detectObjectsButton.disabled = !isStreaming;
    }
    
    if (detectFacesButton) {
        detectFacesButton.classList.remove('active');
        detectFacesButton.disabled = !isStreaming;
    }
    
    if (detectQRButton) {
        detectQRButton.classList.remove('active');
        detectQRButton.disabled = !isStreaming;
    }
    
    if (detectARButton) {
        detectARButton.classList.remove('active');
        detectARButton.disabled = !isStreaming;
    }
    
    // Set active button
    if (currentDetectionMode) {
        switch (currentDetectionMode) {
            case 'objects':
                if (detectObjectsButton) detectObjectsButton.classList.add('active');
                break;
            case 'faces':
                if (detectFacesButton) detectFacesButton.classList.add('active');
                break;
            case 'qr':
                if (detectQRButton) detectQRButton.classList.add('active');
                break;
            case 'ar':
                if (detectARButton) detectARButton.classList.add('active');
                break;
        }
    }
}

function getDetectionModeName(mode) {
    switch (mode) {
        case 'objects': return '物体';
        case 'faces': return '人脸';
        case 'qr': return '二维码';
        case 'ar': return 'AR标记';
        default: return '';
    }
}

function clearDetectionResults() {
    // Clear detection boxes
    const boxes = document.querySelectorAll('.detection-box');
    boxes.forEach(box => box.remove());
    
    // Clear results list
    if (detectionResults) {
        detectionResults.innerHTML = '';
    }
}

function handleDetectionResult(data) {
    if (!isDetecting) return;
    
    clearDetectionResults();
    
    if (data.detections && data.detections.length > 0) {
        if (currentDetectionMode === 'qr') {
            // QR code results are text values
            showTextResults(data.detections);
        } else {
            // Show bounding boxes for objects/faces/AR markers
            showBoxResults(data.detections);
        }
    }
}

function showTextResults(detections) {
    if (!detectionResults) return;
    
    const list = document.createElement('ul');
    
    detections.forEach(detection => {
        const item = document.createElement('li');
        item.textContent = detection.value || detection.text || JSON.stringify(detection);
        list.appendChild(item);
    });
    
    detectionResults.appendChild(list);
}

function showBoxResults(detections) {
    if (!detectionsContainer) return;
    
    // Get video container dimensions
    const containerRect = videoStream.getBoundingClientRect();
    const containerWidth = containerRect.width;
    const containerHeight = containerRect.height;
    
    detections.forEach(detection => {
        // Create bounding box
        const box = document.createElement('div');
        box.className = 'detection-box';
        
        // Scale coordinates to container size
        const x = detection.x * containerWidth;
        const y = detection.y * containerHeight;
        const width = detection.width * containerWidth;
        const height = detection.height * containerHeight;
        
        box.style.left = `${x}px`;
        box.style.top = `${y}px`;
        box.style.width = `${width}px`;
        box.style.height = `${height}px`;
        
        // Add label if available
        if (detection.label || detection.class) {
            const label = document.createElement('div');
            label.className = 'detection-label';
            label.textContent = detection.label || detection.class;
            
            // Add confidence if available
            if (detection.confidence) {
                label.textContent += ` (${Math.round(detection.confidence * 100)}%)`;
            }
            
            box.appendChild(label);
        }
        
        detectionsContainer.appendChild(box);
        
        // Also add to text results
        if (detectionResults) {
            const item = document.createElement('div');
            item.innerHTML = `
                <strong>${detection.label || detection.class || '未知'}</strong>
                ${detection.confidence ? ` - ${Math.round(detection.confidence * 100)}%` : ''}
            `;
            detectionResults.appendChild(item);
        }
    });
}

function addLogMessage(message, type = 'info') {
    console.log(`[${type}] ${message}`);
} 