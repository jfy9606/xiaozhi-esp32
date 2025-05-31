// DOM元素
const connectionStatus = document.getElementById('connection-status');
const statusText = document.getElementById('status-text');
const audioStatus = document.getElementById('audio-status');
const volumeSlider = document.getElementById('volume-slider');
const volumeValue = document.getElementById('volume-value');
const startStreamBtn = document.getElementById('start-stream-btn');
const stopStreamBtn = document.getElementById('stop-stream-btn');
const muteBtn = document.getElementById('mute-btn');
const unmuteBtn = document.getElementById('unmute-btn');
const logContainer = document.getElementById('log-container');
const waveform = document.getElementById('waveform');

// 音频状态变量
let isStreamActive = false;
let isMuted = false;
let previousVolume = 80;
let waveformSimulation = null;

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', initPage);

function initPage() {
    // 设置事件监听器
    setupEventListeners();
    
    // 初始状态为未连接
    updateConnectionStatus(false);
    
    // 连接到WebSocket
    connectWebSocket();
    
    // 页面关闭时断开连接
    window.addEventListener('beforeunload', () => {
        window.xiaozhi.api.disconnectAllWebSockets();
        if (waveformSimulation) {
            clearInterval(waveformSimulation);
        }
    });
}

function setupEventListeners() {
    // 音量滑块事件
    volumeSlider.addEventListener('input', function() {
        const volume = this.value;
        volumeValue.textContent = volume + '%';
        
        // 实时更新音量
        setVolume(volume);
    });
    
    // 开始音频流按钮事件
    startStreamBtn.addEventListener('click', function() {
        startAudioStream();
    });
    
    // 停止音频流按钮事件
    stopStreamBtn.addEventListener('click', function() {
        stopAudioStream();
    });
    
    // 静音按钮事件
    muteBtn.addEventListener('click', function() {
        muteAudio();
    });
    
    // 取消静音按钮事件
    unmuteBtn.addEventListener('click', function() {
        unmuteAudio();
    });
}

function connectWebSocket() {
    try {
        window.xiaozhi.api.connectAudioControl({
            onOpen: () => {
                updateConnectionStatus(true);
                addLogEntry('已连接到音频控制WebSocket', 'success');
            },
            onMessage: (data) => {
                if (data.status === 'ok') {
                    if (data.cmd === 'start_stream') {
                        updateAudioStreamState(true);
                        addLogEntry('音频流已启动', 'success');
                    } else if (data.cmd === 'stop_stream') {
                        updateAudioStreamState(false);
                        addLogEntry('音频流已停止', 'success');
                    } else if (data.cmd === 'volume') {
                        addLogEntry(`音量已设置为 ${data.data.volume}%`, 'success');
                    }
                } else if (data.status === 'error') {
                    addLogEntry(`错误: ${data.message}`, 'error');
                }
            },
            onClose: () => {
                updateConnectionStatus(false);
                addLogEntry('与音频控制WebSocket断开连接', 'error');
                updateAudioStreamState(false);
            },
            onError: (error) => {
                addLogEntry(`WebSocket错误: ${error}`, 'error');
            }
        });
    } catch (error) {
        addLogEntry(`连接WebSocket失败: ${error.message}`, 'error');
    }
}

function startAudioStream() {
    try {
        window.xiaozhi.api.startAudioStream();
        addLogEntry('正在启动音频流...', 'info');
    } catch (error) {
        addLogEntry(`启动音频流失败: ${error.message}`, 'error');
    }
}

function stopAudioStream() {
    try {
        window.xiaozhi.api.stopAudioStream();
        addLogEntry('正在停止音频流...', 'info');
    } catch (error) {
        addLogEntry(`停止音频流失败: ${error.message}`, 'error');
    }
}

function setVolume(volume) {
    try {
        window.xiaozhi.api.setAudioVolume(parseInt(volume));
        
        if (!isMuted) {
            previousVolume = volume;
        }
    } catch (error) {
        addLogEntry(`设置音量失败: ${error.message}`, 'error');
    }
}

function muteAudio() {
    try {
        previousVolume = volumeSlider.value;
        volumeSlider.value = 0;
        volumeValue.textContent = '0%';
        
        window.xiaozhi.api.setAudioVolume(0);
        
        isMuted = true;
        muteBtn.disabled = true;
        unmuteBtn.disabled = false;
        
        addLogEntry('音频已静音', 'info');
    } catch (error) {
        addLogEntry(`静音失败: ${error.message}`, 'error');
    }
}

function unmuteAudio() {
    try {
        volumeSlider.value = previousVolume;
        volumeValue.textContent = previousVolume + '%';
        
        window.xiaozhi.api.setAudioVolume(parseInt(previousVolume));
        
        isMuted = false;
        muteBtn.disabled = false;
        unmuteBtn.disabled = true;
        
        addLogEntry('已取消静音', 'info');
    } catch (error) {
        addLogEntry(`取消静音失败: ${error.message}`, 'error');
    }
}

function updateConnectionStatus(connected) {
    connectionStatus.className = `status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`;
    statusText.textContent = connected ? '已连接' : '未连接';
    
    // 启用/禁用控制元素
    const buttons = document.querySelectorAll('button');
    if (!connected) {
        buttons.forEach(button => {
            button.disabled = true;
        });
        volumeSlider.disabled = true;
        updateAudioStreamState(false);
    } else {
        startStreamBtn.disabled = false;
        stopStreamBtn.disabled = true;
        muteBtn.disabled = false;
        unmuteBtn.disabled = true;
        volumeSlider.disabled = false;
    }
}

function updateAudioStreamState(active) {
    isStreamActive = active;
    startStreamBtn.disabled = active;
    stopStreamBtn.disabled = !active;
    
    // 更新音频状态显示
    audioStatus.textContent = active ? '运行中' : '未启动';
    
    // 更新波形显示
    if (active) {
        waveform.innerHTML = '';
        startWaveformSimulation();
    } else {
        if (waveformSimulation) {
            clearInterval(waveformSimulation);
            waveformSimulation = null;
        }
        waveform.innerHTML = '<p style="text-align: center; padding-top: 80px; color: #666;">音频流未启动</p>';
    }
}

function startWaveformSimulation() {
    // 创建波形模拟
    const canvas = document.createElement('canvas');
    canvas.width = waveform.offsetWidth;
    canvas.height = waveform.offsetHeight;
    waveform.appendChild(canvas);
    
    const ctx = canvas.getContext('2d');
    let offset = 0;
    
    waveformSimulation = setInterval(() => {
        ctx.fillStyle = '#f0f0f0';
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        
        // 绘制波形
        const centerY = canvas.height / 2;
        const amplitude = canvas.height / 4;
        
        ctx.beginPath();
        ctx.moveTo(0, centerY);
        
        for (let x = 0; x < canvas.width; x++) {
            // 创建复合波
            const y = centerY + 
                Math.sin((x + offset) * 0.05) * amplitude * 0.5 +
                Math.sin((x + offset) * 0.01) * amplitude * 0.3 +
                (Math.random() - 0.5) * amplitude * 0.1;
            
            ctx.lineTo(x, y);
        }
        
        ctx.lineTo(canvas.width, centerY);
        ctx.strokeStyle = '#0066cc';
        ctx.lineWidth = 2;
        ctx.stroke();
        
        offset += 5;
    }, 50);
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