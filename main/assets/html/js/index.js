/**
 * index.js - 小智ESP32首页脚本
 */

// 全局变量
let statusPollingInterval; // 状态轮询间隔ID
let otaFile = null; // 保存固件文件

// 初始化页面
document.addEventListener('DOMContentLoaded', function() {
    // 获取系统状态
    getSystemStatus();
    
    // 启动状态轮询
    startStatusPolling();
    
    // 设置事件监听器
    setupEventListeners();
    
    // 设置OTA相关事件
    setupOtaEvents();
});

// 启动状态轮询
function startStatusPolling() {
    // 清除现有轮询
    clearInterval(statusPollingInterval);
    
    // 每10秒轮询一次状态
    statusPollingInterval = setInterval(function() {
        getSystemStatus();
    }, 10000);
}

// 获取系统状态
function getSystemStatus() {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 8000); // 8秒超时

    fetch('/api/status', { signal: controller.signal })
        .then(response => {
            clearTimeout(timeoutId);
            if (response.ok) {
                return response.json();
            }
            throw new Error(`网络响应错误: ${response.status} ${response.statusText}`);
        })
        .then(data => {
            console.log('主页收到状态数据:', JSON.stringify(data, null, 2));
            
            const uptimeEl = document.getElementById('uptime');
            const freeHeapEl = document.getElementById('free-heap');
            const versionEl = document.getElementById('version');
            
            // 统一处理数据源
            const systemData = (data.type === 'status_response' && data.system) ? data.system : data;

            if (uptimeEl) {
                if (systemData.uptime !== undefined) {
                    const uptimeInSeconds = parseInt(systemData.uptime);
                    const hours = Math.floor(uptimeInSeconds / 3600);
                    const minutes = Math.floor((uptimeInSeconds % 3600) / 60);
                    const seconds = uptimeInSeconds % 60;
                    uptimeEl.textContent = 
                    `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
                } else {
                    uptimeEl.textContent = '未知';
                }
            }
                
            if (freeHeapEl) {
                if (systemData.free_heap !== undefined) {
                    freeHeapEl.textContent = `${Math.round(parseInt(systemData.free_heap) / 1024)} KB`;
                } else {
                    freeHeapEl.textContent = '未知';
                }
            }
                
            if (versionEl) {
                if (systemData.esp_idf_version) {
                    versionEl.textContent = systemData.esp_idf_version;
                } else if (systemData.version) {
                    versionEl.textContent = systemData.version;
                } else if (systemData.chip) {
                    versionEl.textContent = systemData.chip;
                } else {
                    versionEl.textContent = '未知';
                }
            }
        })
        .catch(error => {
            clearTimeout(timeoutId);
            console.error('主页获取状态失败:', error.message);
            const uptimeEl = document.getElementById('uptime');
            if (uptimeEl) uptimeEl.textContent = '加载失败';
            const freeHeapEl = document.getElementById('free-heap');
            if (freeHeapEl) freeHeapEl.textContent = '加载失败';
            const versionEl = document.getElementById('version');
            if (versionEl) versionEl.textContent = '加载失败';
        });
}

// 设置事件监听器
function setupEventListeners() {
    // 设置按钮点击事件
    const settingsButton = document.getElementById('settings-button');
    const settingsModal = document.getElementById('settings-modal');
    const closeButton = document.querySelector('.close');
    
    // 确保找到设置按钮
    if (settingsButton) {
        settingsButton.addEventListener('click', function(e) {
            e.preventDefault();
            settingsModal.style.display = 'block';
            // 加载AI设置
            loadAISettings();
        });
    } else {
        console.error('设置按钮未找到');
    }
    
    closeButton.addEventListener('click', function() {
        settingsModal.style.display = 'none';
    });
    
    // 点击模态框外部关闭
    window.addEventListener('click', function(event) {
        if (event.target == settingsModal) {
            settingsModal.style.display = 'none';
        }
    });
    
    // AI设置表单提交
    const aiForm = document.getElementById('ai-form');
    aiForm.addEventListener('submit', function(event) {
        event.preventDefault();
        saveAISettings();
    });
}

// 打开设置选项卡
function openTab(evt, tabName) {
    // 隐藏所有标签内容
    const tabcontent = document.getElementsByClassName('tabcontent');
    for (let i = 0; i < tabcontent.length; i++) {
        tabcontent[i].style.display = 'none';
    }
    
    // 移除所有标签按钮的active类
    const tablinks = document.getElementsByClassName('tablinks');
    for (let i = 0; i < tablinks.length; i++) {
        tablinks[i].className = tablinks[i].className.replace(' active', '');
    }
    
    // 显示当前标签内容并添加active类
    document.getElementById(tabName).style.display = 'block';
    evt.currentTarget.className += ' active';
}

// 保存AI设置
function saveAISettings() {
    const formData = new FormData(document.getElementById('ai-form'));
    const data = {
        api_key: formData.get('api_key'),
        api_model: formData.get('api_model'),
        api_url: formData.get('api_url')
    };
    
    fetch('/api/ai/settings', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(data => {
        // 显示成功消息
        const message = document.getElementById('ai-message');
        message.textContent = '保存成功';
        message.className = 'message success';
        
        // 3秒后隐藏消息
        setTimeout(() => {
            message.className = 'message';
        }, 3000);
    })
    .catch(error => {
        // 显示错误消息
        const message = document.getElementById('ai-message');
        message.textContent = '保存失败: ' + error.message;
        message.className = 'message error';
    });
}

// 加载AI设置
function loadAISettings() {
    fetch('/api/ai/settings')
    .then(response => response.json())
    .then(data => {
        // 设置表单值
        document.getElementById('api_key').value = data.api_key || '';
        document.getElementById('api_model').value = data.api_model || 'gpt-3.5-turbo';
        document.getElementById('api_url').value = data.api_url || 'https://api.openai.com/v1/chat/completions';
    })
    .catch(error => {
        console.error('加载AI设置失败:', error);
    });
}

// 设置OTA相关事件
function setupOtaEvents() {
    const otaButton = document.createElement('button');
    otaButton.className = 'settings-button';
    otaButton.id = 'ota-button';
    otaButton.style.marginLeft = '10px';
    otaButton.innerHTML = '<i class="fas fa-upload"></i> OTA升级';
    
    // 添加按钮到页面
    document.querySelector('.footer').insertBefore(otaButton, document.querySelector('.footer p'));
    
    // OTA按钮点击事件
    otaButton.addEventListener('click', function() {
        document.getElementById('ota-modal').style.display = 'block';
    });
    
    // 关闭按钮点击事件
    document.querySelector('.ota-close').addEventListener('click', function() {
        document.getElementById('ota-modal').style.display = 'none';
    });
    
    // 点击模态框外部关闭
    window.addEventListener('click', function(event) {
        if (event.target == document.getElementById('ota-modal')) {
            document.getElementById('ota-modal').style.display = 'none';
        }
    });
    
    // 上传按钮点击事件
    document.getElementById('ota-upload-button').addEventListener('click', function() {
        document.getElementById('firmware-file').click();
    });
    
    // 文件选择变化事件
    document.getElementById('firmware-file').addEventListener('change', function(e) {
        handleFileSelect(e.target.files);
    });
    
    // 上传区域拖放事件
    const uploadArea = document.getElementById('upload-area');
    
    uploadArea.addEventListener('dragover', function(e) {
        e.preventDefault();
        e.stopPropagation();
        this.style.borderColor = '#6200ea';
    });
    
    uploadArea.addEventListener('dragleave', function(e) {
        e.preventDefault();
        e.stopPropagation();
        this.style.borderColor = '#ccc';
    });
    
    uploadArea.addEventListener('drop', function(e) {
        e.preventDefault();
        e.stopPropagation();
        this.style.borderColor = '#ccc';
        
        if (e.dataTransfer.files.length) {
            handleFileSelect(e.dataTransfer.files);
        }
    });
    
    uploadArea.addEventListener('click', function() {
        document.getElementById('firmware-file').click();
    });
    
    // 刷写按钮点击事件
    document.getElementById('ota-flash-button').addEventListener('click', function() {
        if (otaFile) {
            uploadFirmware(otaFile);
        }
    });
}

// 处理文件选择
function handleFileSelect(files) {
    if (files.length === 0) return;
    
    const file = files[0];
    
    // 检查文件类型
    if (!file.name.endsWith('.bin')) {
        alert('请选择.bin格式的固件文件');
        return;
    }
    
    // 保存文件引用
    otaFile = file;
    
    // 显示文件信息
    const fileInfo = document.getElementById('file-info');
    fileInfo.textContent = `文件: ${file.name} (${(file.size / 1024).toFixed(2)} KB)`;
    
    // 启用刷写按钮
    document.getElementById('ota-flash-button').disabled = false;
}

// 上传固件
function uploadFirmware(file) {
    // 显示进度条
    const progressContainer = document.getElementById('progress-container');
    const progressBar = document.getElementById('progress-bar');
    const statusText = document.getElementById('ota-status');
    
    progressContainer.style.display = 'block';
    progressBar.style.width = '0%';
    statusText.textContent = '准备上传...';
    
    // 创建表单数据
    const formData = new FormData();
    formData.append('firmware', file);
    
    // 发送请求
    const xhr = new XMLHttpRequest();
    
    xhr.upload.addEventListener('progress', function(event) {
        if (event.lengthComputable) {
            const percent = (event.loaded / event.total) * 100;
            progressBar.style.width = percent + '%';
            statusText.textContent = `上传中: ${percent.toFixed(1)}%`;
        }
    });
    
    xhr.addEventListener('load', function() {
        if (xhr.status === 200) {
            statusText.textContent = '固件上传成功，设备正在升级...';
            setTimeout(function() {
                statusText.textContent = '升级完成，设备将自动重启';
            }, 5000);
        } else {
            statusText.textContent = '上传失败: ' + xhr.statusText;
        }
    });
    
    xhr.addEventListener('error', function() {
        statusText.textContent = '上传失败: 网络错误';
    });
    
    xhr.open('POST', '/update');
    xhr.send(formData);
} 