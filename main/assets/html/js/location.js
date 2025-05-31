// DOM Elements
const mapElement = document.getElementById('map');
const startTrackingBtn = document.getElementById('start-tracking');
const stopTrackingBtn = document.getElementById('stop-tracking');
const connectionStatus = document.getElementById('connection-status');
const statusText = document.getElementById('status-text');
const logContainer = document.getElementById('log-container');
const latitudeElement = document.getElementById('latitude');
const longitudeElement = document.getElementById('longitude');
const altitudeElement = document.getElementById('altitude');
const accuracyElement = document.getElementById('accuracy');
const speedElement = document.getElementById('speed');
const headingElement = document.getElementById('heading');
const timestampElement = document.getElementById('timestamp');

// Location tracking state
let map = null;
let currentMarker = null;
let trackingPath = null;
let isTracking = false;
let locationHistory = [];
let waypointMarkers = [];

// Initialize page
document.addEventListener('DOMContentLoaded', initPage);

function initPage() {
    // Setup event listeners
    setupEventListeners();
    
    // Initial connection status - disconnected
    updateConnectionStatus(false);
    
    // Initialize map
    initializeMap();
    
    // Connect to WebSocket
    connectWebSocket();
    
    // Load saved waypoints if available
    loadWaypoints();
    
    // Disconnect WebSocket when page closes
    window.addEventListener('beforeunload', () => {
        window.xiaozhi.api.disconnectAllWebSockets();
    });
}

function setupEventListeners() {
    // Start tracking button
    startTrackingBtn.addEventListener('click', startLocationTracking);
    
    // Stop tracking button
    stopTrackingBtn.addEventListener('click', stopLocationTracking);
    
    // Setup waypoint form if exists
    const waypointForm = document.getElementById('waypoint-form');
    if (waypointForm) {
        waypointForm.addEventListener('submit', (e) => {
            e.preventDefault();
            saveWaypoint();
        });
    }
}

function initializeMap() {
    // Initialize map with default location 
    // This is a placeholder and would normally use an actual map library like Leaflet or Google Maps
    try {
        // Example using Leaflet
        map = L.map(mapElement).setView([39.90, 116.40], 13); // Default to Beijing
        
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        }).addTo(map);
        
        // Create path for tracking
        trackingPath = L.polyline([], {
            color: '#0066cc',
            weight: 5,
            opacity: 0.7
        }).addTo(map);
        
        addLogEntry('地图初始化完成', 'success');
    } catch (error) {
        addLogEntry(`地图初始化失败: ${error.message}`, 'error');
    }
}

function connectWebSocket() {
    try {
        window.xiaozhi.api.connectLocation({
            onOpen: () => {
                updateConnectionStatus(true);
                addLogEntry('已连接到定位服务', 'success');
            },
            onMessage: (data) => {
                handleLocationData(data);
            },
            onClose: () => {
                updateConnectionStatus(false);
                addLogEntry('与定位服务断开连接', 'error');
                stopLocationTracking();
            },
            onError: (error) => {
                addLogEntry(`WebSocket错误: ${error}`, 'error');
            }
        });
    } catch (error) {
        addLogEntry(`连接定位服务失败: ${error.message}`, 'error');
    }
}

function updateConnectionStatus(connected) {
    connectionStatus.className = `status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`;
    statusText.textContent = connected ? '已连接' : '未连接';
    
    // Enable/disable controls
    startTrackingBtn.disabled = !connected;
    stopTrackingBtn.disabled = true;
}

function startLocationTracking() {
    if (isTracking) return;
    
    try {
        window.xiaozhi.api.startLocationTracking();
        isTracking = true;
        startTrackingBtn.disabled = true;
        stopTrackingBtn.disabled = false;
        addLogEntry('开始位置追踪', 'info');
    } catch (error) {
        addLogEntry(`开始位置追踪失败: ${error.message}`, 'error');
    }
}

function stopLocationTracking() {
    if (!isTracking) return;
    
    try {
        window.xiaozhi.api.stopLocationTracking();
        isTracking = false;
        startTrackingBtn.disabled = false;
        stopTrackingBtn.disabled = true;
        addLogEntry('停止位置追踪', 'info');
    } catch (error) {
        addLogEntry(`停止位置追踪失败: ${error.message}`, 'error');
    }
}

function handleLocationData(data) {
    if (data.status === 'ok') {
        const location = data.location;
        
        // Update location info display
        updateLocationInfo(location);
        
        // Update map with new location
        updateMapLocation(location);
        
        // Add to location history
        addLocationToHistory(location);
    } else if (data.status === 'error') {
        addLogEntry(`定位错误: ${data.message}`, 'error');
    }
}

function updateLocationInfo(location) {
    latitudeElement.textContent = location.latitude.toFixed(6);
    longitudeElement.textContent = location.longitude.toFixed(6);
    altitudeElement.textContent = location.altitude ? `${location.altitude.toFixed(1)}米` : '无数据';
    accuracyElement.textContent = location.accuracy ? `${location.accuracy.toFixed(1)}米` : '无数据';
    speedElement.textContent = location.speed ? `${location.speed.toFixed(1)}米/秒` : '无数据';
    headingElement.textContent = location.heading ? `${location.heading.toFixed(1)}°` : '无数据';
    timestampElement.textContent = new Date(location.timestamp).toLocaleString();
}

function updateMapLocation(location) {
    if (!map) return;
    
    const latlng = [location.latitude, location.longitude];
    
    // Update or create marker for current location
    if (currentMarker) {
        currentMarker.setLatLng(latlng);
    } else {
        currentMarker = L.marker(latlng, {
            icon: L.divIcon({
                className: 'current-location-marker',
                html: '<div class="marker-inner"></div>',
                iconSize: [20, 20]
            })
        }).addTo(map);
    }
    
    // Add point to tracking path
    trackingPath.addLatLng(latlng);
    
    // Pan map to current location
    map.panTo(latlng);
}

function addLocationToHistory(location) {
    locationHistory.push({
        latitude: location.latitude,
        longitude: location.longitude,
        timestamp: location.timestamp
    });
    
    // Limit history size to prevent memory issues
    if (locationHistory.length > 1000) {
        locationHistory.shift();
    }
}

function addLogEntry(message, type = 'info') {
    const logEntry = document.createElement('div');
    logEntry.className = `log-entry log-${type}`;
    
    const timestamp = new Date().toLocaleTimeString();
    const logTimestamp = document.createElement('span');
    logTimestamp.className = 'log-timestamp';
    logTimestamp.textContent = `[${timestamp}]`;
    
    logEntry.appendChild(logTimestamp);
    logEntry.appendChild(document.createTextNode(` ${message}`));
    
    logContainer.appendChild(logEntry);
    logContainer.scrollTop = logContainer.scrollHeight;
}

function saveWaypoint() {
    const nameInput = document.getElementById('waypoint-name');
    if (!nameInput || !nameInput.value.trim()) {
        addLogEntry('请输入航点名称', 'error');
        return;
    }
    
    // Get current location
    if (!currentMarker) {
        addLogEntry('当前位置不可用', 'error');
        return;
    }
    
    const latlng = currentMarker.getLatLng();
    const waypoint = {
        id: Date.now().toString(),
        name: nameInput.value.trim(),
        latitude: latlng.lat,
        longitude: latlng.lng,
        timestamp: Date.now()
    };
    
    // Save waypoint
    saveWaypointToStorage(waypoint);
    
    // Add waypoint marker to map
    addWaypointMarker(waypoint);
    
    // Clear input
    nameInput.value = '';
    
    addLogEntry(`保存航点: ${waypoint.name}`, 'success');
}

function saveWaypointToStorage(waypoint) {
    try {
        const savedWaypoints = localStorage.getItem('waypoints');
        let waypoints = savedWaypoints ? JSON.parse(savedWaypoints) : [];
        waypoints.push(waypoint);
        localStorage.setItem('waypoints', JSON.stringify(waypoints));
    } catch (error) {
        console.error('保存航点失败:', error);
    }
}

function loadWaypoints() {
    try {
        const savedWaypoints = localStorage.getItem('waypoints');
        if (savedWaypoints) {
            const waypoints = JSON.parse(savedWaypoints);
            waypoints.forEach(waypoint => {
                addWaypointMarker(waypoint);
            });
            addLogEntry(`加载了 ${waypoints.length} 个航点`, 'info');
        }
    } catch (error) {
        console.error('加载航点失败:', error);
    }
}

function addWaypointMarker(waypoint) {
    if (!map) return;
    
    const marker = L.marker([waypoint.latitude, waypoint.longitude], {
        title: waypoint.name
    }).addTo(map);
    
    marker.bindPopup(`
        <strong>${waypoint.name}</strong><br>
        纬度: ${waypoint.latitude.toFixed(6)}<br>
        经度: ${waypoint.longitude.toFixed(6)}<br>
        时间: ${new Date(waypoint.timestamp).toLocaleString()}
    `);
    
    // Store reference to marker
    waypointMarkers.push({
        id: waypoint.id,
        marker: marker
    });
    
    // Add to waypoint list in UI if it exists
    updateWaypointList();
}

function updateWaypointList() {
    const waypointListElement = document.getElementById('waypoint-list');
    if (!waypointListElement) return;
    
    try {
        const savedWaypoints = localStorage.getItem('waypoints');
        if (savedWaypoints) {
            const waypoints = JSON.parse(savedWaypoints);
            
            waypointListElement.innerHTML = '';
            
            waypoints.forEach(waypoint => {
                const waypointItem = document.createElement('div');
                waypointItem.className = 'waypoint-item';
                waypointItem.innerHTML = `
                    <div class="waypoint-info">
                        <strong>${waypoint.name}</strong><br>
                        ${waypoint.latitude.toFixed(6)}, ${waypoint.longitude.toFixed(6)}
                    </div>
                    <div class="waypoint-actions">
                        <button class="goto-waypoint" data-id="${waypoint.id}">查看</button>
                        <button class="delete-waypoint" data-id="${waypoint.id}">删除</button>
                    </div>
                `;
                
                waypointListElement.appendChild(waypointItem);
            });
            
            // Add event listeners to buttons
            const gotoButtons = document.querySelectorAll('.goto-waypoint');
            gotoButtons.forEach(button => {
                button.addEventListener('click', (e) => {
                    const waypointId = e.target.getAttribute('data-id');
                    gotoWaypoint(waypointId, waypoints);
                });
            });
            
            const deleteButtons = document.querySelectorAll('.delete-waypoint');
            deleteButtons.forEach(button => {
                button.addEventListener('click', (e) => {
                    const waypointId = e.target.getAttribute('data-id');
                    deleteWaypoint(waypointId);
                });
            });
        }
    } catch (error) {
        console.error('更新航点列表失败:', error);
    }
}

function gotoWaypoint(waypointId, waypoints) {
    if (!map) return;
    
    const waypoint = waypoints.find(wp => wp.id === waypointId);
    if (waypoint) {
        map.setView([waypoint.latitude, waypoint.longitude], 16);
        
        // Find and open popup for this waypoint
        waypointMarkers.forEach(markerInfo => {
            if (markerInfo.id === waypointId) {
                markerInfo.marker.openPopup();
            }
        });
    }
}

function deleteWaypoint(waypointId) {
    try {
        const savedWaypoints = localStorage.getItem('waypoints');
        if (savedWaypoints) {
            let waypoints = JSON.parse(savedWaypoints);
            
            // Find waypoint to delete
            const waypointIndex = waypoints.findIndex(wp => wp.id === waypointId);
            if (waypointIndex !== -1) {
                // Remove waypoint from array
                const deletedWaypoint = waypoints.splice(waypointIndex, 1)[0];
                
                // Save updated waypoints
                localStorage.setItem('waypoints', JSON.stringify(waypoints));
                
                // Remove marker from map
                const markerIndex = waypointMarkers.findIndex(m => m.id === waypointId);
                if (markerIndex !== -1) {
                    map.removeLayer(waypointMarkers[markerIndex].marker);
                    waypointMarkers.splice(markerIndex, 1);
                }
                
                // Update waypoint list in UI
                updateWaypointList();
                
                addLogEntry(`删除航点: ${deletedWaypoint.name}`, 'info');
            }
        }
    } catch (error) {
        console.error('删除航点失败:', error);
    }
} 