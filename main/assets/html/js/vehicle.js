/**
 * Xiaozhi ESP32 Vehicle Control System
 * Vehicle Control Module - 整合自 vehicle-control.js 和 vehicle.js
 */

document.addEventListener('DOMContentLoaded', function() {
    // 检查是否需要使用简单控制模式还是高级控制系统
    if (document.getElementById('joystick-zone')) {
        // 高级界面由 HTML 中的内联脚本处理
        console.log('高级 vehicle 控制由页面内联脚本处理');
    } else {
        // 简单控制模式
        initSimpleVehicleControl();
    }
});

// 简单控制模式实现
function initSimpleVehicleControl() {
    // 初始化变量
    let speed = 0.5; // 速度因子 (0.0-1.0)
    let joystickActive = false;
    let joystickX = 0;
    let joystickY = 0;
    const joystickContainer = document.querySelector('.joystick-container');
    const joystickKnob = document.querySelector('.joystick-knob');
    const speedSlider = document.querySelector('.speed-slider');
    const speedValue = document.querySelector('.speed-value');
    
    // 初始化WebSocket连接
    const socket = new WebSocket(`ws://${window.location.host}/ws`);
    
    socket.addEventListener('open', function() {
        console.log('WebSocket连接已建立');
        updateStatus('已连接');
    });
    
    socket.addEventListener('message', function(event) {
        try {
            const data = JSON.parse(event.data);
            
            // 处理超声波数据
            if (data.type === 'ultrasonic_data') {
                updateUltrasonicData(data);
            }
            
            // 处理舵机数据
            if (data.type === 'servo_data') {
                updateServoData(data);
            }
        } catch (e) {
            console.error('解析WebSocket消息失败:', e);
        }
    });
    
    socket.addEventListener('close', function() {
        console.log('WebSocket连接已关闭');
        updateStatus('已断开');
    });
    
    socket.addEventListener('error', function(error) {
        console.error('WebSocket错误:', error);
        updateStatus('连接错误');
    });
    
    // 初始化摇杆控制
    if (joystickContainer && joystickKnob) {
        initJoystick();
    }
    
    // 初始化速度滑块
    if (speedSlider) {
        speedSlider.value = speed * 100;
        if (speedValue) {
            speedValue.textContent = Math.round(speed * 100) + '%';
        }
        
        speedSlider.addEventListener('input', function() {
            speed = this.value / 100;
            if (speedValue) {
                speedValue.textContent = this.value + '%';
            }
            
            // 如果摇杆处于活动状态，更新控制命令
            if (joystickActive) {
                sendControlCommand(joystickX, joystickY, speed);
            }
        });
    }
    
    // 初始化控制按钮
    initControlButtons();
    
    // 摇杆控制初始化
    function initJoystick() {
        const containerRect = joystickContainer.getBoundingClientRect();
        const containerRadius = containerRect.width / 2;
        const knobRadius = joystickKnob.offsetWidth / 2;
        const maxDistance = containerRadius - knobRadius;
        
        // 鼠标/触摸事件处理
        function handleStart(e) {
            joystickActive = true;
            document.addEventListener('mousemove', handleMove);
            document.addEventListener('touchmove', handleMove, { passive: false });
            document.addEventListener('mouseup', handleEnd);
            document.addEventListener('touchend', handleEnd);
            handleMove(e);
        }
        
        function handleMove(e) {
            e.preventDefault();
            
            const containerRect = joystickContainer.getBoundingClientRect();
            const centerX = containerRect.left + containerRect.width / 2;
            const centerY = containerRect.top + containerRect.height / 2;
            
            // 获取触摸/鼠标位置
            let clientX, clientY;
            if (e.type === 'touchmove' || e.type === 'touchstart') {
                clientX = e.touches[0].clientX;
                clientY = e.touches[0].clientY;
            } else {
                clientX = e.clientX;
                clientY = e.clientY;
            }
            
            // 计算相对于中心的位置
            let deltaX = clientX - centerX;
            let deltaY = clientY - centerY;
            
            // 计算距离和角度
            const distance = Math.min(Math.sqrt(deltaX * deltaX + deltaY * deltaY), maxDistance);
            const angle = Math.atan2(deltaY, deltaX);
            
            // 计算新位置
            const knobX = distance * Math.cos(angle);
            const knobY = distance * Math.sin(angle);
            
            // 更新摇杆位置
            joystickKnob.style.transform = `translate(${knobX}px, ${knobY}px)`;
            
            // 计算控制值 (-100 到 100)
            joystickX = Math.round((knobX / maxDistance) * 100);
            joystickY = Math.round((knobY / maxDistance) * 100);
            
            // 发送控制命令
            sendControlCommand(joystickX, joystickY, speed);
        }
        
        function handleEnd() {
            joystickActive = false;
            joystickKnob.style.transform = 'translate(0, 0)';
            joystickX = 0;
            joystickY = 0;
            
            // 发送停止命令
            sendControlCommand(0, 0, 0);
            
            document.removeEventListener('mousemove', handleMove);
            document.removeEventListener('touchmove', handleMove);
            document.removeEventListener('mouseup', handleEnd);
            document.removeEventListener('touchend', handleEnd);
        }
        
        // 添加事件监听器
        joystickContainer.addEventListener('mousedown', handleStart);
        joystickContainer.addEventListener('touchstart', handleStart, { passive: false });
    }
    
    // 初始化控制按钮
    function initControlButtons() {
        const forwardBtn = document.querySelector('.control-button.forward');
        const backwardBtn = document.querySelector('.control-button.backward');
        const leftBtn = document.querySelector('.control-button.left');
        const rightBtn = document.querySelector('.control-button.right');
        const stopBtn = document.querySelector('.control-button.stop');
        
        if (forwardBtn) {
            forwardBtn.addEventListener('click', function() {
                sendControlCommand(0, -100, speed);
            });
        }
        
        if (backwardBtn) {
            backwardBtn.addEventListener('click', function() {
                sendControlCommand(0, 100, speed);
            });
        }
        
        if (leftBtn) {
            leftBtn.addEventListener('click', function() {
                sendControlCommand(-100, 0, speed);
            });
        }
        
        if (rightBtn) {
            rightBtn.addEventListener('click', function() {
                sendControlCommand(100, 0, speed);
            });
        }
        
        if (stopBtn) {
            stopBtn.addEventListener('click', function() {
                sendControlCommand(0, 0, 0);
            });
        }
    }
    
    // 发送控制命令
    function sendControlCommand(dirX, dirY, speedFactor) {
        if (socket.readyState === WebSocket.OPEN) {
            const command = {
                type: 'joystick',
                dirX: dirX,
                dirY: dirY,
                speed: speedFactor
            };
            
            socket.send(JSON.stringify(command));
        }
    }
    
    // 更新超声波数据显示
    function updateUltrasonicData(data) {
        const frontDistance = document.getElementById('front-distance');
        const rearDistance = document.getElementById('rear-distance');
        const frontObstacle = document.getElementById('front-obstacle');
        const rearObstacle = document.getElementById('rear-obstacle');
        
        if (frontDistance) {
            frontDistance.textContent = data.front_distance + ' cm';
        }
        
        if (rearDistance) {
            rearDistance.textContent = data.rear_distance + ' cm';
        }
        
        if (frontObstacle) {
            if (data.front_obstacle_detected) {
                frontObstacle.classList.add('detected');
                frontObstacle.nextElementSibling.textContent = '检测到障碍物';
            } else {
                frontObstacle.classList.remove('detected');
                frontObstacle.nextElementSibling.textContent = '无障碍物';
            }
        }
        
        if (rearObstacle) {
            if (data.rear_obstacle_detected) {
                rearObstacle.classList.add('detected');
                rearObstacle.nextElementSibling.textContent = '检测到障碍物';
            } else {
                rearObstacle.classList.remove('detected');
                rearObstacle.nextElementSibling.textContent = '无障碍物';
            }
        }
    }
    
    // 更新舵机数据显示
    function updateServoData(data) {
        const steeringAngle = document.getElementById('steering-angle');
        const throttlePosition = document.getElementById('throttle-position');
        
        if (steeringAngle) {
            steeringAngle.textContent = data.steering_angle + '°';
        }
        
        if (throttlePosition) {
            throttlePosition.textContent = data.throttle_position + '°';
        }
    }
    
    // 更新连接状态
    function updateStatus(status) {
        const connectionStatus = document.getElementById('connection-status');
        if (connectionStatus) {
            connectionStatus.textContent = status;
        }
    }
}

/**
 * 高级车辆控制系统类 (从 vehicle-control.js 移植)
 */
class VehicleControlSystem {
    constructor(joystickContainerId, servoControlsId) {
        // DOM Elements
        this.joystickContainer = document.getElementById(joystickContainerId);
        this.servoControls = document.getElementById(servoControlsId);
        
        // Control state
        this.joystickActive = false;
        this.joystickPosition = { x: 0, y: 0 };
        this.servoPositions = {
            pan: 90,  // Center position (0-180 degrees)
            tilt: 90  // Center position (0-180 degrees)
        };
        this.motorSpeeds = {
            left: 0,  // Left motor speed (-100 to 100)
            right: 0  // Right motor speed (-100 to 100)
        };
        
        // Obstacle detection
        this.obstacleDetected = {
            front: false,
            back: false
        };
        this.safetyThresholds = {
            front: 20, // cm
            back: 20   // cm
        };
        
        // Control UI elements
        this.joystickBase = null;
        this.joystickHandle = null;
        this.panSlider = null;
        this.tiltSlider = null;
        this.frontUltrasonicDisplay = null;
        this.backUltrasonicDisplay = null;
        
        // Control loop
        this.controlInterval = null;
        this.controlIntervalMs = 100; // Send control updates every 100ms
        
        // Initialize
        this.init();
    }
    
    /**
     * Initialize the vehicle control system
     */
    init() {
        this.createJoystick();
        this.createServoControls();
        this.createUltrasonicDisplays();
        this.setupEventListeners();
        this.startControlLoop();
        
        // Register for WebSocket messages
        if (window.app) {
            window.app.registerWebSocketHandler('vehicle_status', this.handleStatusMessage.bind(this));
            window.app.registerWebSocketHandler('sensor_data', this.handleSensorData.bind(this));
        }
        
        // Initialize vehicle element in window scope
        window.vehicleSystem = this;
        
        console.log('Vehicle control system initialized');
    }
    
    /**
     * Create joystick UI elements
     */
    createJoystick() {
        if (!this.joystickContainer) return;
        
        // Clear container
        this.joystickContainer.innerHTML = '';
        
        // Create joystick base
        this.joystickBase = document.createElement('div');
        this.joystickBase.className = 'joystick-base';
        this.joystickContainer.appendChild(this.joystickBase);
        
        // Create joystick handle
        this.joystickHandle = document.createElement('div');
        this.joystickHandle.className = 'joystick-handle';
        this.joystickContainer.appendChild(this.joystickHandle);
        
        // Create motor speed displays
        const speedDisplay = document.createElement('div');
        speedDisplay.className = 'motor-speed-display';
        speedDisplay.innerHTML = `
            <div class="speed-labels">
                <div class="speed-label">左电机: <span id="left-motor-speed">0</span>%</div>
                <div class="speed-label">右电机: <span id="right-motor-speed">0</span>%</div>
            </div>
        `;
        this.joystickContainer.appendChild(speedDisplay);
        
        // Position handle in center
        this.updateJoystickHandlePosition(0, 0);
    }
    
    /**
     * Create servo controls UI elements
     */
    createServoControls() {
        if (!this.servoControls) return;
        
        // Clear container
        this.servoControls.innerHTML = '';
        
        // Pan servo slider
        const panContainer = document.createElement('div');
        panContainer.className = 'slider-container';
        panContainer.innerHTML = `
            <label class="slider-label">云台水平 (Pan): <span id="pan-value">${this.servoPositions.pan}°</span></label>
            <input type="range" min="0" max="180" value="${this.servoPositions.pan}" class="slider" id="pan-slider">
        `;
        this.servoControls.appendChild(panContainer);
        
        // Tilt servo slider
        const tiltContainer = document.createElement('div');
        tiltContainer.className = 'slider-container';
        tiltContainer.innerHTML = `
            <label class="slider-label">云台垂直 (Tilt): <span id="tilt-value">${this.servoPositions.tilt}°</span></label>
            <input type="range" min="0" max="180" value="${this.servoPositions.tilt}" class="slider" id="tilt-slider">
        `;
        this.servoControls.appendChild(tiltContainer);
        
        // Get references to sliders
        this.panSlider = document.getElementById('pan-slider');
        this.tiltSlider = document.getElementById('tilt-slider');
    }
    
    /**
     * Create ultrasonic sensor displays
     */
    createUltrasonicDisplays() {
        // Create container for ultrasonic displays if it doesn't exist
        let ultrasonicContainer = document.querySelector('.ultrasonic-container');
        
        if (!ultrasonicContainer) {
            ultrasonicContainer = document.createElement('div');
            ultrasonicContainer.className = 'ultrasonic-container';
            
            // Add before the joystick container
            if (this.joystickContainer && this.joystickContainer.parentNode) {
                this.joystickContainer.parentNode.insertBefore(ultrasonicContainer, this.joystickContainer);
            }
        }
        
        // Clear container
        ultrasonicContainer.innerHTML = '';
        
        // Front ultrasonic display
        const frontDisplay = document.createElement('div');
        frontDisplay.className = 'ultrasonic-card';
        frontDisplay.innerHTML = `
            <div class="ultrasonic-icon">⬆️</div>
            <div class="ultrasonic-label">前方距离</div>
            <div class="ultrasonic-value" id="front-ultrasonic-value">--</div>
            <div class="ultrasonic-unit">cm</div>
        `;
        ultrasonicContainer.appendChild(frontDisplay);
        
        // Back ultrasonic display
        const backDisplay = document.createElement('div');
        backDisplay.className = 'ultrasonic-card';
        backDisplay.innerHTML = `
            <div class="ultrasonic-icon">⬇️</div>
            <div class="ultrasonic-label">后方距离</div>
            <div class="ultrasonic-value" id="back-ultrasonic-value">--</div>
            <div class="ultrasonic-unit">cm</div>
        `;
        ultrasonicContainer.appendChild(backDisplay);
        
        // Get references to displays
        this.frontUltrasonicDisplay = document.getElementById('front-ultrasonic-value');
        this.frontUltrasonicIcon = frontDisplay.querySelector('.ultrasonic-icon');
        this.backUltrasonicDisplay = document.getElementById('back-ultrasonic-value');
        this.backUltrasonicIcon = backDisplay.querySelector('.ultrasonic-icon');
    }
    
    /**
     * Setup event listeners for user interaction
     */
    setupEventListeners() {
        // Joystick event listeners
        if (this.joystickContainer) {
            // Mouse/touch events
            this.joystickContainer.addEventListener('mousedown', this.handleJoystickStart.bind(this));
            document.addEventListener('mousemove', this.handleJoystickMove.bind(this));
            document.addEventListener('mouseup', this.handleJoystickEnd.bind(this));
            
            // Touch events
            this.joystickContainer.addEventListener('touchstart', this.handleJoystickStart.bind(this));
            document.addEventListener('touchmove', this.handleJoystickMove.bind(this));
            document.addEventListener('touchend', this.handleJoystickEnd.bind(this));
        }
        
        // Servo slider event listeners
        if (this.panSlider) {
            this.panSlider.addEventListener('input', this.handlePanChange.bind(this));
        }
        
        if (this.tiltSlider) {
            this.tiltSlider.addEventListener('input', this.handleTiltChange.bind(this));
        }
    }
    
    /**
     * Start the control loop for sending commands
     */
    startControlLoop() {
        // Clear existing interval if any
        if (this.controlInterval) {
            clearInterval(this.controlInterval);
        }
        
        // Start new control loop
        this.controlInterval = setInterval(() => {
            this.sendControlCommands();
        }, this.controlIntervalMs);
    }
    
    /**
     * Handle joystick touch/mouse down
     * @param {Event} e - The event object
     */
    handleJoystickStart(e) {
        e.preventDefault();
        this.joystickActive = true;
        this.updateJoystickFromEvent(e);
    }
    
    /**
     * Handle joystick touch/mouse move
     * @param {Event} e - The event object
     */
    handleJoystickMove(e) {
        if (!this.joystickActive) return;
        e.preventDefault();
        this.updateJoystickFromEvent(e);
    }
    
    /**
     * Handle joystick touch/mouse up
     */
    handleJoystickEnd() {
        this.joystickActive = false;
        this.joystickPosition = { x: 0, y: 0 };
        this.motorSpeeds.left = 0;
        this.motorSpeeds.right = 0;
        this.updateJoystickHandlePosition(0, 0);
        this.updateMotorSpeedDisplay();
    }
    
    /**
     * Update joystick position from event
     * @param {Event} e - The event object
     */
    updateJoystickFromEvent(e) {
        if (!this.joystickBase || !this.joystickHandle) return;
        
        // Get touch or mouse position
        let clientX, clientY;
        if (e.touches && e.touches.length > 0) {
            clientX = e.touches[0].clientX;
            clientY = e.touches[0].clientY;
        } else {
            clientX = e.clientX;
            clientY = e.clientY;
        }
        
        // Get joystick container position and dimensions
        const rect = this.joystickBase.getBoundingClientRect();
        const centerX = rect.left + rect.width / 2;
        const centerY = rect.top + rect.height / 2;
        const radius = rect.width / 2;
        
        // Calculate joystick position relative to center (-1 to 1)
        let x = (clientX - centerX) / radius;
        let y = (clientY - centerY) / radius;
        
        // Limit to unit circle
        const magnitude = Math.sqrt(x * x + y * y);
        if (magnitude > 1) {
            x /= magnitude;
            y /= magnitude;
        }
        
        // Update joystick position
        this.joystickPosition.x = x;
        this.joystickPosition.y = y;
        
        // Update UI
        this.updateJoystickHandlePosition(x, y);
        
        // Calculate motor speeds from joystick position
        this.calculateMotorSpeeds(x, y);
        this.updateMotorSpeedDisplay();
    }
    
    /**
     * Update joystick handle visual position
     * @param {number} x - X position (-1 to 1)
     * @param {number} y - Y position (-1 to 1)
     */
    updateJoystickHandlePosition(x, y) {
        if (!this.joystickBase || !this.joystickHandle) return;
        
        const baseRadius = this.joystickBase.offsetWidth / 2;
        const handleRadius = this.joystickHandle.offsetWidth / 2;
        
        // Calculate pixel position
        const posX = baseRadius + (x * (baseRadius - handleRadius));
        const posY = baseRadius + (y * (baseRadius - handleRadius));
        
        // Update handle position
        this.joystickHandle.style.left = `${posX}px`;
        this.joystickHandle.style.top = `${posY}px`;
    }
    
    /**
     * Calculate motor speeds from joystick position
     * @param {number} x - X position (-1 to 1)
     * @param {number} y - Y position (-1 to 1)
     */
    calculateMotorSpeeds(x, y) {
        // Inverse Y axis (forward is negative Y)
        y = -y;
        
        // Apply obstacle avoidance
        if ((y > 0 && this.obstacleDetected.front) || 
            (y < 0 && this.obstacleDetected.back)) {
            // Stop forward/backward movement
            y = 0;
        }
        
        // Calculate left and right motor speeds using differential drive
        let left = 100 * (y + x);
        let right = 100 * (y - x);
        
        // Normalize if exceeding limits
        const maxValue = Math.max(Math.abs(left), Math.abs(right));
        if (maxValue > 100) {
            left = (left / maxValue) * 100;
            right = (right / maxValue) * 100;
        }
        
        // Round and set
        this.motorSpeeds.left = Math.round(left);
        this.motorSpeeds.right = Math.round(right);
    }
    
    /**
     * Update motor speed display
     */
    updateMotorSpeedDisplay() {
        const leftSpeedElement = document.getElementById('left-motor-speed');
        const rightSpeedElement = document.getElementById('right-motor-speed');
        
        if (leftSpeedElement) {
            leftSpeedElement.textContent = this.motorSpeeds.left;
        }
        
        if (rightSpeedElement) {
            rightSpeedElement.textContent = this.motorSpeeds.right;
        }
    }
    
    /**
     * Handle pan servo slider change
     */
    handlePanChange() {
        if (!this.panSlider) return;
        
        // Update pan position
        this.servoPositions.pan = parseInt(this.panSlider.value);
        
        // Update display
        const panValueElement = document.getElementById('pan-value');
        if (panValueElement) {
            panValueElement.textContent = this.servoPositions.pan + '°';
        }
    }
    
    /**
     * Handle tilt servo slider change
     */
    handleTiltChange() {
        if (!this.tiltSlider) return;
        
        // Update tilt position
        this.servoPositions.tilt = parseInt(this.tiltSlider.value);
        
        // Update display
        const tiltValueElement = document.getElementById('tilt-value');
        if (tiltValueElement) {
            tiltValueElement.textContent = this.servoPositions.tilt + '°';
        }
    }
    
    /**
     * Send control commands to the server
     */
    sendControlCommands() {
        // Only send commands if there's something to send
        if (!window.app || !window.app.sendWebSocketMessage) return;
        
        // Create control message
        const controlMessage = {
            type: 'vehicle_control',
            motors: {
                left: this.motorSpeeds.left,
                right: this.motorSpeeds.right
            },
            servos: {
                pan: this.servoPositions.pan,
                tilt: this.servoPositions.tilt
            }
        };
        
        // Send via WebSocket
        window.app.sendWebSocketMessage(controlMessage);
    }
    
    /**
     * Handle vehicle status messages from WebSocket
     * @param {Object} message - The status message
     */
    handleStatusMessage(message) {
        // Update UI based on status
        if (message.motors) {
            // Update motor speeds if controlled remotely
            if (!this.joystickActive) {
                this.motorSpeeds.left = message.motors.left || 0;
                this.motorSpeeds.right = message.motors.right || 0;
                this.updateMotorSpeedDisplay();
            }
        }
        
        if (message.servos) {
            // Update servo positions if controlled remotely
            this.servoPositions.pan = message.servos.pan || 90;
            this.servoPositions.tilt = message.servos.tilt || 90;
            
            // Update sliders
            if (this.panSlider) {
                this.panSlider.value = this.servoPositions.pan;
                const panValueElement = document.getElementById('pan-value');
                if (panValueElement) {
                    panValueElement.textContent = this.servoPositions.pan + '°';
                }
            }
            
            if (this.tiltSlider) {
                this.tiltSlider.value = this.servoPositions.tilt;
                const tiltValueElement = document.getElementById('tilt-value');
                if (tiltValueElement) {
                    tiltValueElement.textContent = this.servoPositions.tilt + '°';
                }
            }
        }
    }
    
    /**
     * Handle sensor data messages from WebSocket
     * @param {Object} message - The sensor data message
     */
    handleSensorData(message) {
        if (message.ultrasonic) {
            this.updateUltrasonicDisplay(message.ultrasonic);
        }
    }
    
    /**
     * Update ultrasonic sensor displays
     * @param {Object} data - Ultrasonic sensor data
     */
    updateUltrasonicDisplay(data) {
        // Update front ultrasonic
        if (data.front !== undefined && this.frontUltrasonicDisplay) {
            this.frontUltrasonicDisplay.textContent = data.front;
            
            // Check for obstacle
            this.obstacleDetected.front = data.front <= this.safetyThresholds.front;
            
            // Update icon
            if (this.frontUltrasonicIcon) {
                if (this.obstacleDetected.front) {
                    this.frontUltrasonicIcon.classList.add('alert');
                } else {
                    this.frontUltrasonicIcon.classList.remove('alert');
                }
            }
        }
        
        // Update back ultrasonic
        if (data.back !== undefined && this.backUltrasonicDisplay) {
            this.backUltrasonicDisplay.textContent = data.back;
            
            // Check for obstacle
            this.obstacleDetected.back = data.back <= this.safetyThresholds.back;
            
            // Update icon
            if (this.backUltrasonicIcon) {
                if (this.obstacleDetected.back) {
                    this.backUltrasonicIcon.classList.add('alert');
                } else {
                    this.backUltrasonicIcon.classList.remove('alert');
                }
            }
        }
    }
}

/**
 * Initialize vehicle control - 公共接口函数（从 vehicle-control.js 移植）
 * @param {string} joystickContainerId - ID of the joystick container element
 * @param {string} servoControlsId - ID of the servo controls container element
 */
function initVehicleControl(joystickContainerId, servoControlsId) {
    return new VehicleControlSystem(joystickContainerId, servoControlsId);
} 