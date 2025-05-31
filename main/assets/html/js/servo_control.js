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

// 生成舵机控制界面
function generateServoControls() {
    servoControlsContainer.innerHTML = '';
    
    for (let i = 0; i < CHANNEL_COUNT; i++) {
        const servoControl = document.createElement('div');
        servoControl.className = 'servo-control';
        
        const title = document.createElement('h3');
        title.textContent = `通道 ${i}`;
        
        const sliderContainer = document.createElement('div');
        sliderContainer.className = 'slider-container';
        
        const angleDisplay = document.createElement('div');
        angleDisplay.className = 'angle-display';
        angleDisplay.textContent = `${servoAngles[i]}°`;
        angleDisplay.id = `angle-display-${i}`;
        
        const slider = document.createElement('input');
        slider.type = 'range';
        slider.min = '0';
        slider.max = '180';
        slider.value = servoAngles[i];
        slider.className = 'range-slider';
        slider.id = `servo-slider-${i}`;
        
        slider.addEventListener('input', (event) => {
            const angle = parseInt(event.target.value);
            servoAngles[i] = angle;
            angleDisplay.textContent = `${angle}°`;
        });
        
        slider.addEventListener('change', (event) => {
            const angle = parseInt(event.target.value);
            setServoAngle(i, angle);
        });
        
        const buttonContainer = document.createElement('div');
        buttonContainer.style.display = 'flex';
        buttonContainer.style.justifyContent = 'space-between';
        buttonContainer.style.marginTop = '10px';
        
        const minBtn = document.createElement('button');
        minBtn.textContent = '0°';
        minBtn.onclick = () => {
            slider.value = 0;
            servoAngles[i] = 0;
            angleDisplay.textContent = '0°';
            setServoAngle(i, 0);
        };
        
        const centerBtn = document.createElement('button');
        centerBtn.textContent = '90°';
        centerBtn.onclick = () => {
            slider.value = 90;
            servoAngles[i] = 90;
            angleDisplay.textContent = '90°';
            setServoAngle(i, 90);
        };
        
        const maxBtn = document.createElement('button');
        maxBtn.textContent = '180°';
        maxBtn.onclick = () => {
            slider.value = 180;
            servoAngles[i] = 180;
            angleDisplay.textContent = '180°';
            setServoAngle(i, 180);
        };
        
        buttonContainer.appendChild(minBtn);
        buttonContainer.appendChild(centerBtn);
        buttonContainer.appendChild(maxBtn);
        
        sliderContainer.appendChild(angleDisplay);
        sliderContainer.appendChild(slider);
        sliderContainer.appendChild(buttonContainer);
        
        servoControl.appendChild(title);
        servoControl.appendChild(sliderContainer);
        
        servoControlsContainer.appendChild(servoControl);
    }
}

// 更新连接状态UI
function updateConnectionStatus(connected) {
    connectionStatus.className = `status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`;
    statusText.textContent = connected ? '已连接' : '未连接';
    
    // 启用/禁用控制元素
    const sliders = document.querySelectorAll('.range-slider');
    const buttons = document.querySelectorAll('button');
    
    sliders.forEach(slider => {
        slider.disabled = !connected;
    });
    
    buttons.forEach(button => {
        button.disabled = !connected;
    });
    
    frequencyInput.disabled = !connected;
}

// 添加日志条目
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

// 设置舵机角度
function setServoAngle(channel, angle) {
    try {
        window.xiaozhi.api.setServoAngleWs(channel, angle);
        addLogEntry(`设置通道 ${channel} 角度为 ${angle}°`, 'info');
    } catch (error) {
        addLogEntry(`设置舵机角度失败: ${error.message}`, 'error');
    }
}

// 设置PWM频率
function setFrequency(frequency) {
    try {
        window.xiaozhi.api.setServoFrequencyWs(frequency);
        addLogEntry(`设置PWM频率为 ${frequency} Hz`, 'info');
    } catch (error) {
        addLogEntry(`设置频率失败: ${error.message}`, 'error');
    }
}

// 初始化页面
function initPage() {
    // 生成舵机控制界面
    generateServoControls();
    
    // 初始状态为未连接
    updateConnectionStatus(false);
    
    // 添加频率设置按钮事件
    setFrequencyBtn.addEventListener('click', () => {
        const frequency = parseInt(frequencyInput.value);
        if (frequency < 50 || frequency > 300) {
            addLogEntry('频率必须在50-300 Hz范围内', 'error');
            return;
        }
        
        setFrequency(frequency);
    });
    
    // 连接到WebSocket
    try {
        window.xiaozhi.api.connectServoControl({
            onOpen: () => {
                updateConnectionStatus(true);
                addLogEntry('已连接到舵机控制WebSocket', 'success');
                
                // 获取舵机状态
                window.xiaozhi.api.getServoStatus()
                    .then(response => {
                        if (response.success) {
                            addLogEntry('获取舵机状态成功', 'success');
                            
                            if (response.data.max_frequency_hz) {
                                frequencyInput.max = response.data.max_frequency_hz;
                                addLogEntry(`舵机控制器支持的最大频率: ${response.data.max_frequency_hz} Hz`, 'info');
                            }
                        }
                    })
                    .catch(error => {
                        addLogEntry(`获取舵机状态失败: ${error.message}`, 'error');
                    });
            },
            onMessage: (data) => {
                if (data.status === 'ok') {
                    if (data.cmd === 'set_angle') {
                        addLogEntry(`通道 ${data.channel} 角度已设置为 ${data.angle}°`, 'success');
                    } else if (data.cmd === 'set_frequency') {
                        frequencyDisplay.textContent = `${data.frequency} Hz`;
                        addLogEntry(`PWM频率已设置为 ${data.frequency} Hz`, 'success');
                    }
                } else if (data.status === 'error') {
                    addLogEntry(`错误: ${data.message}`, 'error');
                }
            },
            onClose: () => {
                updateConnectionStatus(false);
                addLogEntry('与舵机控制WebSocket断开连接', 'error');
            },
            onError: (error) => {
                addLogEntry(`WebSocket错误: ${error.message}`, 'error');
            }
        });
    } catch (error) {
        addLogEntry(`连接WebSocket失败: ${error.message}`, 'error');
    }
    
    // 页面关闭时断开连接
    window.addEventListener('beforeunload', () => {
        window.xiaozhi.api.disconnectAllWebSockets();
    });
}

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', initPage); 