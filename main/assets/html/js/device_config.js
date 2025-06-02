/**
 * device_config.js
 */

console.log('设备配置脚本已加载');

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', initPage);

function initPage() {
    // 设置事件监听器
    setupEventListeners();
    
    // 加载配置数据
    loadDeviceConfig();
    
    // 加载系统信息
    loadSystemInfo();
    
    // 显示状态为已连接
    updateConnectionStatus(true);
}

function setupEventListeners() {
    // 网络设置相关
    document.querySelectorAll('input[name="wifi-mode"]').forEach(radio => {
        radio.addEventListener('change', function() {
            toggleWifiSettings(this.value);
        });
    });
    
    document.querySelectorAll('input[name="ip-config"]').forEach(radio => {
        radio.addEventListener('change', function() {
            toggleIpSettings(this.value);
        });
    });
    
    document.getElementById('save-network-btn').addEventListener('click', saveNetworkSettings);
    
    // 音频设置相关
    const volumeSlider = document.getElementById('audio-volume');
    const volumeDisplay = document.getElementById('volume-display');
    
    volumeSlider.addEventListener('input', function() {
        volumeDisplay.textContent = this.value + '%';
    });
    
    document.getElementById('save-audio-btn').addEventListener('click', saveAudioSettings);
    
    // 舵机设置相关
    document.getElementById('save-servo-btn').addEventListener('click', saveServoSettings);
    
    // 系统相关
    document.getElementById('restart-btn').addEventListener('click', restartSystem);
    
    // 标签切换
    document.querySelectorAll('.tab-button').forEach((button, index) => {
        button.addEventListener('click', function() {
            const tabIds = ['network-tab', 'audio-tab', 'servo-tab', 'system-tab'];
            showTab(tabIds[index]);
        });
    });
}

function showTab(tabId) {
    // 隐藏所有标签内容
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });
    
    // 取消选中所有标签按钮
    document.querySelectorAll('.tab-button').forEach(button => {
        button.classList.remove('active');
    });
    
    // 显示选定的标签内容
    document.getElementById(tabId).classList.add('active');
    
    // 选中对应的标签按钮
    const buttonIndex = ['network-tab', 'audio-tab', 'servo-tab', 'system-tab'].indexOf(tabId);
    if (buttonIndex >= 0) {
        document.querySelectorAll('.tab-button')[buttonIndex].classList.add('active');
    }
}

function toggleWifiSettings(mode) {
    const apSettings = document.getElementById('ap-settings');
    const staSettings = document.getElementById('sta-settings');
    
    if (mode === 'AP') {
        apSettings.classList.remove('hidden');
        staSettings.classList.add('hidden');
    } else {
        apSettings.classList.add('hidden');
        staSettings.classList.remove('hidden');
    }
}

function toggleIpSettings(config) {
    const staticIpSettings = document.getElementById('static-ip-settings');
    
    if (config === 'static') {
        staticIpSettings.classList.remove('hidden');
    } else {
        staticIpSettings.classList.add('hidden');
    }
}

function updateConnectionStatus(connected) {
    const connectionStatus = document.getElementById('connection-status');
    const statusText = document.getElementById('status-text');
    
    connectionStatus.className = `status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`;
    statusText.textContent = connected ? '已连接' : '未连接';
}

function showMessage(elementId, message, type) {
    const messageElement = document.getElementById(elementId);
    messageElement.textContent = message;
    messageElement.className = `message ${type}`;
    
    // 3秒后隐藏消息
    setTimeout(() => {
        messageElement.className = 'message hidden';
    }, 3000);
}

async function loadDeviceConfig() {
    try {
        const response = await window.xiaozhi.api.getDeviceConfig();
        
        if (response.success) {
            const config = response.data;
            console.log('加载的配置:', config);
            
            // 填充网络设置
            if (config.network) {
                // 设置WiFi模式
                const wifiMode = config.network.wifi_mode || 'AP';
                document.querySelector(`input[name="wifi-mode"][value="${wifiMode}"]`).checked = true;
                toggleWifiSettings(wifiMode);
                
                // 设置AP设置
                if (config.network.ap_ssid) {
                    document.getElementById('ap-ssid').value = config.network.ap_ssid;
                }
                
                // 设置STA设置
                if (config.network.sta_ssid) {
                    document.getElementById('sta-ssid').value = config.network.sta_ssid;
                }
                
                // 设置IP配置
                const ipConfig = config.network.dhcp_enabled ? 'dhcp' : 'static';
                document.querySelector(`input[name="ip-config"][value="${ipConfig}"]`).checked = true;
                toggleIpSettings(ipConfig);
                
                // 设置静态IP
                if (config.network.static_ip) {
                    document.getElementById('static-ip').value = config.network.static_ip;
                    document.getElementById('static-mask').value = config.network.static_mask;
                    document.getElementById('static-gateway').value = config.network.static_gateway;
                    document.getElementById('static-dns').value = config.network.static_dns;
                }
            }
            
            // 填充音频设置
            if (config.audio) {
                if (config.audio.volume !== undefined) {
                    document.getElementById('audio-volume').value = config.audio.volume;
                    document.getElementById('volume-display').textContent = `${config.audio.volume}%`;
                }
                
                if (config.audio.sample_rate) {
                    document.getElementById('audio-sample-rate').value = config.audio.sample_rate;
                }
            }
            
            // 填充舵机设置
            if (config.servo && config.servo.default_frequency !== undefined) {
                document.getElementById('servo-default-freq').value = config.servo.default_frequency;
            }
        }
    } catch (error) {
        console.error('加载配置失败:', error);
        updateConnectionStatus(false);
        showMessage('network-message', '加载配置失败: ' + error.message, 'error');
    }
}

async function loadSystemInfo() {
    try {
        const response = await window.xiaozhi.api.getSystemInfo();
        
        if (response.success) {
            const info = response.data;
            console.log('加载的系统信息:', info);
            
            // 填充系统信息
            document.getElementById('device-name').textContent = info.app_name || '小智ESP32';
            document.getElementById('firmware-version').textContent = info.app_version || info.version || '1.0.0';
            
            // 处理运行时间
            if (info.uptime_ms !== undefined) {
                const uptimeSeconds = Math.floor(info.uptime_ms / 1000);
                const hours = Math.floor(uptimeSeconds / 3600);
                const minutes = Math.floor((uptimeSeconds % 3600) / 60);
                const seconds = uptimeSeconds % 60;
                document.getElementById('uptime').textContent = 
                    `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
            }
            
            // 处理内存信息
            if (info.free_heap !== undefined) {
                document.getElementById('free-heap').textContent = `${Math.round(info.free_heap / 1024)} KB`;
            }
            
            if (info.min_free_heap !== undefined) {
                document.getElementById('min-free-heap').textContent = `${Math.round(info.min_free_heap / 1024)} KB`;
            }
        }
    } catch (error) {
        console.error('加载系统信息失败:', error);
        showMessage('system-message', '加载系统信息失败: ' + error.message, 'error');
    }
}

async function saveNetworkSettings() {
    const wifiMode = document.querySelector('input[name="wifi-mode"]:checked').value;
    const ipConfig = document.querySelector('input[name="ip-config"]:checked').value;
    
    // 构建网络配置对象
    const networkConfig = {
        wifi_mode: wifiMode
    };
    
    // AP模式设置
    if (wifiMode === 'AP') {
        networkConfig.ap_ssid = document.getElementById('ap-ssid').value;
        networkConfig.ap_password = document.getElementById('ap-password').value;
    } 
    // STA模式设置
    else {
        networkConfig.sta_ssid = document.getElementById('sta-ssid').value;
        networkConfig.sta_password = document.getElementById('sta-password').value;
        networkConfig.dhcp_enabled = (ipConfig === 'dhcp');
        
        // 如果是静态IP
        if (ipConfig === 'static') {
            networkConfig.static_ip = document.getElementById('static-ip').value;
            networkConfig.static_mask = document.getElementById('static-mask').value;
            networkConfig.static_gateway = document.getElementById('static-gateway').value;
            networkConfig.static_dns = document.getElementById('static-dns').value;
        }
    }
    
    try {
        const response = await window.xiaozhi.api.updateDeviceConfig({
            network: networkConfig
        });
        
        if (response.success) {
            showMessage('network-message', '网络设置已保存', 'success');
        } else {
            showMessage('network-message', '保存网络设置失败: ' + response.message, 'error');
        }
    } catch (error) {
        console.error('保存网络设置失败:', error);
        showMessage('network-message', '保存网络设置失败: ' + error.message, 'error');
    }
}

async function saveAudioSettings() {
    const volume = parseInt(document.getElementById('audio-volume').value);
    const sampleRate = parseInt(document.getElementById('audio-sample-rate').value);
    
    try {
        const response = await window.xiaozhi.api.updateDeviceConfig({
            audio: {
                volume: volume,
                sample_rate: sampleRate
            }
        });
        
        if (response.success) {
            showMessage('audio-message', '音频设置已保存', 'success');
        } else {
            showMessage('audio-message', '保存音频设置失败: ' + response.message, 'error');
        }
    } catch (error) {
        console.error('保存音频设置失败:', error);
        showMessage('audio-message', '保存音频设置失败: ' + error.message, 'error');
    }
}

async function saveServoSettings() {
    const defaultFrequency = parseInt(document.getElementById('servo-default-freq').value);
    
    try {
        const response = await window.xiaozhi.api.updateDeviceConfig({
            servo: {
                default_frequency: defaultFrequency
            }
        });
        
        if (response.success) {
            showMessage('servo-message', '舵机设置已保存', 'success');
            
            // 如果保存成功，立即应用新的频率
            try {
                await window.xiaozhi.api.setServoFrequency(defaultFrequency);
            } catch (error) {
                console.error('设置舵机频率失败:', error);
            }
        } else {
            showMessage('servo-message', '保存舵机设置失败: ' + response.message, 'error');
        }
    } catch (error) {
        console.error('保存舵机设置失败:', error);
        showMessage('servo-message', '保存舵机设置失败: ' + error.message, 'error');
    }
}

async function restartSystem() {
    if (!confirm('确定要重启设备吗？')) {
        return;
    }
    
    try {
        const response = await window.xiaozhi.api.restartSystem();
        
        if (response.success) {
            showMessage('system-message', '设备正在重启...', 'success');
            
            // 禁用重启按钮
            document.getElementById('restart-btn').disabled = true;
            
            // 显示倒计时
            let countdown = 15;
            const messageElement = document.getElementById('system-message');
            
            const countdownInterval = setInterval(() => {
                countdown--;
                messageElement.textContent = `设备正在重启...${countdown}秒后自动刷新页面`;
                
                if (countdown <= 0) {
                    clearInterval(countdownInterval);
                    window.location.reload();
                }
            }, 1000);
        } else {
            showMessage('system-message', '重启设备失败: ' + response.message, 'error');
        }
    } catch (error) {
        console.error('重启设备失败:', error);
        showMessage('system-message', '重启设备失败: ' + error.message, 'error');
    }
}
