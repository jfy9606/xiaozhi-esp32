
// Legacy jQuery implementation
let vehicleMarker = null;
let pathLine = null;
let destinationMarker = null;
let socket = null;
let map = null;

// Check if Leaflet or Google Maps is being used
const useLeaflet = typeof L !== 'undefined';
const useGoogleMaps = typeof google !== 'undefined' && typeof google.maps !== 'undefined';

// Initialize when document is ready - legacy jQuery implementation
$(document).ready(function() {
    if (!window.locationSystem && useLeaflet) {
        console.log('Initializing legacy location system with Leaflet');
        initLegacyLocationSystem();
    } else if (!window.locationSystem && useGoogleMaps) {
        console.log('Initializing LocationSystem with Google Maps');
        window.locationSystem = new LocationSystem('location-container');
    }
});

// Initialize the legacy location system with Leaflet
function initLegacyLocationSystem() {
    // Initialize map
    map = L.map('map').setView([0, 0], 15);
    
    // Add tile layer (OpenStreetMap)
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
    }).addTo(map);
    
    // Vehicle marker
    const vehicleIcon = L.icon({
        iconUrl: 'img/vehicle-marker.png',
        iconSize: [32, 32],
        iconAnchor: [16, 16],
        popupAnchor: [0, -16]
    });
    
    vehicleMarker = L.marker([0, 0], { icon: vehicleIcon }).addTo(map);
    vehicleMarker.bindPopup('Vehicle Location');
    
    // Path line
    pathLine = L.polyline([], { color: 'blue', weight: 3 }).addTo(map);
    
    // WebSocket connection for real-time updates
    socket = new WebSocket(`ws://${window.location.host}/ws/location`);
    
    socket.onopen = function() {
        console.log('WebSocket connected');
        $('#status-badge').removeClass('d-none');
        $('#status-badge-disconnected').addClass('d-none');
    };
    
    socket.onclose = function() {
        console.log('WebSocket disconnected');
        $('#status-badge').addClass('d-none');
        $('#status-badge-disconnected').removeClass('d-none');
    };
    
    socket.onmessage = function(event) {
        const data = JSON.parse(event.data);
        
        if (data.type === 'location') {
            updateLocation(data.data);
        } else if (data.type === 'navigation') {
            updateNavigation(data.data);
        }
    };
    
    // Setup UI event handlers
    setupUIEventHandlers();
}

// Setup UI event handlers for legacy system
function setupUIEventHandlers() {
    // Center map button
    $('#btn-center-map').click(function() {
        const latLng = vehicleMarker.getLatLng();
        map.setView(latLng, 15);
    });
    
    // Clear path button
    $('#btn-clear-path').click(function() {
        pathLine.setLatLngs([]);
    });
    
    // Toggle tracking
    $('#toggle-tracking').change(function() {
        if ($(this).is(':checked')) {
            const latLng = vehicleMarker.getLatLng();
            map.setView(latLng, 15);
        }
    });
    
    // Navigation button
    $('#btn-navigate').click(function() {
        const destination = $('#destination').val();
        if (destination) {
            // In a real implementation, this would geocode the address
            // For now, we'll simulate a destination
            const vehicleLatLng = vehicleMarker.getLatLng();
            const destLat = vehicleLatLng.lat + (Math.random() * 0.01 - 0.005);
            const destLng = vehicleLatLng.lng + (Math.random() * 0.01 - 0.005);
            
            if (destinationMarker) {
                destinationMarker.setLatLng([destLat, destLng]);
            } else {
                destinationMarker = L.marker([destLat, destLng], {
                    icon: L.icon({
                        iconUrl: 'img/destination-marker.png',
                        iconSize: [32, 32],
                        iconAnchor: [16, 32],
                        popupAnchor: [0, -32]
                    })
                }).addTo(map);
                destinationMarker.bindPopup('Destination');
            }
            
            // Calculate distance
            const distance = map.distance([vehicleLatLng.lat, vehicleLatLng.lng], [destLat, destLng]);
            $('#distance-to-destination').text((distance / 1000).toFixed(2) + ' km');
            $('#estimated-time').text(Math.round(distance / 1000 / 5 * 60) + ' min'); // Assuming 5 km/h
            
            // Fit map to show both vehicle and destination
            const bounds = L.latLngBounds(vehicleLatLng, [destLat, destLng]);
            map.fitBounds(bounds, { padding: [50, 50] });
        }
    });
    
    // Start navigation button
    $('#btn-start-navigation').click(function() {
        if (destinationMarker) {
            const destLatLng = destinationMarker.getLatLng();
            sendCommand('start_navigation', {
                latitude: destLatLng.lat,
                longitude: destLatLng.lng
            });
            
            $(this).text('Stop Navigation').removeClass('btn-primary').addClass('btn-danger');
            $(this).attr('id', 'btn-stop-navigation');
        } else {
            alert('Please set a destination first');
        }
    });
    
    // Stop navigation button (dynamically added)
    $(document).on('click', '#btn-stop-navigation', function() {
        sendCommand('stop_navigation', {});
        $(this).text('Start Navigation').removeClass('btn-danger').addClass('btn-primary');
        $(this).attr('id', 'btn-start-navigation');
    });
    
    // Save location button
    $('#btn-save-location').click(function() {
        const label = prompt('Enter a name for this location:');
        if (label) {
            const latLng = vehicleMarker.getLatLng();
            sendCommand('save_location', {
                latitude: latLng.lat,
                longitude: latLng.lng,
                label: label
            });
        }
    });
}

// Update location data and map for legacy system
function updateLocation(location) {
    const lat = location.latitude;
    const lng = location.longitude;
    
    // Update text fields
    $('#current-lat').text(lat.toFixed(6));
    $('#current-lng').text(lng.toFixed(6));
    $('#altitude-value').text(location.altitude.toFixed(1) + ' m');
    $('#speed-value').text(location.speed.toFixed(1) + ' km/h');
    $('#heading-value').text(location.heading.toFixed(1) + '°');
    $('#accuracy-value').text(location.accuracy.toFixed(1) + ' m');
    $('#last-update').text(new Date().toLocaleTimeString());
    
    // Update vehicle marker position
    vehicleMarker.setLatLng([lat, lng]);
    
    // Add point to path
    pathLine.addLatLng([lat, lng]);
    
    // Center map if tracking is enabled
    if ($('#toggle-tracking').is(':checked')) {
        map.setView([lat, lng]);
    }
    
    // Update navigation info if destination is set
    if (destinationMarker) {
        const destLatLng = destinationMarker.getLatLng();
        const distance = map.distance([lat, lng], [destLatLng.lat, destLatLng.lng]);
        $('#distance-to-destination').text((distance / 1000).toFixed(2) + ' km');
        
        // Estimate time based on current speed
        if (location.speed > 0) {
            const timeInHours = distance / 1000 / location.speed;
            const timeInMinutes = timeInHours * 60;
            $('#estimated-time').text(Math.round(timeInMinutes) + ' min');
        } else {
            $('#estimated-time').text('N/A');
        }
    }
}

// Update navigation data for legacy system
function updateNavigation(navigation) {
    if (navigation.destination) {
        const lat = navigation.destination.latitude;
        const lng = navigation.destination.longitude;
        
        // Set or update destination marker
        if (destinationMarker) {
            destinationMarker.setLatLng([lat, lng]);
        } else {
            destinationMarker = L.marker([lat, lng], {
                icon: L.icon({
                    iconUrl: 'img/destination-marker.png',
                    iconSize: [32, 32],
                    iconAnchor: [16, 32],
                    popupAnchor: [0, -32]
                })
            }).addTo(map);
            destinationMarker.bindPopup('Destination');
        }
        
        // Update navigation info
        $('#distance-to-destination').text(navigation.distance.toFixed(2) + ' km');
        $('#estimated-time').text(navigation.estimatedTime + ' min');
        
        // Fit map to show both vehicle and destination
        const bounds = L.latLngBounds(vehicleMarker.getLatLng(), destinationMarker.getLatLng());
        map.fitBounds(bounds, { padding: [50, 50] });
    }
}

// Send command to the server for legacy system
function sendCommand(cmd, params) {
    if (socket && socket.readyState === WebSocket.OPEN) {
        const message = {
            cmd: cmd,
            ...params
        };
        socket.send(JSON.stringify(message));
    }
}

// OOP Implementation of Location Tracking System
class LocationSystem {
    constructor(containerId) {
        // DOM Elements
        this.container = document.getElementById(containerId);
        
        // Map elements
        this.map = null;
        this.vehicleMarker = null;
        this.pathPolyline = null;
        
        // Location data
        this.currentLocation = null;
        this.locationHistory = [];
        this.maxHistoryPoints = 100;
        
        // Navigation state
        this.isNavigating = false;
        this.navigationTarget = null;
        this.navigationPath = [];
        
        // Map settings
        this.settings = {
            defaultZoom: 18,
            mapType: 'roadmap', // roadmap, satellite, hybrid, terrain
            followVehicle: true,
            showHistory: true,
            historyLineColor: '#2196F3',
            historyLineWidth: 3,
            navigationLineColor: '#4CAF50',
            navigationLineWidth: 4
        };
        
        // Initialize
        if (this.container) {
            this.init();
        }
    }
    
    /**
     * Initialize location system
     */
    init() {
        this.createMapContainer();
        
        // Initialize map after ensuring container is ready
        setTimeout(() => {
            this.initMap();
        }, 100);
        
        // Register for WebSocket messages
        if (window.app) {
            window.app.registerWebSocketHandler('location_update', this.handleLocationUpdate.bind(this));
            window.app.registerWebSocketHandler('navigation_update', this.handleNavigationUpdate.bind(this));
        }
        
        // Set as global instance
        window.locationSystem = this;
        
        console.log('Location system initialized (OOP)');
    }
    
    /**
     * Create map container
     */
    createMapContainer() {
        if (!this.container) return;
        
        // Clear container
        this.container.innerHTML = '';
        
        // Add map div
        const mapDiv = document.createElement('div');
        mapDiv.id = 'map';
        mapDiv.style.width = '100%';
        mapDiv.style.height = '100%';
        this.container.appendChild(mapDiv);
        
        // Add control panel
        const controlPanel = document.createElement('div');
        controlPanel.className = 'map-controls';
        controlPanel.innerHTML = `
            <div class="btn-group">
                <button id="btn-map-type" class="btn btn-primary btn-sm">地图类型</button>
                <button id="btn-follow" class="btn btn-primary btn-sm active">自动跟随</button>
                <button id="btn-history" class="btn btn-primary btn-sm active">显示轨迹</button>
                <button id="btn-clear" class="btn btn-outline-primary btn-sm">清除轨迹</button>
            </div>
            <div class="coordinates">
                <div>经度: <span id="longitude">--</span></div>
                <div>纬度: <span id="latitude">--</span></div>
                <div>海拔: <span id="altitude">--</span> m</div>
                <div>精度: <span id="accuracy">--</span> m</div>
            </div>
        `;
        this.container.appendChild(controlPanel);
        
        // Setup control event listeners
        setTimeout(() => {
            this.setupMapControlListeners();
        }, 200);
    }
    
    /**
     * Initialize map
     */
    initMap() {
        // Check if Google Maps API is loaded
        if (!window.google || !window.google.maps) {
            console.error('Google Maps API not loaded');
            this.showMapError('地图API未加载，请检查网络连接');
        return;
    }
    
        // Default center (can be updated when first location is received)
        const center = {lat: 39.916527, lng: 116.397128}; // Beijing
        
        // Map options
        const options = {
            center: center,
            zoom: this.settings.defaultZoom,
            mapTypeId: this.settings.mapType,
            mapTypeControl: false,
            streetViewControl: false,
            fullscreenControl: true,
            zoomControl: true
        };
        
        // Create map
        this.map = new google.maps.Map(document.getElementById('map'), options);
        
        // Create vehicle marker (using default position until we get a real one)
        this.createVehicleMarker(center);
        
        // Create path polyline
        this.pathPolyline = new google.maps.Polyline({
            path: [],
            geodesic: true,
            strokeColor: this.settings.historyLineColor,
            strokeOpacity: 0.8,
            strokeWeight: this.settings.historyLineWidth,
            map: this.map,
            visible: this.settings.showHistory
        });
        
        // Setup map click event for navigation
        this.map.addListener('click', (event) => {
            this.handleMapClick(event.latLng);
        });
    }
    
    /**
     * Setup map control button listeners
     */
    setupMapControlListeners() {
        // Map type button
        const mapTypeBtn = document.getElementById('btn-map-type');
        if (mapTypeBtn) {
            mapTypeBtn.addEventListener('click', () => {
                this.cycleMapType();
            });
        }
        
        // Follow vehicle button
        const followBtn = document.getElementById('btn-follow');
        if (followBtn) {
            followBtn.addEventListener('click', () => {
                this.settings.followVehicle = !this.settings.followVehicle;
                followBtn.classList.toggle('active', this.settings.followVehicle);
                
                // Center map if turning follow on
                if (this.settings.followVehicle && this.currentLocation) {
                    this.centerMapOnVehicle();
                }
            });
        }
        
        // Show history button
        const historyBtn = document.getElementById('btn-history');
        if (historyBtn) {
            historyBtn.addEventListener('click', () => {
                this.settings.showHistory = !this.settings.showHistory;
                historyBtn.classList.toggle('active', this.settings.showHistory);
                
                // Update polyline visibility
                if (this.pathPolyline) {
                    this.pathPolyline.setVisible(this.settings.showHistory);
                }
            });
        }
        
        // Clear history button
        const clearBtn = document.getElementById('btn-clear');
        if (clearBtn) {
            clearBtn.addEventListener('click', () => {
                this.clearLocationHistory();
            });
        }
    }
    
    /**
     * Create vehicle marker
     * @param {Object} position - Initial position {lat, lng}
     */
    createVehicleMarker(position) {
        if (!this.map) return;
        
        // Create marker if it doesn't exist
        if (!this.vehicleMarker) {
            // Vehicle icon
            const icon = {
                path: google.maps.SymbolPath.FORWARD_CLOSED_ARROW,
                scale: 6,
                fillColor: '#F44336',
                fillOpacity: 1,
                strokeColor: '#B71C1C',
                strokeWeight: 2,
                rotation: 0
            };
            
            this.vehicleMarker = new google.maps.Marker({
                position: position,
                map: this.map,
                title: '我的车辆',
                icon: icon,
                optimized: false, // Important for smooth rotation
                zIndex: 10
            });
        } else {
            // Update position if marker exists
            this.vehicleMarker.setPosition(position);
        }
    }
    
    /**
     * Cycle through map types
     */
    cycleMapType() {
        if (!this.map) return;
        
        const mapTypes = [
            {id: 'roadmap', name: '道路图'},
            {id: 'satellite', name: '卫星图'},
            {id: 'hybrid', name: '混合图'},
            {id: 'terrain', name: '地形图'}
        ];
        
        // Find current type index
        let currentIndex = mapTypes.findIndex(type => type.id === this.settings.mapType);
        
        // Move to next type or back to first
        currentIndex = (currentIndex + 1) % mapTypes.length;
        this.settings.mapType = mapTypes[currentIndex].id;
        
        // Update map type
        this.map.setMapTypeId(this.settings.mapType);
        
        // Update button text
        const mapTypeBtn = document.getElementById('btn-map-type');
        if (mapTypeBtn) {
            mapTypeBtn.textContent = mapTypes[currentIndex].name;
        }
    }
    
    /**
     * Handle map click event
     * @param {google.maps.LatLng} latLng - Clicked location
     */
    handleMapClick(latLng) {
        // If already navigating, cancel current navigation
        if (this.isNavigating) {
            this.cancelNavigation();
            return;
        }
        
        // Set navigation target
        this.navigationTarget = {
            lat: latLng.lat(),
            lng: latLng.lng()
        };
        
        // Create or update marker for target
        if (!this.targetMarker) {
            this.targetMarker = new google.maps.Marker({
                position: this.navigationTarget,
                map: this.map,
                icon: {
                    path: google.maps.SymbolPath.CIRCLE,
                    scale: 7,
                    fillColor: '#4CAF50',
                    fillOpacity: 1,
                    strokeColor: '#388E3C',
                    strokeWeight: 2
                },
                title: '目标位置',
                animation: google.maps.Animation.DROP
            });
        } else {
            this.targetMarker.setPosition(this.navigationTarget);
            this.targetMarker.setVisible(true);
        }
        
        // Create navigation line if it doesn't exist
        if (!this.navigationLine) {
            this.navigationLine = new google.maps.Polyline({
                path: [
                    this.currentLocation || this.navigationTarget, // Start from current location if available
                    this.navigationTarget
                ],
                geodesic: true,
                strokeColor: this.settings.navigationLineColor,
                strokeOpacity: 0.8,
                strokeWeight: this.settings.navigationLineWidth,
                map: this.map
            });
        } else {
            this.navigationLine.setPath([
                this.currentLocation || this.navigationTarget,
                this.navigationTarget
            ]);
            this.navigationLine.setVisible(true);
        }
        
        // Show navigation confirmation dialog
        this.showNavigationConfirm();
    }
    
    /**
     * Show navigation confirmation dialog
     */
    showNavigationConfirm() {
        // Create confirmation modal
        const modal = document.createElement('div');
        modal.className = 'modal fade';
        modal.id = 'navigationConfirmModal';
        modal.innerHTML = `
            <div class="modal-dialog">
                <div class="modal-content">
                    <div class="modal-header">
                        <h5 class="modal-title">开始导航</h5>
                        <button type="button" class="close" data-dismiss="modal">&times;</button>
                    </div>
                    <div class="modal-body">
                        <p>是否导航到选定位置？</p>
                        <div class="nav-details">
                            ${this.currentLocation ? `
                                <p>距离: 约 ${this.calculateDistance(this.currentLocation, this.navigationTarget).toFixed(2)} 米</p>
                            ` : ''}
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-outline-primary" data-dismiss="modal">取消</button>
                        <button type="button" class="btn btn-primary" id="startNavBtn">开始导航</button>
                    </div>
                </div>
                    </div>
                `;
                
        document.body.appendChild(modal);
        
        // Initialize modal
        const modalInstance = new bootstrap.Modal(modal);
        modalInstance.show();
        
        // Add event listeners
        document.getElementById('startNavBtn').addEventListener('click', () => {
            this.startNavigation();
            modalInstance.hide();
        });
        
        modal.addEventListener('hidden.bs.modal', () => {
            // If not started navigation, clear markers
            if (!this.isNavigating) {
                if (this.targetMarker) {
                    this.targetMarker.setVisible(false);
                }
                if (this.navigationLine) {
                    this.navigationLine.setVisible(false);
                }
            }
            
            // Remove modal from DOM
            modal.remove();
        });
    }
    
    /**
     * Start navigation to target
     */
    startNavigation() {
        if (!this.navigationTarget) return;
        
        this.isNavigating = true;
        
        // Send navigation command to server
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'navigation_control',
                action: 'start',
                target: this.navigationTarget
            });
            
            window.app.showNotification('正在导航到目标位置', 'info');
        }
    }
    
    /**
     * Cancel current navigation
     */
    cancelNavigation() {
        this.isNavigating = false;
        
        // Hide navigation elements
        if (this.targetMarker) {
            this.targetMarker.setVisible(false);
        }
        
        if (this.navigationLine) {
            this.navigationLine.setVisible(false);
        }
        
        // Clear navigation path
        this.navigationPath = [];
        
        // Send cancel command to server
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'navigation_control',
                action: 'cancel'
            });
            
            window.app.showNotification('导航已取消', 'info');
        }
    }
    
    /**
     * Handle location update from server
     * @param {Object} message - Location update message
     */
    handleLocationUpdate(message) {
        if (!message.location) return;
        
        // Update current location
        this.currentLocation = message.location;
        
        // Add to history
        this.addLocationToHistory(message.location);
        
        // Update marker position and rotation
        this.updateVehiclePosition(message.location);
        
        // Update coordinates display
        this.updateCoordinatesDisplay(message.location);
        
        // Center map if following vehicle
        if (this.settings.followVehicle) {
            this.centerMapOnVehicle();
        }
        
        // Update navigation line if navigating
        if (this.isNavigating && this.navigationLine && this.navigationTarget) {
            this.navigationLine.setPath([
                this.currentLocation,
                this.navigationTarget
            ]);
        }
    }
    
    /**
     * Handle navigation update from server
     * @param {Object} message - Navigation update message
     */
    handleNavigationUpdate(message) {
        // Update navigation status
        if (message.status) {
            if (message.status === 'completed') {
                // Navigation completed
                this.isNavigating = false;
                window.app?.showNotification('已到达目标位置', 'success');
                
                // Hide navigation elements after a delay
                setTimeout(() => {
                    if (this.targetMarker) {
                        this.targetMarker.setVisible(false);
                    }
                    if (this.navigationLine) {
                        this.navigationLine.setVisible(false);
                    }
                }, 5000);
            } else if (message.status === 'failed') {
                // Navigation failed
                this.isNavigating = false;
                window.app?.showNotification('导航失败: ' + (message.reason || '未知原因'), 'error');
                
                // Hide navigation elements
                if (this.targetMarker) {
                    this.targetMarker.setVisible(false);
                }
                if (this.navigationLine) {
                    this.navigationLine.setVisible(false);
                }
            }
        }
        
        // Update navigation path
        if (message.path) {
            this.navigationPath = message.path;
            
            // Update navigation line if it exists
            if (this.navigationLine) {
                this.navigationLine.setPath(this.navigationPath);
            }
        }
    }
    
    /**
     * Add location to history
     * @param {Object} location - Location object {lat, lng, heading}
     */
    addLocationToHistory(location) {
        // Add to history array
        this.locationHistory.push(location);
        
        // Limit history size
        if (this.locationHistory.length > this.maxHistoryPoints) {
            this.locationHistory.shift();
        }
        
        // Update path polyline
        if (this.pathPolyline) {
            const path = this.locationHistory.map(loc => ({lat: loc.lat, lng: loc.lng}));
            this.pathPolyline.setPath(path);
        }
    }
    
    /**
     * Update vehicle position and rotation
     * @param {Object} location - Location object {lat, lng, heading}
     */
    updateVehiclePosition(location) {
        if (!this.map || !this.vehicleMarker) return;
        
        // Update marker position
        const position = new google.maps.LatLng(location.lat, location.lng);
        this.vehicleMarker.setPosition(position);
        
        // Update heading/rotation if available
        if (location.heading !== undefined) {
            const icon = this.vehicleMarker.getIcon();
            icon.rotation = location.heading;
            this.vehicleMarker.setIcon(icon);
        }
    }
    
    /**
     * Center map on vehicle position
     */
    centerMapOnVehicle() {
        if (!this.map || !this.currentLocation) return;
        
        this.map.setCenter({
            lat: this.currentLocation.lat, 
            lng: this.currentLocation.lng
        });
    }
    
    /**
     * Update coordinates display
     * @param {Object} location - Location object {lat, lng, heading, altitude, accuracy}
     */
    updateCoordinatesDisplay(location) {
        // Update coordinate display elements
        const latElement = document.getElementById('latitude');
        const lngElement = document.getElementById('longitude');
        const altElement = document.getElementById('altitude');
        const accElement = document.getElementById('accuracy');
        
        if (latElement) latElement.textContent = location.lat.toFixed(6);
        if (lngElement) lngElement.textContent = location.lng.toFixed(6);
        if (altElement && location.altitude !== undefined) {
            altElement.textContent = location.altitude.toFixed(1);
        }
        if (accElement && location.accuracy !== undefined) {
            accElement.textContent = location.accuracy.toFixed(1);
        }
    }
    
    /**
     * Clear location history
     */
    clearLocationHistory() {
        // Clear history array
        this.locationHistory = [];
        
        // Update path polyline
        if (this.pathPolyline) {
            this.pathPolyline.setPath([]);
        }
        
        window.app?.showNotification('轨迹已清除', 'info');
    }
    
    /**
     * Show error when map fails to load
     * @param {string} message - Error message to display
     */
    showMapError(message) {
        if (!this.container) return;
        
        // Create error message
        const errorDiv = document.createElement('div');
        errorDiv.className = 'map-error';
        errorDiv.innerHTML = `
            <div class="map-error-icon">⚠️</div>
            <div class="map-error-message">${message}</div>
            <div class="map-error-action">
                <button class="btn btn-primary" id="reload-map">重新加载</button>
            </div>
        `;
        
        // Clear container and add error
        this.container.innerHTML = '';
        this.container.appendChild(errorDiv);
        
        // Add reload button handler
        document.getElementById('reload-map').addEventListener('click', () => {
            window.location.reload();
        });
    }
    
    /**
     * Calculate distance between two coordinates in meters
     * @param {Object} coord1 - First coordinate {lat, lng}
     * @param {Object} coord2 - Second coordinate {lat, lng}
     * @returns {number} Distance in meters
     */
    calculateDistance(coord1, coord2) {
        // Haversine formula
        const R = 6371e3; // Earth radius in meters
        const φ1 = coord1.lat * Math.PI/180;
        const φ2 = coord2.lat * Math.PI/180;
        const Δφ = (coord2.lat - coord1.lat) * Math.PI/180;
        const Δλ = (coord2.lng - coord1.lng) * Math.PI/180;
        
        const a = Math.sin(Δφ/2) * Math.sin(Δφ/2) +
                Math.cos(φ1) * Math.cos(φ2) *
                Math.sin(Δλ/2) * Math.sin(Δλ/2);
        const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
        
        return R * c; // Distance in meters
    }
}

/**
 * Initialize location tracking - OOP method
 * @param {string} containerId - ID of the container element
 */
function initLocationTracking(containerId) {
    return new LocationSystem(containerId);
} 