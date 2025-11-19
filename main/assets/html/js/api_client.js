
class XiaozhiAPI {
    /**
     * 构造函数
     * @param {string} baseUrl - API基础URL，默认为当前主机
     */
    constructor(baseUrl) {
        this.baseUrl = baseUrl || window.location.origin;
        this.httpBaseUrl = `${this.baseUrl}/api`;
        this.wsBaseUrl = `${this.baseUrl.replace(/^http/, 'ws')}/ws`;
        this.wsConnections = {};
        this.wsCallbacks = {};

        // 消息批处理相关
        this.messageQueues = {};
        this.batchInterval = 100; // 批处理间隔(毫秒)
        this.batchTimers = {};
        this.maxQueueSize = 10; // 单个批次最大消息数

        // 重连相关设置
        this.reconnectIntervals = {}; // 存储每个端点的当前重连间隔
        this.maxReconnectInterval = 30000; // 最大重连间隔(30秒)
        this.baseReconnectInterval = 1000; // 初始重连间隔(1秒)

        // 安全相关
        this.authToken = localStorage.getItem('xiaozhi_auth_token') || null;
    }

    //==================
    // HTTP API 方法
    //==================

    /**
     * 通用HTTP请求方法
     * @param {string} method - HTTP方法 (GET, POST, PUT, DELETE)
     * @param {string} endpoint - API端点路径
     * @param {Object} data - 请求数据（对于POST和PUT请求）
     * @returns {Promise<Object>} - 响应数据
     */
    async request(method, endpoint, data = null) {
        const url = `${this.httpBaseUrl}${endpoint}`;
        const options = {
            method,
            headers: {
                'Content-Type': 'application/json',
                'Accept': 'application/json'
            }
        };

        // 添加认证令牌（如果存在）
        if (this.authToken) {
            options.headers['Authorization'] = `Bearer ${this.authToken}`;
        }

        if (data && (method === 'POST' || method === 'PUT')) {
            options.body = JSON.stringify(data);
        }

        try {
            console.debug(`API Request: ${method} ${url}`, data || '');
            const startTime = performance.now();
            const response = await fetch(url, options);
            const endTime = performance.now();

            if (!response.ok) {
                const errorText = await response.text();
                console.error(`API Error: ${response.status} ${response.statusText}`, errorText);
                throw new Error(`API Error: ${response.status} ${response.statusText} - ${errorText}`);
            }

            const responseData = await response.json();

            // 记录响应时间
            console.debug(`API Response (${Math.round(endTime - startTime)}ms):`, responseData);

            // 统一响应格式检查
            if (!responseData.hasOwnProperty('status')) {
                console.warn('API Response missing status field:', responseData);
            }

            return responseData;
        } catch (error) {
            console.error(`API Request failed: ${error.message}`);
            throw error;
        }
    }

    /**
     * 获取系统信息
     * @returns {Promise<Object>} - 系统信息
     */
    async getSystemInfo() {
        return this.request('GET', '/system/info');
    }

    /**
     * 重启系统
     * @returns {Promise<Object>} - 响应结果
     */
    async restartSystem() {
        return this.request('POST', '/system/restart');
    }

    /**
     * 获取舵机状态
     * @returns {Promise<Object>} - 舵机状态信息
     */
    async getServoStatus() {
        return this.request('GET', '/servo/status');
    }

    /**
     * 设置舵机角度
     * @param {number} channel - 舵机通道 (0-15)
     * @param {number} angle - 角度 (0-180)
     * @returns {Promise<Object>} - 响应结果
     */
    async setServoAngle(channel, angle) {
        return this.request('POST', '/servo/angle', { channel, angle });
    }

    /**
     * 设置舵机PWM频率
     * @param {number} frequency - 频率 (50-300Hz)
     * @returns {Promise<Object>} - 响应结果
     */
    async setServoFrequency(frequency) {
        return this.request('POST', '/servo/frequency', { frequency });
    }

    /**
     * 获取设备配置
     * @returns {Promise<Object>} - 设备配置信息
     */
    async getDeviceConfig() {
        return this.request('GET', '/device/config');
    }

    /**
     * 更新设备配置
     * @param {Object} config - 设备配置对象
     * @returns {Promise<Object>} - 响应结果
     */
    async updateDeviceConfig(config) {
        return this.request('POST', '/device/config', config);
    }

    /**
     * 用户登录
     * @param {string} username - 用户名
     * @param {string} password - 密码
     * @returns {Promise<Object>} - 登录结果，包含token
     */
    async login(username, password) {
        const response = await this.request('POST', '/auth/login', { username, password });
        if (response.status === 'success' && response.token) {
            this.authToken = response.token;
            localStorage.setItem('xiaozhi_auth_token', response.token);
        }
        return response;
    }

    /**
     * 用户登出
     * @returns {Promise<Object>} - 登出结果
     */
    async logout() {
        const response = await this.request('POST', '/auth/logout');
        this.authToken = null;
        localStorage.removeItem('xiaozhi_auth_token');
        return response;
    }

    //==================
    // WebSocket API 方法
    //==================

    /**
     * 连接到WebSocket
     * @param {string} endpoint - WebSocket端点 ('/servo', '/sensor', etc.)
     * @param {Object} callbacks - 回调函数对象
     * @returns {WebSocket} - WebSocket连接
     */
    connectWebSocket(endpoint, callbacks = {}) {
        if (this.wsConnections[endpoint]) {
            console.warn(`Already connected to WebSocket ${endpoint}`);
            return this.wsConnections[endpoint];
        }

        const url = `${this.wsBaseUrl}${endpoint}`;
        console.log(`Connecting to WebSocket: ${url}`);

        // 初始化该端点的消息队列
        if (!this.messageQueues[endpoint]) {
            this.messageQueues[endpoint] = [];
        }

        let wsUrl = url;
        // 添加认证信息(如果有)
        if (this.authToken) {
            wsUrl += (url.includes('?') ? '&' : '?') + `token=${this.authToken}`;
        }

        const ws = new WebSocket(wsUrl);
        this.wsConnections[endpoint] = ws;
        this.wsCallbacks[endpoint] = callbacks;

        // 初始化重连间隔
        if (!this.reconnectIntervals[endpoint]) {
            this.reconnectIntervals[endpoint] = this.baseReconnectInterval;
        }

        ws.onopen = () => {
            console.log(`Connected to WebSocket ${endpoint}`);
            // 重置重连间隔
            this.reconnectIntervals[endpoint] = this.baseReconnectInterval;

            // 如果有排队的消息，立即处理
            this.processBatchQueue(endpoint);

            if (callbacks.onOpen) callbacks.onOpen();
        };

        ws.onmessage = (event) => {
            try {
                const startTime = performance.now();
                const data = JSON.parse(event.data);
                const endTime = performance.now();

                console.debug(`Received from ${endpoint} (${Math.round(endTime - startTime)}ms):`, data);

                // 检查是否有统一的响应格式
                if (data && typeof data === 'object') {
                    if (!data.hasOwnProperty('type') && !data.hasOwnProperty('status')) {
                        console.warn(`WebSocket response from ${endpoint} missing standard fields:`, data);
                    }
                }

                if (callbacks.onMessage) callbacks.onMessage(data);
            } catch (error) {
                console.error(`Error parsing WebSocket message from ${endpoint}: ${error.message}`, event.data);
                if (callbacks.onError) callbacks.onError(new Error(`Parse error: ${error.message}`));
            }
        };

        ws.onclose = (event) => {
            // 详细记录关闭原因
            let closeReason = "Unknown reason";

            // WebSocket关闭代码: https://developer.mozilla.org/en-US/docs/Web/API/CloseEvent/code
            switch (event.code) {
                case 1000: closeReason = "Normal closure"; break;
                case 1001: closeReason = "Going away"; break;
                case 1002: closeReason = "Protocol error"; break;
                case 1003: closeReason = "Unsupported data"; break;
                case 1005: closeReason = "No status received"; break;
                case 1006: closeReason = "Abnormal closure"; break;
                case 1007: closeReason = "Invalid frame payload data"; break;
                case 1008: closeReason = "Policy violation"; break;
                case 1009: closeReason = "Message too big"; break;
                case 1010: closeReason = "Mandatory extension"; break;
                case 1011: closeReason = "Internal server error"; break;
                case 1012: closeReason = "Service restart"; break;
                case 1013: closeReason = "Try again later"; break;
                case 1014: closeReason = "Bad gateway"; break;
                case 1015: closeReason = "TLS handshake"; break;
            }

            console.log(`Disconnected from WebSocket ${endpoint}: code=${event.code} (${closeReason}), reason=${event.reason || "No reason provided"}`);

            delete this.wsConnections[endpoint];
            if (callbacks.onClose) callbacks.onClose(event);

            // 如果不是正常关闭且配置了重连，则尝试重连
            if (event.code !== 1000 && callbacks.autoReconnect !== false) {
                // 使用指数退避算法
                const reconnectInterval = this.reconnectIntervals[endpoint];
                console.log(`Attempting to reconnect to ${endpoint} in ${reconnectInterval / 1000} seconds...`);

                setTimeout(() => {
                    this.connectWebSocket(endpoint, callbacks);
                    // 增加重连间隔，但不超过最大值
                    this.reconnectIntervals[endpoint] = Math.min(
                        reconnectInterval * 1.5,
                        this.maxReconnectInterval
                    );
                }, reconnectInterval);
            }
        };

        ws.onerror = (error) => {
            console.error(`WebSocket ${endpoint} error:`, error);

            // 提取更详细的错误信息
            let errorDetails = "Unknown error";
            if (error && error.message) {
                errorDetails = error.message;
            } else if (error && error.target && error.target.readyState === WebSocket.CLOSED) {
                errorDetails = "Connection closed";
            } else if (error && error.target && error.target.readyState === WebSocket.CONNECTING) {
                errorDetails = "Connection failed";
            }

            console.error(`WebSocket ${endpoint} detailed error: ${errorDetails}`);

            if (callbacks.onError) callbacks.onError(error, errorDetails);
        };

        return ws;
    }

    /**
     * 添加消息到批处理队列
     * @param {string} endpoint - WebSocket端点
     * @param {Object} data - 要发送的数据
     */
    addToBatchQueue(endpoint, data) {
        if (!this.messageQueues[endpoint]) {
            this.messageQueues[endpoint] = [];
        }

        this.messageQueues[endpoint].push(data);

        // 如果队列达到最大大小，立即处理
        if (this.messageQueues[endpoint].length >= this.maxQueueSize) {
            this.processBatchQueue(endpoint);
            return;
        }

        // 否则，启动或重置定时器
        if (this.batchTimers[endpoint]) {
            clearTimeout(this.batchTimers[endpoint]);
        }

        this.batchTimers[endpoint] = setTimeout(() => {
            this.processBatchQueue(endpoint);
        }, this.batchInterval);
    }

    /**
     * 处理批处理队列
     * @param {string} endpoint - WebSocket端点
     */
    processBatchQueue(endpoint) {
        // 清除定时器
        if (this.batchTimers[endpoint]) {
            clearTimeout(this.batchTimers[endpoint]);
            delete this.batchTimers[endpoint];
        }

        // 检查队列是否为空
        if (!this.messageQueues[endpoint] || this.messageQueues[endpoint].length === 0) {
            return;
        }

        const ws = this.wsConnections[endpoint];
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            console.warn(`Cannot process batch for ${endpoint}: WebSocket not connected`);
            return;
        }

        // 如果只有一条消息，直接发送
        if (this.messageQueues[endpoint].length === 1) {
            ws.send(JSON.stringify(this.messageQueues[endpoint][0]));
            this.messageQueues[endpoint] = [];
            return;
        }

        // 否则，发送批处理消息
        const batchData = {
            type: 'batch',
            messages: [...this.messageQueues[endpoint]]
        };

        ws.send(JSON.stringify(batchData));

        // 清空队列
        this.messageQueues[endpoint] = [];
    }

    /**
     * 发送WebSocket消息
     * @param {string} endpoint - WebSocket端点
     * @param {Object} data - 要发送的数据
     * @param {boolean} batch - 是否启用批处理
     */
    sendWebSocketMessage(endpoint, data, batch = false) {
        // 如果启用批处理，添加到队列中
        if (batch) {
            this.addToBatchQueue(endpoint, data);
            return;
        }

        // 否则，直接发送
        const ws = this.wsConnections[endpoint];
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            const error = new Error(`WebSocket ${endpoint} not connected (readyState: ${ws ? ws.readyState : 'undefined'})`);
            console.error(error.message);
            throw error;
        }

        try {
            const message = JSON.stringify(data);
            console.debug(`Sending to ${endpoint}:`, data);
            ws.send(message);
        } catch (error) {
            console.error(`Error sending message to ${endpoint}:`, error);
            throw error;
        }
    }

    /**
     * 断开WebSocket连接
     * @param {string} endpoint - WebSocket端点
     */
    disconnectWebSocket(endpoint) {
        const ws = this.wsConnections[endpoint];
        if (ws) {
            // 处理剩余的消息队列
            this.processBatchQueue(endpoint);

            // 关闭连接
            try {
                ws.close(1000, "Normal closure");
            } catch (error) {
                console.warn(`Error closing WebSocket ${endpoint}:`, error);
            }

            delete this.wsConnections[endpoint];
            delete this.messageQueues[endpoint];

            if (this.batchTimers[endpoint]) {
                clearTimeout(this.batchTimers[endpoint]);
                delete this.batchTimers[endpoint];
            }
        }
    }

    /**
     * 断开所有WebSocket连接
     */
    disconnectAllWebSockets() {
        Object.keys(this.wsConnections).forEach(endpoint => {
            this.disconnectWebSocket(endpoint);
        });
    }

    //==================
    // 舵机控制API
    //==================

    /**
     * 连接到舵机控制WebSocket
     * @param {Object} callbacks - 回调函数对象
     */
    connectServoControl(callbacks = {}) {
        return this.connectWebSocket('/ws/servo', {
            ...callbacks,
            autoReconnect: true
        });
    }

    /**
     * 通过WebSocket设置舵机角度
     * @param {number} channel - 舵机通道 (0-15)
     * @param {number} angle - 角度 (0-180)
     * @param {boolean} batch - 是否启用批处理
     */
    setServoAngleWs(channel, angle, batch = false) {
        this.sendWebSocketMessage('/ws/servo', {
            cmd: 'set',
            id: channel,
            angle: angle,
            timestamp: new Date().getTime()
        }, batch);
    }

    /**
     * 通过WebSocket设置舵机PWM频率
     * @param {number} frequency - 频率 (50-300Hz)
     */
    setServoFrequencyWs(frequency) {
        this.sendWebSocketMessage('/ws/servo', {
            cmd: 'set_frequency',
            frequency: frequency,
            timestamp: new Date().getTime()
        });
    }

    //==================
    // 传感器数据API
    //==================

    /**
     * 连接到传感器数据WebSocket
     * @param {Object} callbacks - 回调函数对象
     */
    connectSensorData(callbacks = {}) {
        const ws = this.connectWebSocket('/sensor', {
            ...callbacks,
            autoReconnect: true,
            onOpen: () => {
                // 连接后自动订阅传感器数据
                this.subscribeSensorData();
                if (callbacks.onOpen) callbacks.onOpen();
            }
        });

        return ws;
    }

    /**
     * 订阅传感器数据
     */
    subscribeSensorData() {
        this.sendWebSocketMessage('/sensor', {
            cmd: 'subscribe',
            timestamp: new Date().getTime()
        });
    }

    /**
     * 取消订阅传感器数据
     */
    unsubscribeSensorData() {
        this.sendWebSocketMessage('/sensor', {
            cmd: 'unsubscribe',
            timestamp: new Date().getTime()
        });
    }

    //==================
    // 音频处理API
    //==================

    /**
     * 连接到音频处理WebSocket
     * @param {Object} callbacks - 回调函数对象
     */
    connectAudioControl(callbacks = {}) {
        return this.connectWebSocket('/audio', {
            ...callbacks,
            autoReconnect: true
        });
    }

    /**
     * 开始音频流
     */
    startAudioStream() {
        this.sendWebSocketMessage('/audio', {
            cmd: 'start_stream',
            timestamp: new Date().getTime()
        });
    }

    /**
     * 停止音频流
     */
    stopAudioStream() {
        this.sendWebSocketMessage('/audio', {
            cmd: 'stop_stream',
            timestamp: new Date().getTime()
        });
    }

    /**
     * 调整音量
     * @param {number} volume - 音量值 (0-100)
     */
    setAudioVolume(volume) {
        this.sendWebSocketMessage('/audio', {
            cmd: 'volume',
            value: volume,
            timestamp: new Date().getTime()
        });
    }

    //==================
    // 工具方法
    //==================

    /**
     * 检查是否已认证
     * @returns {boolean} - 是否已认证
     */
    isAuthenticated() {
        return this.authToken !== null;
    }

    /**
     * 设置批处理配置
     * @param {number} interval - 批处理间隔(毫秒)
     * @param {number} maxSize - 单个批次最大消息数
     */
    configureBatchProcessing(interval, maxSize) {
        if (interval !== undefined) this.batchInterval = interval;
        if (maxSize !== undefined) this.maxQueueSize = maxSize;
    }

    /**
     * 返回连接状态信息
     * @returns {Object} - 连接状态信息
     */
    getConnectionStatus() {
        const status = {
            http: {
                baseUrl: this.httpBaseUrl,
                authenticated: this.isAuthenticated()
            },
            websocket: {}
        };

        // 收集WebSocket连接状态
        for (const endpoint in this.wsConnections) {
            const ws = this.wsConnections[endpoint];
            let state = "unknown";

            if (ws) {
                switch (ws.readyState) {
                    case WebSocket.CONNECTING: state = "connecting"; break;
                    case WebSocket.OPEN: state = "connected"; break;
                    case WebSocket.CLOSING: state = "closing"; break;
                    case WebSocket.CLOSED: state = "closed"; break;
                }
            } else {
                state = "not_initialized";
            }

            status.websocket[endpoint] = {
                state,
                queueSize: (this.messageQueues[endpoint] || []).length,
                reconnectInterval: this.reconnectIntervals[endpoint] || this.baseReconnectInterval
            };
        }

        return status;
    }
}

// 创建全局API实例
window.xiaozhi = window.xiaozhi || {};
window.xiaozhi.api = new XiaozhiAPI();