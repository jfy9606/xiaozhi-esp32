/**
 * servo_control.js - 舵机控制模块
 * 控制ESP32上的舵机，提供角度调节和频率设置功能
 */

// 通道数量
const CHANNEL_COUNT = 16;

// DOM元素
const servoControlsContainer = document.getElementById('servo-controls');
const connectionStatus = document.getElementById('connection-status');
const statusText = document.getElementById('status-text');
const frequencyDisplay = document.getElementById('frequency-display');
const frequencyInput = document.getElementById('frequency-input');
const setFrequencyBtn = document.getElementById('set-frequency-btn');
const logContainer = document.getElementById('log-container');

// 存储舵机角度的数组
const servoAngles = Array(CHANNEL_COUNT).fill(90);

// 舵机控制器类
class ServoController {
    constructor() {
        // 初始化API客户端
        this.api = window.xiaozhi.api;
        this.utils = window.xiaozhi.utils;
        
        // 舵机控制状态
        this.servoStatus = {};
        this.isConnected = false;
        this.pendingUpdates = false;
        this.lastAngleUpdate = {}; // 记录最后一次角度更新时间
        this.updateThrottle = 50; // 更新节流间隔(毫秒)
        
        // 初始化UI元素引用
        this.angleSliders = document.querySelectorAll('.servo-angle');
        this.frequencySlider = document.getElementById('servo-frequency');
        this.statusLabels = document.querySelectorAll('.status-label');
        this.connectionStatus = document.getElementById('connection-status');

        // 添加UI事件监听器
        this._setupEventListeners();
        
        // 初始化WebSocket连接
        this._initWebSocket();
        
        // 加载初始状态
        this._loadInitialStatus();
    }
    
    /**
     * 初始化事件监听器
     * @private
     */
    _setupEventListeners() {
        // 舵机角度滑块变化事件
        this.angleSliders.forEach(slider => {
            slider.addEventListener('input', (e) => {
                const channel = parseInt(e.target.dataset.channel, 10);
                const angle = parseInt(e.target.value, 10);
                this._updateAngleDebounced(channel, angle);
            });
            
            slider.addEventListener('change', (e) => {
                const channel = parseInt(e.target.dataset.channel, 10);
                const angle = parseInt(e.target.value, 10);
                this._updateAngleImmediate(channel, angle);
            });
        });
        
        // 频率滑块变化事件
        if (this.frequencySlider) {
            this.frequencySlider.addEventListener('change', (e) => {
                const frequency = parseInt(e.target.value, 10);
                this.setFrequency(frequency);
            });
        }
        
        // 设置预设按钮事件
        const presetButtons = document.querySelectorAll('.preset-btn');
        presetButtons.forEach(button => {
            button.addEventListener('click', (e) => {
                const presetData = JSON.parse(e.target.dataset.preset);
                this.applyPreset(presetData);
            });
        });
        
        // 连接/断开按钮
        const connectBtn = document.getElementById('connect-btn');
        if (connectBtn) {
            connectBtn.addEventListener('click', () => {
                if (this.isConnected) {
                    this.disconnect();
                } else {
                    this.connect();
                }
            });
        }

        // 监听网络状态变化
        window.xiaozhi.utils.NetworkMonitor.onOnline(() => {
            if (!this.isConnected) {
                this._initWebSocket();
            }
        });
    }
    
    /**
     * 初始化WebSocket连接
     * @private
     */
    _initWebSocket() {
        this.utils.UI.showToast('正在连接舵机控制器...', 'info');
        this.updateConnectionStatus('connecting');
        
        try {
            // 连接舵机控制WebSocket
            this.ws = this.api.connectServoControl({
                onOpen: () => {
                    this.isConnected = true;
                    this.updateConnectionStatus('connected');
                    this.utils.UI.showToast('已连接到舵机控制器', 'success');
                    this._loadInitialStatus();
                },
                onClose: () => {
                    this.isConnected = false;
                    this.updateConnectionStatus('disconnected');
                    this.utils.UI.showToast('舵机控制器连接已断开', 'warning');
                },
                onError: (error, details) => {
                    this.isConnected = false;
                    this.updateConnectionStatus('error');
                    this.utils.UI.showToast(`舵机控制器连接错误: ${details}`, 'error');
                    this.utils.Logger.error(`舵机WebSocket错误: ${details}`, error);
                },
                onMessage: (data) => this._handleMessage(data),
                autoReconnect: true
            });
        } catch (error) {
            this.utils.Logger.error('初始化舵机控制WebSocket失败', error);
            this.updateConnectionStatus('error');
            this.utils.UI.showToast('连接舵机控制器失败', 'error');
        }
    }
    
    /**
     * 加载初始舵机状态
     * @private
     */
    _loadInitialStatus() {
        this.utils.UI.showLoader('加载舵机状态...');
        this.api.getServoStatus()
            .then(response => {
                if (response && response.status === 'success') {
                    this.servoStatus = response.data || {};
                    this._updateUI();
                    this.utils.Logger.info('舵机状态已加载', this.servoStatus);
                } else {
                    throw new Error(response.message || '加载舵机状态失败');
                }
            })
            .catch(error => {
                this.utils.Logger.error('加载舵机状态失败', error);
                this.utils.UI.showToast('加载舵机状态失败: ' + error.message, 'error');
            })
            .finally(() => {
                this.utils.UI.hideLoader();
            });
    }
    
    /**
     * 处理WebSocket消息
     * @private
     * @param {Object} data - 接收到的消息数据
     */
    _handleMessage(data) {
        try {
            if (!data || !data.type) {
                this.utils.Logger.warn('收到无效舵机消息', data);
                return;
            }
            
            switch (data.type) {
                case 'status_update':
                    this.servoStatus = data.data || this.servoStatus;
                    this._updateUI();
                    break;
                    
                case 'servo_moved':
                    this._updateServoPosition(data.channel, data.angle);
                    this.utils.Logger.debug(`舵机${data.channel}移动到${data.angle}度`);
                    break;
                    
                case 'frequency_changed':
                    this._updateFrequencyDisplay(data.frequency);
                    this.utils.Logger.debug(`舵机频率更新为${data.frequency}Hz`);
                    break;
                    
                case 'error':
                    this.utils.UI.showToast(`舵机控制错误: ${data.message}`, 'error');
                    this.utils.Logger.error('舵机控制错误', data);
                    break;
                    
                default:
                    this.utils.Logger.debug('收到未处理的舵机消息类型', data);
            }
            
        } catch (error) {
            this.utils.Logger.error('处理舵机消息出错', error);
        }
    }
    
    /**
     * 更新UI显示
     * @private
     */
    _updateUI() {
        // 更新舵机角度滑块
        if (this.servoStatus.angles) {
            Object.keys(this.servoStatus.angles).forEach(channel => {
                const angle = this.servoStatus.angles[channel];
                const slider = document.querySelector(`.servo-angle[data-channel="${channel}"]`);
                const display = document.querySelector(`.angle-display[data-channel="${channel}"]`);
                
                if (slider) {
                    slider.value = angle;
                }
                
                if (display) {
                    display.textContent = angle + '°';
                }
            });
        }
        
        // 更新频率显示
        if (this.servoStatus.frequency && this.frequencySlider) {
            this.frequencySlider.value = this.servoStatus.frequency;
            const frequencyDisplay = document.getElementById('frequency-display');
            if (frequencyDisplay) {
                frequencyDisplay.textContent = this.servoStatus.frequency + ' Hz';
            }
        }
    }
    
    /**
     * 更新舵机位置显示
     * @private
     * @param {number} channel - 舵机通道
     * @param {number} angle - 舵机角度
     */
    _updateServoPosition(channel, angle) {
        // 更新内部状态
        if (!this.servoStatus.angles) {
            this.servoStatus.angles = {};
        }
        this.servoStatus.angles[channel] = angle;
        
        // 更新UI
        const slider = document.querySelector(`.servo-angle[data-channel="${channel}"]`);
        const display = document.querySelector(`.angle-display[data-channel="${channel}"]`);
        
        if (slider && parseInt(slider.value, 10) !== angle) {
            slider.value = angle;
        }
        
        if (display) {
            display.textContent = angle + '°';
        }
    }
    
    /**
     * 更新频率显示
     * @private
     * @param {number} frequency - 频率值
     */
    _updateFrequencyDisplay(frequency) {
        // 更新内部状态
        this.servoStatus.frequency = frequency;
        
        // 更新UI
        if (this.frequencySlider) {
            this.frequencySlider.value = frequency;
        }
        
        const frequencyDisplay = document.getElementById('frequency-display');
        if (frequencyDisplay) {
            frequencyDisplay.textContent = frequency + ' Hz';
        }
    }
    
    /**
     * 节流更新舵机角度
     * @private
     * @param {number} channel - 舵机通道
     * @param {number} angle - 舵机角度
     */
    _updateAngleDebounced(channel, angle) {
        const now = Date.now();
        
        // 显示实时更新但不发送
        this._updateServoPosition(channel, angle);
        
        // 检查是否需要节流
        if (!this.lastAngleUpdate[channel] || 
            (now - this.lastAngleUpdate[channel]) > this.updateThrottle) {
            this._updateAngleImmediate(channel, angle);
        }
    }
    
    /**
     * 立即更新舵机角度
     * @private
     * @param {number} channel - 舵机通道
     * @param {number} angle - 舵机角度
     */
    _updateAngleImmediate(channel, angle) {
        // 记录更新时间
        this.lastAngleUpdate[channel] = Date.now();
        
        // 检查连接状态
        if (!this.isConnected) {
            this.utils.UI.showToast('舵机控制器未连接', 'warning');
            return;
        }
        
        try {
            // 使用WebSocket发送，启用批处理
            this.api.setServoAngleWs(channel, angle, true);
        } catch (error) {
            this.utils.Logger.error(`设置舵机${channel}角度失败: ${error.message}`, error);
            this.utils.UI.showToast(`设置舵机${channel}角度失败`, 'error');
            
            // 尝试重连
            if (!this.isConnected) {
                this._initWebSocket();
            }
        }
    }
    
    /**
     * 设置舵机PWM频率
     * @public
     * @param {number} frequency - 频率值(Hz)
     */
    setFrequency(frequency) {
        // 验证频率范围
        if (frequency < 50 || frequency > 300) {
            this.utils.UI.showToast(`频率值${frequency}Hz超出范围(50-300Hz)`, 'warning');
            return;
        }
        
        // 检查连接状态
        if (!this.isConnected) {
            this.utils.UI.showToast('舵机控制器未连接', 'warning');
            return;
        }
        
        try {
            // 使用WebSocket发送
            this.api.setServoFrequencyWs(frequency);
            this.utils.Logger.info(`设置舵机频率为${frequency}Hz`);
        } catch (error) {
            this.utils.Logger.error(`设置舵机频率失败: ${error.message}`, error);
            this.utils.UI.showToast('设置舵机频率失败', 'error');
        }
    }
    
    /**
     * 应用舵机预设
     * @public
     * @param {Object} preset - 预设数据，包含多个舵机角度
     */
    applyPreset(preset) {
        if (!preset || typeof preset !== 'object') {
            this.utils.Logger.warn('无效的预设数据', preset);
            return;
        }
        
        // 检查连接状态
        if (!this.isConnected) {
            this.utils.UI.showToast('舵机控制器未连接', 'warning');
            return;
        }
        
        try {
            this.utils.Logger.info('应用舵机预设', preset);
            
            // 标记开始批量更新
            this.pendingUpdates = true;
            
            // 应用每个通道的设置
            Object.keys(preset).forEach(channel => {
                const angle = preset[channel];
                
                // 更新UI
                this._updateServoPosition(parseInt(channel, 10), angle);
                
                // 发送命令
                try {
                    this.api.setServoAngleWs(parseInt(channel, 10), angle, true);
                } catch (error) {
                    this.utils.Logger.error(`设置舵机${channel}预设失败`, error);
                }
            });
            
            // 显示提示
            this.utils.UI.showToast('已应用舵机预设', 'success');
            
        } catch (error) {
            this.utils.Logger.error('应用预设失败', error);
            this.utils.UI.showToast('应用预设失败: ' + error.message, 'error');
        } finally {
            // 完成批量更新
            this.pendingUpdates = false;
        }
    }
    
    /**
     * 连接到舵机控制器
     * @public
     */
    connect() {
        if (!this.isConnected) {
            this._initWebSocket();
        }
    }
    
    /**
     * 断开与舵机控制器的连接
     * @public
     */
    disconnect() {
        if (this.isConnected) {
            try {
                this.api.disconnectWebSocket('/servo');
                this.isConnected = false;
                this.updateConnectionStatus('disconnected');
                this.utils.UI.showToast('已断开舵机控制器连接', 'info');
            } catch (error) {
                this.utils.Logger.error('断开舵机控制器连接失败', error);
                this.utils.UI.showToast('断开连接失败', 'error');
            }
        }
    }
    
    /**
     * 更新连接状态显示
     * @param {string} status - 连接状态 (connected|disconnected|connecting|error)
     */
    updateConnectionStatus(status) {
        if (!this.connectionStatus) return;
        
        // 移除现有状态类
        this.connectionStatus.classList.remove(
            'status-connected', 
            'status-disconnected', 
            'status-connecting',
            'status-error'
        );
        
        // 添加新状态类
        this.connectionStatus.classList.add('status-' + status);
        
        // 更新状态文本
        let statusText = '';
        switch(status) {
            case 'connected':
                statusText = '已连接';
                break;
            case 'disconnected':
                statusText = '已断开';
                break;
            case 'connecting':
                statusText = '连接中...';
                break;
            case 'error':
                statusText = '连接错误';
                break;
            default:
                statusText = status;
        }
        
        this.connectionStatus.textContent = statusText;
        
        // 更新连接按钮
        const connectBtn = document.getElementById('connect-btn');
        if (connectBtn) {
            connectBtn.textContent = this.isConnected ? '断开连接' : '连接';
            connectBtn.classList.toggle('btn-danger', this.isConnected);
            connectBtn.classList.toggle('btn-success', !this.isConnected);
        }
    }
}

// Hardware Manager Integration for Servo Control Page
class HardwareManagerIntegration {
    constructor() {
        this.sensorUpdateInterval = null;
        this.autoUpdateEnabled = false;
        this.initializeEventHandlers();
        this.initializeSensorMonitoring();
        this.initializeMotorControls();
    }
    
    initializeEventHandlers() {
        // Motor control sliders
        $('#motor-0-speed').on('input', (e) => {
            const speed = parseInt(e.target.value);
            $('#motor-0-value').text(speed);
            this.controlMotor(0, speed);
        });
        
        $('#motor-1-speed').on('input', (e) => {
            const speed = parseInt(e.target.value);
            $('#motor-1-value').text(speed);
            this.controlMotor(1, speed);
        });
        
        // Stop all motors button
        $('#stop-all-motors').click(() => {
            this.stopAllMotors();
        });
        
        // Sensor refresh button
        $('#refresh-sensors').click(() => {
            this.refreshSensors();
        });
        
        // Auto update toggle
        $('#toggle-sensor-auto-update').click(() => {
            this.toggleAutoUpdate();
        });
    }
    
    initializeSensorMonitoring() {
        // Initial sensor data load
        this.refreshSensors();
    }
    
    initializeMotorControls() {
        // Initialize motor status
        this.updateMotorStatus(0, false, 'Stopped');
        this.updateMotorStatus(1, false, 'Stopped');
    }
    
    controlMotor(motorId, speed) {
        if (window.HardwareManager) {
            window.HardwareManager.controlMotor(motorId, speed)
                .then(response => {
                    if (response.success) {
                        const statusText = speed === 0 ? 'Stopped' : 
                                         speed > 0 ? 'Forward' : 'Backward';
                        this.updateMotorStatus(motorId, speed !== 0, statusText);
                    }
                })
                .catch(error => {
                    console.error(`Failed to control motor ${motorId}:`, error);
                    this.updateMotorStatus(motorId, false, 'Error');
                });
        }
    }
    
    stopAllMotors() {
        $('#motor-0-speed').val(0);
        $('#motor-1-speed').val(0);
        $('#motor-0-value').text('0');
        $('#motor-1-value').text('0');
        
        this.controlMotor(0, 0);
        this.controlMotor(1, 0);
    }
    
    refreshSensors() {
        if (window.HardwareManager) {
            window.HardwareManager.getSensorData()
                .then(data => {
                    if (data.success && data.data && data.data.sensors) {
                        this.updateSensorDisplay(data.data.sensors);
                    }
                })
                .catch(error => {
                    console.error('Failed to refresh sensors:', error);
                });
        }
    }
    
    toggleAutoUpdate() {
        const button = $('#toggle-sensor-auto-update');
        const icon = button.find('i');
        
        if (this.autoUpdateEnabled) {
            // Disable auto update
            if (this.sensorUpdateInterval) {
                clearInterval(this.sensorUpdateInterval);
                this.sensorUpdateInterval = null;
            }
            this.autoUpdateEnabled = false;
            button.removeClass('btn-outline-primary').addClass('btn-outline-secondary');
            icon.removeClass('bi-pause-fill').addClass('bi-play-fill');
            button.find('span').text(' Auto Update');
        } else {
            // Enable auto update
            this.sensorUpdateInterval = setInterval(() => {
                this.refreshSensors();
            }, 2000); // Update every 2 seconds
            this.autoUpdateEnabled = true;
            button.removeClass('btn-outline-secondary').addClass('btn-outline-primary');
            icon.removeClass('bi-play-fill').addClass('bi-pause-fill');
            button.find('span').text(' Stop Auto');
        }
    }
    
    updateSensorDisplay(sensors) {
        // Update temperature sensor
        if (sensors.temperature !== undefined) {
            this.updateSensorStatus('temp-sensor', sensors.temperature_valid, 
                sensors.temperature.toFixed(1) + '°C');
        }
        
        // Update voltage sensor
        if (sensors.voltage !== undefined) {
            this.updateSensorStatus('voltage-sensor', sensors.voltage_valid,
                sensors.voltage.toFixed(2) + 'V');
        }
        
        // Update light sensor
        if (sensors.light !== undefined) {
            this.updateSensorStatus('light-sensor', sensors.light_valid,
                sensors.light.toString());
        }
        
        // Update motion sensor
        if (sensors.motion !== undefined) {
            const motionText = sensors.motion ? 'Motion Detected' : 'No Motion';
            this.updateSensorStatus('motion-sensor', sensors.motion_valid, motionText);
        }
    }
    
    updateSensorStatus(sensorId, valid, value) {
        const statusIcon = $(`#${sensorId}-status`);
        const valueElement = $(`#${sensorId}-value`);
        
        if (statusIcon.length) {
            statusIcon.removeClass('text-success text-danger text-secondary');
            if (valid) {
                statusIcon.addClass('text-success');
            } else {
                statusIcon.addClass('text-danger');
            }
        }
        
        if (valueElement.length) {
            valueElement.text(value);
        }
    }
    
    updateMotorStatus(motorId, active, statusText) {
        const statusIcon = $(`#motor-${motorId}-status`);
        const statusTextElement = $(`#motor-${motorId}-status-text`);
        
        if (statusIcon.length) {
            statusIcon.removeClass('text-success text-danger text-secondary');
            if (active) {
                statusIcon.addClass('text-success');
            } else {
                statusIcon.addClass('text-secondary');
            }
        }
        
        if (statusTextElement.length) {
            statusTextElement.text(statusText);
        }
    }
}

// 当页面加载完成时初始化舵机控制器
document.addEventListener('DOMContentLoaded', function() {
    // 确保API和工具库已加载
    if (window.xiaozhi && window.xiaozhi.api) {
        // 初始化控制器
        window.servoController = new ServoController();
        
        // 初始化硬件管理器集成
        setTimeout(() => {
            window.hardwareManagerIntegration = new HardwareManagerIntegration();
        }, 1000);
    } else {
        console.error('无法初始化舵机控制器: API客户端未加载');
        // 设置重试
        setTimeout(() => {
            if (window.xiaozhi && window.xiaozhi.api) {
                window.servoController = new ServoController();
                window.hardwareManagerIntegration = new HardwareManagerIntegration();
            } else {
                console.error('加载舵机控制器失败: API客户端不可用');
                alert('加载舵机控制组件失败，请刷新页面重试');
            }
        }, 1000);
    }
}); 