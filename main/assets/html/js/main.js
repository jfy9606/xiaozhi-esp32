/**
 * Xiaozhi ESP32 Vehicle Control System
 * Main JavaScript file for all vehicle functionality
 */

// Global variables
let webSocket = null;
let isConnected = false;
const wsClients = {};
let currentPage = null;
let systemConfig = {};

/**
 * Initialize the application
 * This should be called when the DOM is loaded
 */
function initApp() {
    console.log('Initializing vehicle control system...');
    
    // Set current page based on the HTML file name
    currentPage = window.location.pathname.split('/').pop().split('.')[0];
    if (!currentPage || currentPage === '') currentPage = 'index';
    
    // Initialize common UI elements
    initHeader();
    initStatusBar();
    initEventListeners();
    
    // Connect to WebSocket
    connectWebSocket();
    
    // Initialize page-specific modules
    switch (currentPage) {
        case 'index':
            // Dashboard components
            initDashboard();
            break;
            
        case 'vehicle':
            // Vehicle control components
            initCamera('camera-view', 'camera-controls');
            initVehicleControl('joystick-container', 'servo-controls');
            break;
            
        case 'location':
            // Location tracking components
            initLocationTracking('map-container');
            break;
            
        case 'ai':
            // AI chat interface
            initAIChat('chat-container');
            break;
            
        case 'settings':
            // Settings panel
            initSettings();
            break;
            
        default:
            console.log(`No specific initialization for page: ${currentPage}`);
    }
    
    // Log initialization completion
    console.log('Vehicle control system initialized');
}

/**
 * Initialize the header with navigation
 */
function initHeader() {
    const header = document.querySelector('.header');
    if (!header) return;
    
    // Highlight current page in navigation
    const navLinks = header.querySelectorAll('.nav-link');
    navLinks.forEach(link => {
        const href = link.getAttribute('href').split('.')[0].split('/').pop();
        if (href === currentPage || 
            (href === 'index' && currentPage === '')) {
            link.classList.add('active');
        }
    });
}

/**
 * Initialize status bar
 */
function initStatusBar() {
    const statusBar = document.querySelector('.status-bar');
    if (!statusBar) return;
    
    // Create connection status indicator if it doesn't exist
    if (!document.getElementById('connection-status')) {
        const statusIndicator = document.createElement('div');
        statusIndicator.id = 'connection-status';
        statusIndicator.className = 'status-indicator status-disconnected';
        statusIndicator.textContent = '未连接';
        statusBar.appendChild(statusIndicator);
    }
    
    // Update connection status
    updateConnectionStatus(isConnected);
}

/**
 * Initialize global event listeners
 */
function initEventListeners() {
    // Add global event listeners here
    
    // Example: Theme toggle button
    const themeToggle = document.getElementById('theme-toggle');
    if (themeToggle) {
        themeToggle.addEventListener('click', toggleTheme);
    }
    
    // Example: Tab buttons
    const tabButtons = document.querySelectorAll('.tab-button');
    tabButtons.forEach(button => {
        button.addEventListener('click', function() {
            const tabId = this.getAttribute('data-tab');
            showTab(tabId);
        });
    });
}

/**
 * Show a specific tab
 * @param {string} tabId - The ID of the tab to show
 */
function showTab(tabId) {
    // Hide all tab content
    const tabContents = document.querySelectorAll('.tab-content');
    tabContents.forEach(tab => tab.classList.remove('active'));
    
    // Deactivate all tab buttons
    const tabButtons = document.querySelectorAll('.tab-button');
    tabButtons.forEach(button => button.classList.remove('active'));
    
    // Activate selected tab
    document.getElementById(`${tabId}-tab`).classList.add('active');
    document.querySelector(`.tab-button[data-tab="${tabId}"]`).classList.add('active');
}

/**
 * Initialize dashboard components
 */
function initDashboard() {
    // System information
    fetchSystemInfo();
    
    // Status cards
    updateStatusCards();
    
    // Refresh system info periodically
    setInterval(fetchSystemInfo, 30000);
    
    console.log('Dashboard initialized');
}

/**
 * Fetch system information
 */
function fetchSystemInfo() {
    fetch('/api/system/info')
        .then(response => {
            if (!response.ok) {
                throw new Error(`HTTP error! Status: ${response.status}`);
            }
            return response.json();
        })
        .then(data => {
            updateSystemInfo(data);
        })
        .catch(error => {
            console.error('Error fetching system info:', error);
            // 使用默认数据以避免界面错误
            updateSystemInfo({
                hostname: 'ESP32 Device',
                version: '1.0.0',
                ip_address: window.location.hostname,
                mac: 'Unknown',
                free_heap: 0,
                uptime: 0
            });
        });
}

/**
 * Update system information display
 * @param {Object} data - System information data
 */
function updateSystemInfo(data) {
    const infoTable = document.getElementById('system-info');
    if (!infoTable) return;
    
    // Update system information table
    infoTable.innerHTML = '';
    
    // Add rows for each piece of information
    const infoItems = [
        { label: '设备名称', value: data.hostname },
        { label: '固件版本', value: data.version },
        { label: 'IP地址', value: data.ip_address },
        { label: 'MAC地址', value: data.mac },
        { label: '可用内存', value: formatBytes(data.free_heap) },
        { label: '运行时间', value: formatUptime(data.uptime) }
    ];
    
    infoItems.forEach(item => {
        const row = document.createElement('tr');
        const cell1 = document.createElement('td');
        const cell2 = document.createElement('td');
        
        cell1.textContent = item.label;
        cell2.textContent = item.value;
        
        row.appendChild(cell1);
        row.appendChild(cell2);
        infoTable.appendChild(row);
    });
}

/**
 * Format bytes to human readable form
 * @param {number} bytes - Number of bytes
 * @param {number} decimals - Decimal places (default: 2)
 * @returns {string} Formatted string
 */
function formatBytes(bytes, decimals = 2) {
    if (bytes === 0) return '0 Bytes';
    
    const k = 1024;
    const dm = decimals < 0 ? 0 : decimals;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    
    return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

/**
 * Format uptime to human readable form
 * @param {number} seconds - Uptime in seconds
 * @returns {string} Formatted string
 */
function formatUptime(seconds) {
    const days = Math.floor(seconds / (3600 * 24));
    const hours = Math.floor((seconds % (3600 * 24)) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = Math.floor(seconds % 60);
    
    let result = '';
    if (days > 0) result += `${days}天 `;
    if (hours > 0 || days > 0) result += `${hours}小时 `;
    if (minutes > 0 || hours > 0 || days > 0) result += `${minutes}分钟 `;
    result += `${secs}秒`;
    
    return result;
}

/**
 * Update status cards on the dashboard
 */
function updateStatusCards() {
    // Update various status cards
    // This would fetch data from different APIs
}

/**
 * Connect to the WebSocket
 */
function connectWebSocket() {
    if (webSocket && webSocket.readyState === WebSocket.OPEN) {
        console.log('WebSocket already connected');
        return;
    }
    
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    
    console.log(`Connecting to WebSocket: ${wsUrl}`);
    webSocket = new WebSocket(wsUrl);
    
    webSocket.onopen = function() {
        console.log('WebSocket connected');
        isConnected = true;
        updateConnectionStatus(true);
        
        // Register this client with the server
        sendWebSocketMessage({
            type: 'register',
            page: currentPage
        });
        
        // Initialize modules that need WebSocket
        initWebSocketModules();
    };
    
    webSocket.onclose = function() {
        console.log('WebSocket disconnected');
        isConnected = false;
        updateConnectionStatus(false);
        
        // Try to reconnect after 5 seconds
        setTimeout(connectWebSocket, 5000);
    };
    
    webSocket.onerror = function(error) {
        console.error('WebSocket error:', error);
    };
    
    webSocket.onmessage = function(event) {
        handleWebSocketMessage(event);
    };
}

/**
 * Handle incoming WebSocket messages
 * @param {MessageEvent} event - WebSocket message event
 */
function handleWebSocketMessage(event) {
    try {
        const message = JSON.parse(event.data);
        
        if (!message || !message.type) {
            console.warn('Received invalid message format');
            return;
        }
        
        console.log(`Received message type: ${message.type}`);
        
        // Route messages to appropriate handlers
        switch (message.type) {
            case 'system_status':
                handleSystemStatus(message);
                break;
                
            case 'sensor_data':
                handleSensorData(message);
                break;
                
            case 'camera_status':
                if (window.cameraSystem) {
                    window.cameraSystem.handleStatusMessage(message);
                }
                break;
                
            case 'vehicle_status':
                if (window.vehicleSystem) {
                    window.vehicleSystem.handleStatusMessage(message);
                }
                break;
                
            case 'location_update':
                if (window.locationSystem) {
                    window.locationSystem.handleLocationUpdate(message);
                }
                break;
                
            case 'ai_response':
                if (window.aiSystem) {
                    window.aiSystem.handleResponse(message);
                }
                break;
                
            case 'error':
                showNotification(message.message, 'error');
                break;
                
            default:
                // Forward to registered handlers if available
                const moduleHandler = wsClients[message.type];
                if (moduleHandler && typeof moduleHandler === 'function') {
                    moduleHandler(message);
                } else {
                    console.log(`No handler for message type: ${message.type}`);
                }
        }
    } catch (e) {
        console.error('Error parsing WebSocket message:', e);
    }
}

/**
 * Update the connection status display
 * @param {boolean} connected - Whether connected or not
 */
function updateConnectionStatus(connected) {
    const statusIndicator = document.getElementById('connection-status');
    if (!statusIndicator) return;
    
    if (connected) {
        statusIndicator.className = 'status-indicator status-connected';
        statusIndicator.textContent = '已连接';
    } else {
        statusIndicator.className = 'status-indicator status-disconnected';
        statusIndicator.textContent = '未连接';
    }
}

/**
 * Initialize modules that need WebSocket connection
 */
function initWebSocketModules() {
    // These will run after WebSocket is connected
    
    // Fetch system configuration
    sendWebSocketMessage({
        type: 'get_config'
    });
}

/**
 * Send message through WebSocket
 * @param {Object} message - The message object to send
 * @returns {boolean} Whether the message was sent
 */
function sendWebSocketMessage(message) {
    if (!webSocket || webSocket.readyState !== WebSocket.OPEN) {
        console.error('WebSocket is not connected');
        return false;
    }
    
    try {
        webSocket.send(JSON.stringify(message));
        return true;
    } catch (e) {
        console.error('Error sending WebSocket message:', e);
        return false;
    }
}

/**
 * Register a WebSocket message handler
 * @param {string} messageType - The message type to handle
 * @param {Function} handler - The handler function
 */
function registerWebSocketHandler(messageType, handler) {
    wsClients[messageType] = handler;
}

/**
 * Handle system status message
 * @param {Object} message - The system status message
 */
function handleSystemStatus(message) {
    // Update system status information
    systemConfig = message.config || systemConfig;
    
    // Update UI if needed
    if (currentPage === 'index' || currentPage === 'settings') {
        updateSystemInfo(message);
    }
}

/**
 * Handle sensor data message
 * @param {Object} message - The sensor data message
 */
function handleSensorData(message) {
    console.log('传感器数据消息:', message);
    
    // 更新距离信息
    if (message.distances) {
        const frontDist = message.distances.front !== undefined ? message.distances.front + 'cm' : '未知';
        const rearDist = message.distances.rear !== undefined ? message.distances.rear + 'cm' : '未知';
        $('#distance-value').text(`前方: ${frontDist}, 后方: ${rearDist}`);
    }
    
    // 更新光照信息（可用于演示调整信号强度）
    if (message.light !== undefined && !window.vehicleSystem) {
        // 只有在vehicleSystem未定义时才使用光照来演示信号强度
        let signalStrength = Math.min(100, Math.max(0, message.light / 10));
        let signalText = 'Very Poor';
        let signalIcon = 'bi-reception-0';
        
        if (signalStrength > 80) {
            signalText = 'Excellent';
            signalIcon = 'bi-reception-4';
        } else if (signalStrength > 60) {
            signalText = 'Good';
            signalIcon = 'bi-reception-3';
        } else if (signalStrength > 40) {
            signalText = 'Fair';
            signalIcon = 'bi-reception-2';
        } else if (signalStrength > 20) {
            signalText = 'Poor';
            signalIcon = 'bi-reception-1';
        }
        
        $('.signal-strength i').removeClass().addClass('bi ' + signalIcon);
        $('#signal-strength').text(signalText);
    }
    
    // 其他传感器数据处理...
}

/**
 * Show a notification
 * @param {string} message - Notification message
 * @param {string} type - Notification type (success, error, info)
 */
function showNotification(message, type = 'info') {
    // Check if notification container exists
    let notificationContainer = document.getElementById('notification-container');
    
    // Create if it doesn't exist
    if (!notificationContainer) {
        notificationContainer = document.createElement('div');
        notificationContainer.id = 'notification-container';
        notificationContainer.style.position = 'fixed';
        notificationContainer.style.top = '20px';
        notificationContainer.style.right = '20px';
        notificationContainer.style.zIndex = '9999';
        document.body.appendChild(notificationContainer);
    }
    
    // Create notification element
    const notification = document.createElement('div');
    notification.className = `notification ${type}`;
    notification.innerHTML = `
        <div class="notification-content">
            <span class="notification-message">${message}</span>
            <button class="notification-close">&times;</button>
        </div>
    `;
    
    // Style notification
    notification.style.backgroundColor = 
        type === 'success' ? '#4CAF50' : 
        type === 'error' ? '#F44336' : 
        '#2196F3';
    notification.style.color = 'white';
    notification.style.padding = '12px 16px';
    notification.style.margin = '0 0 10px 0';
    notification.style.borderRadius = '4px';
    notification.style.boxShadow = '0 2px 5px rgba(0,0,0,0.2)';
    notification.style.display = 'flex';
    notification.style.justifyContent = 'space-between';
    notification.style.alignItems = 'center';
    notification.style.opacity = '0';
    notification.style.transition = 'opacity 0.3s ease';
    
    // Add to container
    notificationContainer.appendChild(notification);
    
    // Fade in
    setTimeout(() => {
        notification.style.opacity = '1';
    }, 10);
    
    // Add close handler
    const closeButton = notification.querySelector('.notification-close');
    closeButton.addEventListener('click', () => {
        closeNotification(notification);
    });
    
    // Auto-hide after 5 seconds
    setTimeout(() => {
        closeNotification(notification);
    }, 5000);
}

/**
 * Close a notification
 * @param {HTMLElement} notification - The notification element
 */
function closeNotification(notification) {
    notification.style.opacity = '0';
    setTimeout(() => {
        notification.parentNode.removeChild(notification);
    }, 300);
}

/**
 * Toggle between light and dark theme
 */
function toggleTheme() {
    const body = document.body;
    body.classList.toggle('dark-theme');
    
    // Store preference
    const isDarkMode = body.classList.contains('dark-theme');
    localStorage.setItem('darkTheme', isDarkMode);
}

/**
 * Apply saved theme preference
 */
function applyThemePreference() {
    const isDarkMode = localStorage.getItem('darkTheme') === 'true';
    if (isDarkMode) {
        document.body.classList.add('dark-theme');
    }
}

// Initialize app when DOM is loaded
document.addEventListener('DOMContentLoaded', function() {
    applyThemePreference();
    initApp();
});

// Export common functions to window for global access
window.app = {
    showNotification,
    sendWebSocketMessage,
    registerWebSocketHandler,
    updateConnectionStatus,
    formatBytes,
    formatUptime,
    showTab
};

// 创建车辆系统对象
window.vehicleSystem = {
    handleStatusMessage: function(message) {
        console.log('车辆状态消息:', message);
        
        // 更新电池电量
        if (message.batteryLevel !== undefined) {
            this.updateBatteryLevel(message.batteryLevel);
        }
        
        // 更新车速
        if (message.speed !== undefined) {
            $('#speed-value').text(message.speed + ' m/s');
        }
        
        // 更新模式
        if (message.mode) {
            $('#mode-value').text(message.mode);
        }
        
        // 更新距离信息
        if (message.distances) {
            $('#distance-value').text(`前方: ${message.distances.front}cm, 后方: ${message.distances.rear}cm`);
        }
        
        // 更新车辆状态
        if (message.readyState) {
            this.updateVehicleStatus(message.readyState);
        }
        
        // 更新信号强度
        if (message.signal) {
            this.updateSignalStrength(message.signal);
        }
    },
    
    updateBatteryLevel: function(batteryLevel) {
        const batteryEl = $('#battery-level');
        if (!batteryEl.length) return;
        
        batteryEl.css('width', batteryLevel + '%').text(batteryLevel + '%');
        
        if (batteryLevel > 60) {
            batteryEl.removeClass('bg-warning bg-danger').addClass('bg-success');
        } else if (batteryLevel > 20) {
            batteryEl.removeClass('bg-success bg-danger').addClass('bg-warning');
        } else {
            batteryEl.removeClass('bg-success bg-warning').addClass('bg-danger');
        }
    },
    
    updateVehicleStatus: function(status) {
        const statusEl = $('#vehicle-status');
        if (!statusEl.length) return;
        
        statusEl.text(status);
        
        if (status === 'Ready') {
            statusEl.removeClass('bg-warning bg-danger').addClass('bg-success');
        } else if (status === 'Moving') {
            statusEl.removeClass('bg-success bg-danger').addClass('bg-warning');
        } else if (status === 'Error') {
            statusEl.removeClass('bg-success bg-warning').addClass('bg-danger');
        }
    },
    
    updateSignalStrength: function(signal) {
        const signalEl = $('#signal-strength');
        const signalIconEl = $('.signal-strength i');
        if (!signalEl.length) return;
        
        let signalIcon = 'bi-reception-0';
        
        if (signal === 'Excellent') {
            signalIcon = 'bi-reception-4';
        } else if (signal === 'Good') {
            signalIcon = 'bi-reception-3';
        } else if (signal === 'Fair') {
            signalIcon = 'bi-reception-2';
        } else if (signal === 'Poor') {
            signalIcon = 'bi-reception-1';
        }
        
        signalIconEl.removeClass().addClass('bi ' + signalIcon);
        signalEl.text(signal);
    },
    
    updateUltrasonicDisplay: function(data) {
        if (data.front !== undefined || data.rear !== undefined) {
            const frontDist = data.front !== undefined ? data.front + 'cm' : '未知';
            const rearDist = data.rear !== undefined ? data.rear + 'cm' : '未知';
            $('#distance-value').text(`前方: ${frontDist}, 后方: ${rearDist}`);
        }
    }
}; 