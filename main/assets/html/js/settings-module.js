/**
 * Xiaozhi ESP32 Vehicle Control System
 * Settings Module
 */

// Settings management system
class SettingsManager {
    constructor() {
        // Settings state
        this.settings = {
            system: {
                deviceName: 'ESP32小车',
                autoConnect: true
            },
            vehicle: {
                motorMaxSpeed: 100,
                motorLeftTrim: 0,
                motorRightTrim: 0,
                servoPanMin: 0,
                servoPanMax: 180,
                servoTiltMin: 0,
                servoTiltMax: 180,
                ultrasonicThreshold: 20,
                enableObstacleAvoidance: true
            },
            camera: {
                resolution: '640x480',
                quality: 80,
                brightness: 0,
                contrast: 0,
                saturation: 0,
                hMirror: false,
                vFlip: false,
                detectionModel: 'yolo',
                detectionConfidence: 0.5,
                enableDetection: false
            },
            ai: {
                model: 'local',
                apiKey: '',
                apiEndpoint: 'https://api.openai.com/v1',
                apiModelName: 'gpt-3.5-turbo',
                speechRecognitionLanguage: 'zh-CN',
                enableContinuousRecognition: false
            },
            network: {
                currentWifi: '',
                wifiSSID: '',
                wifiPassword: '',
                apSSID: 'ESP32_AP',
                apPassword: '12345678',
                enableAP: true
            }
        };

        // UI elements
        this.tabButtons = document.querySelectorAll('.tab-button');
        this.tabContents = document.querySelectorAll('.tab-content');
        this.saveButton = document.getElementById('save-settings-btn');
        this.resetButton = document.getElementById('reset-settings-btn');

        // Initialize
        this.init();
    }

    /**
     * Initialize settings manager
     */
    init() {
        this.setupTabNavigation();
        this.loadSettings();
        this.setupEventListeners();
        this.bindSettingsInputs();
        
        // Register for WebSocket messages
        if (window.app) {
            window.app.registerWebSocketHandler('settings', this.handleSettingsUpdate.bind(this));
        }
        
        // Set as global instance
        window.settingsManager = this;
        
        console.log('Settings manager initialized');
    }

    /**
     * Setup tab navigation
     */
    setupTabNavigation() {
        if (!this.tabButtons) return;
        
        this.tabButtons.forEach(button => {
            button.addEventListener('click', () => {
                // Get tab ID
                const tabId = button.getAttribute('data-tab');
                this.showTab(tabId);
            });
        });
    }

    /**
     * Show specified tab
     * @param {string} tabId - The tab ID to show
     */
    showTab(tabId) {
        // Deactivate all tabs and buttons
        this.tabButtons.forEach(btn => btn.classList.remove('active'));
        this.tabContents.forEach(tab => tab.style.display = 'none');
        
        // Activate selected tab
        document.querySelector(`.tab-button[data-tab="${tabId}"]`).classList.add('active');
        document.getElementById(`${tabId}-tab`).style.display = 'block';
    }

    /**
     * Load settings from server
     */
    loadSettings() {
        // First load default settings
        this.updateUIFromSettings();
        
        // Then request current settings from server
        if (window.app && window.app.sendWebSocketMessage) {
            window.app.sendWebSocketMessage({
                type: 'get_settings'
            });
        } else {
            console.warn('WebSocket connection not available for settings');
        }
    }

    /**
     * Setup event listeners
     */
    setupEventListeners() {
        // Save button
        if (this.saveButton) {
            this.saveButton.addEventListener('click', () => {
                this.saveSettings();
            });
        }
        
        // Reset button
        if (this.resetButton) {
            this.resetButton.addEventListener('click', () => {
                this.resetSettings();
            });
        }
        
        // System settings
        const rebootBtn = document.getElementById('reboot-btn');
        if (rebootBtn) {
            rebootBtn.addEventListener('click', () => {
                if (confirm('确定要重启设备吗？')) {
                    this.rebootDevice();
                }
            });
        }
        
        const restoreBtn = document.getElementById('restore-btn');
        if (restoreBtn) {
            restoreBtn.addEventListener('click', () => {
                if (confirm('确定要恢复出厂设置吗？所有设置将被清除！')) {
                    this.restoreDevice();
                }
            });
        }
        
        // Firmware upload
        const uploadBtn = document.getElementById('upload-btn');
        if (uploadBtn) {
            uploadBtn.addEventListener('click', () => {
                this.uploadFirmware();
            });
        }
        
        // WiFi settings
        const connectWifiBtn = document.getElementById('connect-wifi-btn');
        if (connectWifiBtn) {
            connectWifiBtn.addEventListener('click', () => {
                this.connectWifi();
            });
        }
        
        const scanWifiBtn = document.getElementById('scan-wifi-btn');
        if (scanWifiBtn) {
            scanWifiBtn.addEventListener('click', () => {
                this.scanWifiNetworks();
            });
        }
        
        // AP settings
        const updateApBtn = document.getElementById('update-ap-btn');
        if (updateApBtn) {
            updateApBtn.addEventListener('click', () => {
                this.updateAPSettings();
            });
        }
        
        // Setup range input displays
        const rangeInputs = document.querySelectorAll('input[type="range"]');
        rangeInputs.forEach(input => {
            const valueDisplay = document.getElementById(`${input.id}-value`);
            if (valueDisplay) {
                input.addEventListener('input', () => {
                    let value = input.value;
                    
                    // Add units if applicable
                    if (input.id.includes('motor-max-speed')) {
                        value += '%';
                    } else if (input.id.includes('servo')) {
                        value += '°';
                    }
                    
                    valueDisplay.textContent = value;
                });
            }
        });
    }

    /**
     * Bind settings inputs to settings object
     */
    bindSettingsInputs() {
        // System settings
        this.bindInput('device-name', 'system.deviceName');
        
        // Vehicle settings
        this.bindInput('motor-max-speed', 'vehicle.motorMaxSpeed', true);
        this.bindInput('motor-left-trim', 'vehicle.motorLeftTrim', true);
        this.bindInput('motor-right-trim', 'vehicle.motorRightTrim', true);
        this.bindInput('servo-pan-min', 'vehicle.servoPanMin', true);
        this.bindInput('servo-pan-max', 'vehicle.servoPanMax', true);
        this.bindInput('servo-tilt-min', 'vehicle.servoTiltMin', true);
        this.bindInput('servo-tilt-max', 'vehicle.servoTiltMax', true);
        this.bindInput('ultrasonic-threshold', 'vehicle.ultrasonicThreshold', true);
        this.bindInput('enable-obstacle-avoidance', 'vehicle.enableObstacleAvoidance', false, true);
        
        // Camera settings
        this.bindInput('camera-resolution', 'camera.resolution');
        this.bindInput('camera-quality', 'camera.quality', true);
        this.bindInput('camera-brightness', 'camera.brightness', true);
        this.bindInput('camera-contrast', 'camera.contrast', true);
        this.bindInput('camera-saturation', 'camera.saturation', true);
        this.bindInput('camera-hmirror', 'camera.hMirror', false, true);
        this.bindInput('camera-vflip', 'camera.vFlip', false, true);
        this.bindInput('detection-model', 'camera.detectionModel');
        this.bindInput('detection-confidence', 'camera.detectionConfidence', true);
        this.bindInput('enable-detection', 'camera.enableDetection', false, true);
        
        // AI settings
        this.bindInput('ai-model', 'ai.model');
        this.bindInput('api-key', 'ai.apiKey');
        this.bindInput('api-endpoint', 'ai.apiEndpoint');
        this.bindInput('api-model-name', 'ai.apiModelName');
        this.bindInput('speech-recognition-language', 'ai.speechRecognitionLanguage');
        this.bindInput('enable-continuous-recognition', 'ai.enableContinuousRecognition', false, true);
        
        // Network settings
        this.bindInput('current-wifi', 'network.currentWifi');
        this.bindInput('wifi-ssid', 'network.wifiSSID');
        this.bindInput('wifi-password', 'network.wifiPassword');
        this.bindInput('ap-ssid', 'network.apSSID');
        this.bindInput('ap-password', 'network.apPassword');
        this.bindInput('enable-ap', 'network.enableAP', false, true);
    }

    /**
     * Bind input element to settings object
     * @param {string} elementId - Element ID
     * @param {string} settingsPath - Settings object path (e.g., 'system.deviceName')
     * @param {boolean} isNumber - Whether value should be treated as number
     * @param {boolean} isCheckbox - Whether input is checkbox
     */
    bindInput(elementId, settingsPath, isNumber = false, isCheckbox = false) {
        const element = document.getElementById(elementId);
        if (!element) return;
        
        // Set value from settings
        this.setInputValueFromSettings(element, settingsPath, isNumber, isCheckbox);
        
        // Add input event listener
        element.addEventListener(isCheckbox ? 'change' : 'input', () => {
            this.updateSettingFromInput(element, settingsPath, isNumber, isCheckbox);
        });
    }

    /**
     * Set input value from settings object
     * @param {HTMLElement} element - Input element
     * @param {string} path - Settings object path
     * @param {boolean} isNumber - Whether value should be treated as number
     * @param {boolean} isCheckbox - Whether input is checkbox
     */
    setInputValueFromSettings(element, path, isNumber, isCheckbox) {
        // Get value from settings
        const value = this.getNestedValue(this.settings, path);
        
        if (isCheckbox) {
            element.checked = Boolean(value);
        } else {
            element.value = value;
        }
    }

    /**
     * Update setting from input value
     * @param {HTMLElement} element - Input element
     * @param {string} path - Settings object path
     * @param {boolean} isNumber - Whether value should be treated as number
     * @param {boolean} isCheckbox - Whether input is checkbox
     */
    updateSettingFromInput(element, path, isNumber, isCheckbox) {
        let value;
        
        if (isCheckbox) {
            value = element.checked;
        } else {
            value = isNumber ? parseFloat(element.value) : element.value;
        }
        
        // Update setting
        this.setNestedValue(this.settings, path, value);
    }

    /**
     * Get nested value from object using dot notation path
     * @param {Object} obj - Object to get value from
     * @param {string} path - Path in dot notation (e.g., 'system.deviceName')
     * @returns {*} Value at path
     */
    getNestedValue(obj, path) {
        return path.split('.').reduce((value, key) => {
            return value && value[key] !== undefined ? value[key] : null;
        }, obj);
    }

    /**
     * Set nested value in object using dot notation path
     * @param {Object} obj - Object to set value in
     * @param {string} path - Path in dot notation (e.g., 'system.deviceName')
     * @param {*} value - Value to set
     */
    setNestedValue(obj, path, value) {
        const keys = path.split('.');
        const lastKey = keys.pop();
        const target = keys.reduce((value, key) => {
            return value[key] = value[key] || {};
        }, obj);
        
        target[lastKey] = value;
    }

    /**
     * Update UI from settings object
     */
    updateUIFromSettings() {
        // Update all bound inputs
        const updateInput = (elementId, path, isNumber = false, isCheckbox = false) => {
            const element = document.getElementById(elementId);
            if (element) {
                this.setInputValueFromSettings(element, path, isNumber, isCheckbox);
            }
        };
        
        // System settings
        updateInput('device-name', 'system.deviceName');
        
        // Vehicle settings
        updateInput('motor-max-speed', 'vehicle.motorMaxSpeed', true);
        updateInput('motor-left-trim', 'vehicle.motorLeftTrim', true);
        updateInput('motor-right-trim', 'vehicle.motorRightTrim', true);
        updateInput('servo-pan-min', 'vehicle.servoPanMin', true);
        updateInput('servo-pan-max', 'vehicle.servoPanMax', true);
        updateInput('servo-tilt-min', 'vehicle.servoTiltMin', true);
        updateInput('servo-tilt-max', 'vehicle.servoTiltMax', true);
        updateInput('ultrasonic-threshold', 'vehicle.ultrasonicThreshold', true);
        updateInput('enable-obstacle-avoidance', 'vehicle.enableObstacleAvoidance', false, true);
        
        // Camera settings
        updateInput('camera-resolution', 'camera.resolution');
        updateInput('camera-quality', 'camera.quality', true);
        updateInput('camera-brightness', 'camera.brightness', true);
        updateInput('camera-contrast', 'camera.contrast', true);
        updateInput('camera-saturation', 'camera.saturation', true);
        updateInput('camera-hmirror', 'camera.hMirror', false, true);
        updateInput('camera-vflip', 'camera.vFlip', false, true);
        updateInput('detection-model', 'camera.detectionModel');
        updateInput('detection-confidence', 'camera.detectionConfidence', true);
        updateInput('enable-detection', 'camera.enableDetection', false, true);
        
        // Update range slider displays
        document.getElementById('motor-max-speed-value').textContent = this.settings.vehicle.motorMaxSpeed + '%';
        document.getElementById('motor-left-trim-value').textContent = this.settings.vehicle.motorLeftTrim;
        document.getElementById('motor-right-trim-value').textContent = this.settings.vehicle.motorRightTrim;
        document.getElementById('servo-pan-min-value').textContent = this.settings.vehicle.servoPanMin + '°';
        document.getElementById('servo-pan-max-value').textContent = this.settings.vehicle.servoPanMax + '°';
        document.getElementById('servo-tilt-min-value').textContent = this.settings.vehicle.servoTiltMin + '°';
        document.getElementById('servo-tilt-max-value').textContent = this.settings.vehicle.servoTiltMax + '°';
        document.getElementById('camera-quality-value').textContent = this.settings.camera.quality;
        document.getElementById('camera-brightness-value').textContent = this.settings.camera.brightness;
        document.getElementById('camera-contrast-value').textContent = this.settings.camera.contrast;
        document.getElementById('camera-saturation-value').textContent = this.settings.camera.saturation;
        document.getElementById('detection-confidence-value').textContent = this.settings.camera.detectionConfidence;
        
        // AI settings
        updateInput('ai-model', 'ai.model');
        updateInput('api-key', 'ai.apiKey');
        updateInput('api-endpoint', 'ai.apiEndpoint');
        updateInput('api-model-name', 'ai.apiModelName');
        updateInput('speech-recognition-language', 'ai.speechRecognitionLanguage');
        updateInput('enable-continuous-recognition', 'ai.enableContinuousRecognition', false, true);
        
        // Network settings
        updateInput('current-wifi', 'network.currentWifi');
        updateInput('wifi-ssid', 'network.wifiSSID');
        updateInput('wifi-password', 'network.wifiPassword');
        updateInput('ap-ssid', 'network.apSSID');
        updateInput('ap-password', 'network.apPassword');
        updateInput('enable-ap', 'network.enableAP', false, true);
        
        // Update API settings visibility based on model type
        this.updateAPISettingsVisibility();
    }

    /**
     * Update API settings visibility based on selected model type
     */
    updateAPISettingsVisibility() {
        const apiSettings = document.getElementById('api-settings');
        if (!apiSettings) return;
        
        const modelType = this.settings.ai.model;
        
        if (modelType === 'local') {
            apiSettings.style.display = 'none';
        } else {
            apiSettings.style.display = 'block';
        }
    }

    /**
     * Save settings to server
     */
    saveSettings() {
        if (window.app && window.app.sendWebSocketMessage) {
            window.app.sendWebSocketMessage({
                type: 'update_settings',
                settings: this.settings
            });
            
            window.app.showNotification('设置已保存', 'success');
        } else {
            window.app?.showNotification('无法连接到服务器，设置未保存', 'error');
        }
    }

    /**
     * Reset settings to defaults
     */
    resetSettings() {
        if (confirm('确定要重置所有设置吗？')) {
            if (window.app && window.app.sendWebSocketMessage) {
                window.app.sendWebSocketMessage({
                    type: 'reset_settings'
                });
                
                window.app.showNotification('设置已重置为默认值', 'info');
            } else {
                window.app?.showNotification('无法连接到服务器，设置未重置', 'error');
            }
        }
    }

    /**
     * Reboot device
     */
    rebootDevice() {
        if (window.app && window.app.sendWebSocketMessage) {
            window.app.sendWebSocketMessage({
                type: 'system_control',
                action: 'reboot'
            });
            
            window.app.showNotification('设备正在重启...', 'info');
        } else {
            window.app?.showNotification('无法连接到服务器，设备未重启', 'error');
        }
    }

    /**
     * Restore device to factory settings
     */
    restoreDevice() {
        if (window.app && window.app.sendWebSocketMessage) {
            window.app.sendWebSocketMessage({
                type: 'system_control',
                action: 'restore'
            });
            
            window.app.showNotification('设备正在恢复出厂设置...', 'info');
        } else {
            window.app?.showNotification('无法连接到服务器，设备未恢复', 'error');
        }
    }

    /**
     * Upload firmware
     */
    uploadFirmware() {
        const fileInput = document.getElementById('firmware-file');
        if (!fileInput || !fileInput.files || fileInput.files.length === 0) {
            window.app?.showNotification('请选择固件文件', 'error');
            return;
        }
        
        const file = fileInput.files[0];
        const reader = new FileReader();
        
        reader.onload = (e) => {
            const fileData = e.target.result;
            
            if (window.app && window.app.sendWebSocketMessage) {
                // Show progress
                const progressContainer = document.getElementById('upload-progress');
                const progressBar = progressContainer.querySelector('.progress-bar');
                
                progressContainer.classList.remove('hidden');
                progressBar.style.width = '0%';
                progressBar.textContent = '0%';
                
                // Send firmware data
                window.app.sendWebSocketMessage({
                    type: 'firmware_update',
                    filename: file.name,
                    size: file.size,
                    data: fileData
                });
                
                window.app.showNotification('固件上传中...', 'info');
            } else {
                window.app?.showNotification('无法连接到服务器，固件未上传', 'error');
            }
        };
        
        reader.readAsDataURL(file);
    }

    /**
     * Connect to WiFi network
     */
    connectWifi() {
        const ssid = document.getElementById('wifi-ssid').value.trim();
        const password = document.getElementById('wifi-password').value;
        
        if (!ssid) {
            window.app?.showNotification('请输入WiFi名称', 'error');
            return;
        }
        
        if (window.app && window.app.sendWebSocketMessage) {
            window.app.sendWebSocketMessage({
                type: 'network_control',
                action: 'connect_wifi',
                ssid: ssid,
                password: password
            });
            
            window.app.showNotification(`正在连接到WiFi: ${ssid}...`, 'info');
        } else {
            window.app?.showNotification('无法连接到服务器，WiFi未连接', 'error');
        }
    }

    /**
     * Scan for WiFi networks
     */
    scanWifiNetworks() {
        if (window.app && window.app.sendWebSocketMessage) {
            window.app.sendWebSocketMessage({
                type: 'network_control',
                action: 'scan_wifi'
            });
            
            window.app.showNotification('正在扫描WiFi网络...', 'info');
        } else {
            window.app?.showNotification('无法连接到服务器，无法扫描WiFi', 'error');
        }
    }

    /**
     * Update AP settings
     */
    updateAPSettings() {
        const ssid = document.getElementById('ap-ssid').value.trim();
        const password = document.getElementById('ap-password').value;
        const enabled = document.getElementById('enable-ap').checked;
        
        if (!ssid) {
            window.app?.showNotification('请输入热点名称', 'error');
            return;
        }
        
        if (password && password.length < 8) {
            window.app?.showNotification('热点密码至少需要8个字符', 'error');
            return;
        }
        
        if (window.app && window.app.sendWebSocketMessage) {
            window.app.sendWebSocketMessage({
                type: 'network_control',
                action: 'update_ap',
                ssid: ssid,
                password: password,
                enabled: enabled
            });
            
            window.app.showNotification('热点设置已更新', 'success');
        } else {
            window.app?.showNotification('无法连接到服务器，热点设置未更新', 'error');
        }
    }

    /**
     * Handle settings update from server
     * @param {Object} message - Settings message
     */
    handleSettingsUpdate(message) {
        if (!message || !message.settings) return;
        
        // Update settings object
        this.settings = {...this.settings, ...message.settings};
        
        // Update UI
        this.updateUIFromSettings();
        
        // Update system info
        if (message.system_info) {
            this.updateSystemInfo(message.system_info);
        }
    }

    /**
     * Update system info display
     * @param {Object} systemInfo - System info object
     */
    updateSystemInfo(systemInfo) {
        const systemInfoTable = document.getElementById('system-info');
        if (!systemInfoTable) return;
        
        // Update system info fields
        const updateField = (label, value) => {
            const rows = systemInfoTable.querySelectorAll('tr');
            for (const row of rows) {
                const labelCell = row.querySelector('td:first-child');
                if (labelCell && labelCell.textContent === label) {
                    const valueCell = row.querySelector('td:last-child');
                    if (valueCell) {
                        valueCell.textContent = value;
                        return;
                    }
                }
            }
        };
        
        // Update known fields
        if (systemInfo.hostname) updateField('设备名称', systemInfo.hostname);
        if (systemInfo.version) updateField('固件版本', systemInfo.version);
        if (systemInfo.model) updateField('硬件型号', systemInfo.model);
        if (systemInfo.mac) updateField('MAC地址', systemInfo.mac);
        
        // Update device name input if available
        if (systemInfo.hostname) {
            const deviceNameInput = document.getElementById('device-name');
            if (deviceNameInput) {
                deviceNameInput.value = systemInfo.hostname;
            }
        }
        
        // Update current WiFi if available
        if (systemInfo.wifi && systemInfo.wifi.ssid) {
            const currentWifiInput = document.getElementById('current-wifi');
            if (currentWifiInput) {
                currentWifiInput.value = systemInfo.wifi.ssid;
            }
        }
    }
}

/**
 * Initialize settings manager
 */
document.addEventListener('DOMContentLoaded', function() {
    // Initialize settings manager when DOM is loaded
    new SettingsManager();
}); 