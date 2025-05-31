// move.js - 小智ESP32小车控制脚本

document.addEventListener('DOMContentLoaded', function() {
    const baseHost = document.location.origin;
    const streamUrl = `${baseHost}/camera/stream`;
    const stillUrl = `${baseHost}/camera/capture`;
    const controlUrl = `${baseHost}/camera/control`;
    const statusUrl = `${baseHost}/camera/status`;

    const stream = document.getElementById('video-stream');
    const startStreamBtn = document.getElementById('start-stream');
    const stopStreamBtn = document.getElementById('stop-stream');

    // 视频流控制
    let streamActive = false;
    let userWantsStream = false;

    // WebSocket连接变量
    let wsConnection = null;
    let reconnectAttempt = 0;
    const maxReconnectAttempts = 15; // 增加到15次重连尝试
    let reconnectInterval = 1000;
    let reconnecting = false;
    let lastPongTime = 0;
    let heartbeatInterval = null;
    let connectionTimeout = null;
    
    // 控制参数
    let joystickActive = false;
    let joystickX = 0;
    let joystickY = 0;

    // 创建WebSocket连接
    function connectWebSocket() {
        if (wsConnection && (wsConnection.readyState === WebSocket.CONNECTING ||
                            wsConnection.readyState === WebSocket.OPEN)) {
            console.log('WebSocket已连接或正在连接中');
            return;
        }

        // 清除之前的定时器
        if (heartbeatInterval) {
            clearInterval(heartbeatInterval);
            heartbeatInterval = null;
        }

        if (connectionTimeout) {
            clearTimeout(connectionTimeout);
            connectionTimeout = null;
        }

        console.log('正在创建WebSocket连接...');

        try {
            // 确保使用正确的WebSocket URL
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            
            // 使用当前主机的地址
            const wsUrl = `${protocol}//${window.location.host}/ws`;
            console.log(`连接到WebSocket: ${wsUrl}`);

            wsConnection = new WebSocket(wsUrl);

            // 保存到window对象，允许其他组件访问
            window.wsConnection = wsConnection;

            wsConnection.onopen = function() {
                console.log('WebSocket连接已建立');
                reconnectAttempt = 0;
                reconnectInterval = 1000;
                reconnecting = false;

                // 记录连接时间
                lastPongTime = Date.now();

                // 发送欢迎消息
                try {
                    const helloMsg = JSON.stringify({
                        type: "hello", 
                        client: "web_controller", 
                        version: "1.0"
                    });
                    wsConnection.send(helloMsg);
                    console.log('发送hello消息');
                } catch (e) {
                    console.error('发送欢迎消息失败:', e);
                }

                // 设置定期心跳
                heartbeatInterval = setInterval(function() {
                    if (wsConnection && wsConnection.readyState === WebSocket.OPEN) {
                        try {
                            // 发送正确格式的ping消息
                            const pingMsg = JSON.stringify({type: "ping"});
                            wsConnection.send(pingMsg);

                            // 检查是否收到了pong响应
                            const currentTime = Date.now();
                            if (currentTime - lastPongTime > 10000) { // 10秒没有收到pong
                                console.warn('WebSocket心跳超时，尝试重连');
                                wsConnection.close();
                                connectWebSocket();
                            }
                        } catch (e) {
                            console.error('发送心跳失败:', e);
                            wsConnection.close();
                        }
                    }
                }, 5000); // 每5秒发送一次心跳
                
                // 增加活跃度保持机制 - 每15秒发送一个活跃信号
                setInterval(function() {
                    if (wsConnection && wsConnection.readyState === WebSocket.OPEN) {
                        try {
                            // 发送活跃保持消息
                            const keepAliveMsg = JSON.stringify({
                                type: "keep_alive", 
                                client: "web_controller", 
                                timestamp: Date.now()
                            });
                            wsConnection.send(keepAliveMsg);
                            console.log('发送活跃保持消息');
                        } catch (e) {
                            console.error('发送活跃保持消息失败:', e);
                        }
                    }
                }, 15000); // 每15秒发送一次活跃信号，比心跳更长周期

                // 更新UI状态
                document.getElementById('connection-status').classList.add('connected');
                document.getElementById('connection-text').innerText = '已连接';
            };

            wsConnection.onmessage = function(e) {
                // 处理来自服务器的消息
                const data = e.data;
                
                try {
                    // 尝试解析JSON消息
                    const jsonData = JSON.parse(data);
                    
                    // 检查消息类型
                    if (jsonData.type === "pong") {
                        // 收到心跳响应，更新时间戳
                        lastPongTime = Date.now();
                    } else if (jsonData.type === "hello_response") {
                        // 收到欢迎消息
                        console.log('收到欢迎消息:', jsonData.message);
                        if (jsonData.status === "ok") {
                            // 连接成功，更新UI
                            document.getElementById('connection-status').classList.add('connected');
                            document.getElementById('connection-text').innerText = '已连接';
                        }
                    } else if (jsonData.type === "car_control_ack" || jsonData.type === "joystick_ack") {
                        // 收到控制命令确认
                        console.log('控制命令已确认:', jsonData.status);
                        
                        // 处理错误响应
                        if (jsonData.status === "error" && jsonData.message) {
                            // 显示障碍物警告
                            showWarningAlert(jsonData.message);
                        }
                    } else if (jsonData.type === "camera_status") {
                        // 处理摄像头状态消息
                        console.log('收到摄像头状态:', jsonData);
                        // 如果收到摄像头状态消息，可以更新UI
                        if(jsonData.streaming === true) {
                            console.log('摄像头正在流式传输');
                            // 可以添加代码更新UI显示摄像头状态
                        } else if(jsonData.streaming === false) {
                            console.log('摄像头停止流式传输');
                            // 可以添加代码更新UI显示摄像头状态
                        }
                    } else if (jsonData.type === "ultrasonic_data") {
                        // 处理超声波数据
                        console.log('收到超声波数据:', jsonData);
                        updateUltrasonicDisplay(jsonData);
                        // 更新活跃时间
                        lastPongTime = Date.now();
                    } else if (jsonData.type === "servo_data") {
                        // 处理舵机状态数据
                        console.log('收到舵机数据:', jsonData);
                        updateServoStatus(jsonData);
                        // 更新活跃时间
                        lastPongTime = Date.now();
                    } else if (jsonData.type === "status") {
                        // 更新连接状态指示
                        if (jsonData.status === 'connected' || jsonData.status === 'ap_only') {
                            document.getElementById('connection-status').classList.add('connected');
                            document.getElementById('connection-text').innerText = jsonData.status === 'connected' 
                                ? `已连接 (${jsonData.ip || ''}, RSSI: ${jsonData.rssi || ''}dBm)` 
                                : `AP模式 (${jsonData.ap_ip || ''})`;
                        }
                    } else if (jsonData.type === "status_response") {
                        // 处理系统状态数据，更新超声波显示
                        if (jsonData.sensors && jsonData.sensors.ultrasonic) {
                            updateUltrasonicDisplay(jsonData.sensors.ultrasonic);
                        }
                        // 作为活跃响应处理
                        lastPongTime = Date.now();
                        // 更新系统状态信息（可选）
                        console.log('收到系统状态数据，已处理');
                    } else if (jsonData.type === "car_servo_ack") {
                        // 处理舵机命令确认
                        console.log('舵机命令已确认:', jsonData);
                        if (jsonData.status === "error" && jsonData.message) {
                            // 显示舵机错误
                            showServoError(jsonData.message || "舵机控制失败");
                        }
                    } else if (jsonData.type === "keep_alive_ack") {
                        // 处理保活消息确认
                        console.log('保活消息已确认');
                        lastPongTime = Date.now();
                    } else {
                        console.log('收到未知类型消息:', jsonData);
                    }
                } catch (err) {
                    // 如果不是JSON消息，简单处理
                    if (data === 'pong') {
                        lastPongTime = Date.now();
                    } else {
                        console.log('收到非JSON消息:', data);
                    }
                }
            };

            wsConnection.onclose = function(event) {
                console.log('WebSocket连接已关闭', event.code, event.reason);

                // 清除心跳
                if (heartbeatInterval) {
                    clearInterval(heartbeatInterval);
                    heartbeatInterval = null;
                }

                // 更新UI状态
                document.getElementById('connection-status').classList.remove('connected');
                document.getElementById('connection-text').innerText = '已断开';

                // 尝试重连
                if (!reconnecting && reconnectAttempt < maxReconnectAttempts) {
                    reconnecting = true;
                    // 使用指数退避策略，但限制最大延迟为30秒
                    const delay = Math.min(30000, reconnectInterval * Math.pow(1.5, reconnectAttempt));
                    console.log(`尝试在 ${delay}ms 后重新连接... (尝试 ${reconnectAttempt + 1}/${maxReconnectAttempts})`);

                    setTimeout(function() {
                        reconnectAttempt++;
                        connectWebSocket();
                    }, delay);

                    reconnectInterval = delay;
                } else if (reconnectAttempt >= maxReconnectAttempts) {
                    console.error(`已达到最大重连次数 (${maxReconnectAttempts})，停止重连`);
                    document.getElementById('connection-text').innerText = '连接失败';
                }
            };

            wsConnection.onerror = function(err) {
                console.error('WebSocket发生错误:', err);
                // 错误发生后不需要额外关闭，因为onerror之后通常会自动触发onclose
            };

            // 设置连接超时
            connectionTimeout = setTimeout(function() {
                if (wsConnection && wsConnection.readyState !== WebSocket.OPEN) {
                    console.warn('WebSocket连接超时');
                    wsConnection.close();
                }
            }, 10000);

        } catch (e) {
            console.error('创建WebSocket连接失败:', e);

            // 更新UI状态
            document.getElementById('connection-status').classList.remove('connected');
            document.getElementById('connection-text').innerText = '连接错误';

            // 尝试重连
            if (!reconnecting && reconnectAttempt < maxReconnectAttempts) {
                reconnecting = true;
                setTimeout(function() {
                    reconnectAttempt++;
                    connectWebSocket();
                }, reconnectInterval);
            }
        }
    }

    // 视频流控制函数
    function startStream() {
        if (streamActive) return;
        
        console.log('正在启动视频流...');
        stream.src = streamUrl;
        
        stream.onload = function() {
            streamActive = true;
            userWantsStream = true;
            startStreamBtn.disabled = true;
            stopStreamBtn.disabled = false;
            console.log('视频流已启动');
        };
        
        stream.onerror = function(e) {
            console.error('视频流启动失败:', e);
            streamActive = false;
            stream.src = '';
            startStreamBtn.disabled = false;
            stopStreamBtn.disabled = true;
        };
    }

    function stopStream() {
        if (!streamActive) return;
        
        console.log('正在停止视频流...');
        stream.src = '';
        streamActive = false;
        userWantsStream = false;
        startStreamBtn.disabled = false;
        stopStreamBtn.disabled = true;
        console.log('视频流已停止');
    }

    // 为流媒体按钮添加事件监听器
    if (startStreamBtn) {
        startStreamBtn.addEventListener('click', startStream);
    }
    
    if (stopStreamBtn) {
        stopStreamBtn.addEventListener('click', stopStream);
    }

    // 初始化摇杆控制
    const joystick = document.getElementById('joystick');
    const joystickButton = document.getElementById('joystick-button');
    const directionIndicator = document.getElementById('direction-indicator');
    const joystickStatus = document.getElementById('joystick-status');
    let joystickInterval;

    function showWarningAlert(message) {
        const alertElement = document.getElementById('warning-alert');
        if (!alertElement) {
            const newAlert = document.createElement('div');
            newAlert.id = 'warning-alert';
            newAlert.className = 'warning-alert';
            newAlert.innerText = message;
            document.body.appendChild(newAlert);
            
            // 添加到DOM后显示
            setTimeout(() => {
                newAlert.classList.add('show');
                
                // 3秒后隐藏
                setTimeout(() => {
                    newAlert.classList.remove('show');
                    
                    // 动画结束后移除
                    setTimeout(() => {
                        if (newAlert.parentNode) {
                            newAlert.parentNode.removeChild(newAlert);
                        }
                    }, 500);
                }, 3000);
            }, 10);
        } else {
            alertElement.innerText = message;
            alertElement.classList.add('show');
            
            // 3秒后隐藏
            setTimeout(() => {
                alertElement.classList.remove('show');
            }, 3000);
        }
    }

    function showServoError(message) {
        const errorElement = document.getElementById('servo-error');
        if (errorElement) {
            errorElement.innerText = message;
            errorElement.classList.add('show');
            
            // 3秒后隐藏
            setTimeout(() => {
                errorElement.classList.remove('show');
            }, 3000);
        }
    }

    function initJoystick() {
        if (!joystick || !joystickButton) return;

        const joystickRect = joystick.getBoundingClientRect();
        const joystickCenterX = joystickRect.width / 2;
        const joystickCenterY = joystickRect.height / 2;
        const maxDistance = joystickRect.width / 2 * 0.7; // 限制摇杆移动的最大距离
        
        // 初始化摇杆位置
        joystickButton.style.left = '33.5%';
        joystickButton.style.top = '33.5%';
        
        // 追踪触点ID以处理多点触控
        let activePointerId = null;

        // 触摸事件处理函数
        function handleTouchStart(e) {
            e.preventDefault();
            if (activePointerId !== null) return; // 已经有一个激活的触点
            
            const touch = e.changedTouches[0];
            activePointerId = touch.identifier;
            joystickActive = true;
            
            // 注册临时监听器
            document.addEventListener('touchmove', handleTouchMove, { passive: false });
            document.addEventListener('touchend', handleTouchEnd);
            document.addEventListener('touchcancel', handleTouchEnd);
            
            // 计算初始位置
            updateJoystickPosition(touch.clientX, touch.clientY);
            startSendingCommands();
        }
        
        function handleTouchMove(e) {
            e.preventDefault();
            
            // 找到我们正在跟踪的触点
            const touchList = e.changedTouches;
            for (let i = 0; i < touchList.length; i++) {
                if (touchList[i].identifier === activePointerId) {
                    updateJoystickPosition(touchList[i].clientX, touchList[i].clientY);
                    break;
                }
            }
        }
        
        function handleTouchEnd(e) {
            const touchList = e.changedTouches;
            for (let i = 0; i < touchList.length; i++) {
                if (touchList[i].identifier === activePointerId) {
                    resetJoystick();
                    activePointerId = null;
                    
                    // 移除临时监听器
                    document.removeEventListener('touchmove', handleTouchMove);
                    document.removeEventListener('touchend', handleTouchEnd);
                    document.removeEventListener('touchcancel', handleTouchEnd);
                    break;
                }
            }
        }
        
        // 鼠标事件处理函数
        function handleMouseDown(e) {
            e.preventDefault();
            if (joystickActive) return; // 避免与触摸事件冲突
            
            joystickActive = true;
            
            // 注册临时监听器
            document.addEventListener('mousemove', handleMouseMove);
            document.addEventListener('mouseup', handleMouseUp);
            
            // 计算初始位置
            updateJoystickPosition(e.clientX, e.clientY);
            startSendingCommands();
        }
        
        function handleMouseMove(e) {
            if (!joystickActive) return;
            e.preventDefault();
            updateJoystickPosition(e.clientX, e.clientY);
        }
        
        function handleMouseUp(e) {
            if (!joystickActive) return;
            e.preventDefault();
            resetJoystick();
            
            // 移除临时监听器
            document.removeEventListener('mousemove', handleMouseMove);
            document.removeEventListener('mouseup', handleMouseUp);
        }
        
        // 更新摇杆位置和计算控制值
        function updateJoystickPosition(clientX, clientY) {
            const joystickRect = joystick.getBoundingClientRect();
            
            // 计算相对于摇杆中心的位置
            let deltaX = clientX - (joystickRect.left + joystickCenterX);
            let deltaY = clientY - (joystickRect.top + joystickCenterY);
            
            // 计算距离和角度
            const distance = Math.min(Math.sqrt(deltaX * deltaX + deltaY * deltaY), maxDistance);
            const angle = Math.atan2(deltaY, deltaX);
            
            // 根据距离和角度计算新位置
            const newX = distance * Math.cos(angle);
            const newY = distance * Math.sin(angle);
            
            // 计算百分比位置（中心是0,0）
            const percentX = newX / maxDistance;
            const percentY = newY / maxDistance;
            
            // 更新全局变量用于命令发送
            joystickX = percentX;
            joystickY = -percentY; // Y轴要反转：向上是负值，但我们想要向前为正
            
            // 更新摇杆按钮位置
            const buttonX = joystickCenterX + newX;
            const buttonY = joystickCenterY + newY;
            
            // 将位置转换为百分比
            const buttonPercentX = (buttonX / joystickRect.width) * 100;
            const buttonPercentY = (buttonY / joystickRect.height) * 100;
            
            joystickButton.style.left = buttonPercentX + '%';
            joystickButton.style.top = buttonPercentY + '%';
            
            // 更新方向指示器
            if (directionIndicator) {
                directionIndicator.style.left = (joystickCenterX + newX * 1.5) + 'px';
                directionIndicator.style.top = (joystickCenterY + newY * 1.5) + 'px';
                directionIndicator.style.opacity = '1';
            }
            
            // 更新UI状态显示
            updateJoystickStatus();
        }
        
        // 重置摇杆位置
        function resetJoystick() {
            joystickActive = false;
            joystickX = 0;
            joystickY = 0;
            
            joystickButton.style.left = '33.5%';
            joystickButton.style.top = '33.5%';
            
            if (directionIndicator) {
                directionIndicator.style.opacity = '0';
            }
            
            stopSendingCommands();
            sendStopCommand();
            updateJoystickStatus();
        }
        
        // 更新摇杆状态显示
        function updateJoystickStatus() {
            if (joystickStatus) {
                if (joystickActive) {
                    let speedText = Math.abs(joystickY) > 0.1 ? Math.round(Math.abs(joystickY) * 100) + '%' : '0%';
                    let directionText = '';
                    
                    // 确定主要移动方向
                    if (Math.abs(joystickY) > Math.abs(joystickX) * 1.5) {
                        // 主要是前进或后退
                        directionText = joystickY > 0.1 ? '前进' : (joystickY < -0.1 ? '后退' : '停止');
                    } else if (Math.abs(joystickX) > Math.abs(joystickY) * 1.5) {
                        // 主要是左转或右转
                        directionText = joystickX < -0.1 ? '左转' : (joystickX > 0.1 ? '右转' : '停止');
                    } else if (Math.abs(joystickX) > 0.1 && Math.abs(joystickY) > 0.1) {
                        // 斜向移动
                        let xDir = joystickX < 0 ? '左' : '右';
                        let yDir = joystickY > 0 ? '前' : '后';
                        directionText = yDir + xDir;
                    } else {
                        directionText = '停止';
                    }
                    
                    joystickStatus.textContent = `方向: ${directionText}, 速度: ${speedText}`;
                } else {
                    joystickStatus.textContent = '摇杆未激活';
                }
            }
        }
        
        // 添加事件监听
        joystick.addEventListener('mousedown', handleMouseDown);
        joystick.addEventListener('touchstart', handleTouchStart, { passive: false });
        
        // 阻止默认的移动行为以防止页面滚动
        joystick.addEventListener('touchmove', function(e) {
            e.preventDefault();
        }, { passive: false });
    }
    
    // 定义命令发送函数
    function startSendingCommands() {
        // 清除可能存在的旧计时器
        if (joystickInterval) {
            clearInterval(joystickInterval);
        }
        
        // 创建新的命令发送计时器
        joystickInterval = setInterval(function() {
            if (joystickActive && wsConnection && wsConnection.readyState === WebSocket.OPEN) {
                // 计算摇杆控制数据
                const speed = Math.round(joystickY * 100); // 前进后退速度，-100到100
                const turn = Math.round(joystickX * 100);  // 左右转向，-100到100
                
                // 创建控制消息
                const controlMsg = {
                    type: 'joystick_control',
                    speed: speed,
                    turn: turn,
                    timestamp: Date.now()
                };
                
                try {
                    wsConnection.send(JSON.stringify(controlMsg));
                    console.log('发送控制命令:', speed, turn);
                } catch (e) {
                    console.error('发送控制命令失败:', e);
                }
            }
        }, 100); // 每100毫秒发送一次，频率不要太高
    }
    
    function stopSendingCommands() {
        if (joystickInterval) {
            clearInterval(joystickInterval);
            joystickInterval = null;
        }
    }
    
    function sendStopCommand() {
        if (wsConnection && wsConnection.readyState === WebSocket.OPEN) {
            // 发送停止命令
            const stopMsg = {
                type: 'joystick_control',
                speed: 0,
                turn: 0,
                timestamp: Date.now()
            };
            
            try {
                wsConnection.send(JSON.stringify(stopMsg));
                console.log('发送停止命令');
            } catch (e) {
                console.error('发送停止命令失败:', e);
            }
        }
    }

    // 超声波数据显示
    function updateUltrasonicDisplay(data) {
        // 检查前方超声波元素
        const frontUltrasonicValue = document.getElementById('front-ultrasonic-value');
        if (frontUltrasonicValue && data.front !== undefined) {
            const frontValue = parseFloat(data.front);
            frontUltrasonicValue.textContent = frontValue.toFixed(1) + ' cm';
            
            // 更新警告状态
            const frontIcon = document.getElementById('front-ultrasonic-icon');
            if (frontIcon) {
                if (frontValue < 20) {
                    frontIcon.classList.add('alert');
                } else {
                    frontIcon.classList.remove('alert');
                }
            }
        }
        
        // 检查后方超声波元素
        const backUltrasonicValue = document.getElementById('back-ultrasonic-value');
        if (backUltrasonicValue && data.back !== undefined) {
            const backValue = parseFloat(data.back);
            backUltrasonicValue.textContent = backValue.toFixed(1) + ' cm';
            
            // 更新警告状态
            const backIcon = document.getElementById('back-ultrasonic-icon');
            if (backIcon) {
                if (backValue < 20) {
                    backIcon.classList.add('alert');
                } else {
                    backIcon.classList.remove('alert');
                }
            }
        }
        
        // 如果接近障碍物，显示警告
        if ((data.front !== undefined && parseFloat(data.front) < 15) || 
            (data.back !== undefined && parseFloat(data.back) < 15)) {
            showWarningAlert('警告：接近障碍物！');
        }
    }

    // 舵机控制
    function initServoControls() {
        const panServoSlider = document.getElementById('pan-servo');
        const tiltServoSlider = document.getElementById('tilt-servo');
        const panValueElement = document.getElementById('pan-value');
        const tiltValueElement = document.getElementById('tilt-value');
        
        // 防抖动函数，限制发送命令的频率
        function debounce(func, wait) {
            let timeout;
            return function(...args) {
                const context = this;
                clearTimeout(timeout);
                timeout = setTimeout(() => func.apply(context, args), wait);
            };
        }
        
        // 发送舵机控制命令
        const sendServoCommand = debounce(function(type, value) {
            if (wsConnection && wsConnection.readyState === WebSocket.OPEN) {
                const servoMsg = {
                    type: 'car_servo_control',
                    servo: type,
                    value: value,
                    timestamp: Date.now()
                };
                
                try {
                    wsConnection.send(JSON.stringify(servoMsg));
                    console.log(`发送${type}舵机控制命令:`, value);
                } catch (e) {
                    console.error('发送舵机命令失败:', e);
                    showServoError('舵机控制命令发送失败');
                }
            } else {
                console.error('WebSocket未连接，无法发送舵机命令');
                showServoError('网络连接已断开，无法控制舵机');
            }
        }, 100); // 100毫秒防抖
        
        // 更新舵机状态
        function updateServoStatus(data) {
            if (data.servo === 'pan' && panServoSlider && panValueElement) {
                panServoSlider.value = data.value;
                panValueElement.textContent = data.value;
            } else if (data.servo === 'tilt' && tiltServoSlider && tiltValueElement) {
                tiltServoSlider.value = data.value;
                tiltValueElement.textContent = data.value;
            }
        }
        
        // 为舵机滑块添加事件处理
        if (panServoSlider && panValueElement) {
            panServoSlider.addEventListener('input', function() {
                panValueElement.textContent = this.value;
            });
            
            panServoSlider.addEventListener('change', function() {
                sendServoCommand('pan', parseInt(this.value));
            });
        }
        
        if (tiltServoSlider && tiltValueElement) {
            tiltServoSlider.addEventListener('input', function() {
                tiltValueElement.textContent = this.value;
            });
            
            tiltServoSlider.addEventListener('change', function() {
                sendServoCommand('tilt', parseInt(this.value));
            });
        }
        
        // 如果页面上存在舵机回中按钮，添加事件监听器
        const centerServosBtn = document.getElementById('center-servos');
        if (centerServosBtn) {
            centerServosBtn.addEventListener('click', function() {
                // 舵机回到中间位置
                if (panServoSlider) {
                    panServoSlider.value = 90;
                    panValueElement.textContent = '90';
                    sendServoCommand('pan', 90);
                }
                
                if (tiltServoSlider) {
                    tiltServoSlider.value = 90;
                    tiltValueElement.textContent = '90';
                    sendServoCommand('tilt', 90);
                }
            });
        }
        
        // 添加快捷按钮的事件处理（如果存在）
        // 上下左右按钮
        const upBtn = document.getElementById('servo-up');
        const downBtn = document.getElementById('servo-down');
        const leftBtn = document.getElementById('servo-left');
        const rightBtn = document.getElementById('servo-right');
        
        if (upBtn && tiltServoSlider) {
            upBtn.addEventListener('click', function() {
                let currentValue = parseInt(tiltServoSlider.value);
                let newValue = Math.max(0, Math.min(180, currentValue + 10));
                tiltServoSlider.value = newValue;
                tiltValueElement.textContent = newValue;
                sendServoCommand('tilt', newValue);
            });
        }
        
        if (downBtn && tiltServoSlider) {
            downBtn.addEventListener('click', function() {
                let currentValue = parseInt(tiltServoSlider.value);
                let newValue = Math.max(0, Math.min(180, currentValue - 10));
                tiltServoSlider.value = newValue;
                tiltValueElement.textContent = newValue;
                sendServoCommand('tilt', newValue);
            });
        }
        
        if (leftBtn && panServoSlider) {
            leftBtn.addEventListener('click', function() {
                let currentValue = parseInt(panServoSlider.value);
                let newValue = Math.max(0, Math.min(180, currentValue + 10));
                panServoSlider.value = newValue;
                panValueElement.textContent = newValue;
                sendServoCommand('pan', newValue);
            });
        }
        
        if (rightBtn && panServoSlider) {
            rightBtn.addEventListener('click', function() {
                let currentValue = parseInt(panServoSlider.value);
                let newValue = Math.max(0, Math.min(180, currentValue - 10));
                panServoSlider.value = newValue;
                panValueElement.textContent = newValue;
                sendServoCommand('pan', newValue);
            });
        }
    }
    
    // 页面加载完成后初始化舵机控制
    initServoControls();
    
    // 请求系统状态信息（包括舵机位置和超声波数据）
    function requestSystemStatus() {
        if (wsConnection && wsConnection.readyState === WebSocket.OPEN) {
            const statusRequest = {
                type: 'get_status',
                timestamp: Date.now()
            };
            
            try {
                wsConnection.send(JSON.stringify(statusRequest));
                console.log('请求系统状态');
            } catch (e) {
                console.error('请求系统状态失败:', e);
            }
        }
    }
    
    // 设置定期请求状态
    setInterval(requestSystemStatus, 5000); // 每5秒请求一次
    
    // 初始请求一次状态
    setTimeout(requestSystemStatus, 1000); // 连接建立后1秒请求一次

    // 页面可见性变化处理
    document.addEventListener('visibilitychange', function() {
        if (document.visibilityState === 'visible') {
            // 页面变为可见时，尝试重连WebSocket并重新请求状态
            if (!wsConnection || wsConnection.readyState !== WebSocket.OPEN) {
                connectWebSocket();
            } else {
                // 页面重新可见时，请求最新状态
                requestSystemStatus();
            }
            
            // 如果用户之前开启了视频流，尝试恢复
            if (userWantsStream && !streamActive) {
                startStream();
            }
        } else {
            // 页面不可见时可以选择关闭视频流以节省资源
            if (streamActive) {
                stopStream();
            }
        }
    });
    
    // 页面卸载时发送停止命令
    window.addEventListener('beforeunload', function() {
        sendStopCommand();
        if (wsConnection && wsConnection.readyState === WebSocket.OPEN) {
            wsConnection.close();
        }
    });

    // 连接WebSocket并初始化摇杆
    connectWebSocket();
    initJoystick();
}); 