/**
 * XiaozhiAPI - 小智ESP32 API客户端库
 * 
 * 提供与小智ESP32后端API的通信功能，包括HTTP和WebSocket接口
 */

class XiaozhiAPI {
    /**
     * 构造函数
     * @param {string} baseUrl - API基础URL，默认为当前主机
     */
    constructor(baseUrl) {
        this.baseUrl = baseUrl || window.location.origin;
        this.httpBaseUrl = `${this.baseUrl}/api/v1/http`;
        this.wsBaseUrl = `${this.baseUrl.replace(/^http/, 'ws')}/api/v1/ws`;
        this.wsConnections = {};
        this.wsCallbacks = {};
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

        if (data && (method === 'POST' || method === 'PUT')) {
            options.body = JSON.stringify(data);
        }

        try {
            const response = await fetch(url, options);
            
            if (!response.ok) {
                const errorText = await response.text();
                throw new Error(`API Error: ${response.status} ${response.statusText} - ${errorText}`);
            }
            
            return await response.json();
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
        
        const ws = new WebSocket(url);
        this.wsConnections[endpoint] = ws;
        this.wsCallbacks[endpoint] = callbacks;

        ws.onopen = () => {
            console.log(`Connected to WebSocket ${endpoint}`);
            if (callbacks.onOpen) callbacks.onOpen();
        };

        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                console.log(`Received from ${endpoint}:`, data);
                if (callbacks.onMessage) callbacks.onMessage(data);
            } catch (error) {
                console.error(`Error parsing WebSocket message: ${error.message}`);
            }
        };

        ws.onclose = (event) => {
            console.log(`Disconnected from WebSocket ${endpoint}: code=${event.code}, reason=${event.reason}`);
            delete this.wsConnections[endpoint];
            if (callbacks.onClose) callbacks.onClose(event);
            
            // 如果不是正常关闭且配置了重连，则尝试重连
            if (event.code !== 1000 && callbacks.autoReconnect) {
                console.log(`Attempting to reconnect to ${endpoint} in 3 seconds...`);
                setTimeout(() => {
                    this.connectWebSocket(endpoint, callbacks);
                }, 3000);
            }
        };

        ws.onerror = (error) => {
            console.error(`WebSocket ${endpoint} error:`, error);
            if (callbacks.onError) callbacks.onError(error);
        };

        return ws;
    }

    /**
     * 发送WebSocket消息
     * @param {string} endpoint - WebSocket端点
     * @param {Object} data - 要发送的数据
     */
    sendWebSocketMessage(endpoint, data) {
        const ws = this.wsConnections[endpoint];
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            throw new Error(`WebSocket ${endpoint} not connected`);
        }
        
        ws.send(JSON.stringify(data));
    }

    /**
     * 断开WebSocket连接
     * @param {string} endpoint - WebSocket端点
     */
    disconnectWebSocket(endpoint) {
        const ws = this.wsConnections[endpoint];
        if (ws) {
            ws.close();
            delete this.wsConnections[endpoint];
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
        return this.connectWebSocket('/servo', {
            ...callbacks,
            autoReconnect: true
        });
    }

    /**
     * 通过WebSocket设置舵机角度
     * @param {number} channel - 舵机通道 (0-15)
     * @param {number} angle - 角度 (0-180)
     */
    setServoAngleWs(channel, angle) {
        this.sendWebSocketMessage('/servo', {
            cmd: 'set_angle',
            channel: channel,
            angle: angle
        });
    }

    /**
     * 通过WebSocket设置舵机PWM频率
     * @param {number} frequency - 频率 (50-300Hz)
     */
    setServoFrequencyWs(frequency) {
        this.sendWebSocketMessage('/servo', {
            cmd: 'set_frequency',
            frequency: frequency
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
            cmd: 'subscribe'
        });
    }

    /**
     * 取消订阅传感器数据
     */
    unsubscribeSensorData() {
        this.sendWebSocketMessage('/sensor', {
            cmd: 'unsubscribe'
        });
    }
}

// 创建全局API实例
window.xiaozhi = window.xiaozhi || {};
window.xiaozhi.api = new XiaozhiAPI(); 