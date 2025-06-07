/**
 * common.js - 小智ESP32通用JavaScript工具库
 * 提供通用功能：错误处理，日志记录，安全增强等
 */

// 日志级别
const LOG_LEVELS = {
    ERROR: 0,
    WARN: 1,
    INFO: 2,
    DEBUG: 3
};

// 日志系统
const Logger = {
    // 默认日志级别设置为INFO
    level: LOG_LEVELS.INFO,
    
    // 是否启用日志时间戳
    enableTimestamp: true,
    
    // 是否启用控制台颜色
    enableColors: true,
    
    // 消息历史（最多保留100条）
    messageHistory: [],
    maxHistorySize: 100,
    
    // 设置日志级别
    setLevel: function(level) {
        this.level = level;
        console.log(`日志级别设置为: ${Object.keys(LOG_LEVELS).find(key => LOG_LEVELS[key] === level)}`);
    },
    
    // 格式化日志消息
    formatMessage: function(message, data) {
        let timestamp = '';
        if (this.enableTimestamp) {
            const now = new Date();
            timestamp = `[${now.toLocaleTimeString()}] `;
        }
        
        let formattedMsg = `${timestamp}${message}`;
        
        // 记录到历史
        this.messageHistory.push({
            timestamp: new Date(),
            message: formattedMsg,
            data: data
        });
        
        // 保持历史记录在最大大小范围内
        if (this.messageHistory.length > this.maxHistorySize) {
            this.messageHistory.shift();
        }
        
        return formattedMsg;
    },
    
    // 错误日志
    error: function(message, data) {
        if (this.level >= LOG_LEVELS.ERROR) {
            console.error(this.formatMessage(`❌ ${message}`, data), data || '');
        }
    },
    
    // 警告日志
    warn: function(message, data) {
        if (this.level >= LOG_LEVELS.WARN) {
            console.warn(this.formatMessage(`⚠️ ${message}`, data), data || '');
        }
    },
    
    // 信息日志
    info: function(message, data) {
        if (this.level >= LOG_LEVELS.INFO) {
            console.info(this.formatMessage(`ℹ️ ${message}`, data), data || '');
        }
    },
    
    // 调试日志
    debug: function(message, data) {
        if (this.level >= LOG_LEVELS.DEBUG) {
            console.debug(this.formatMessage(`🔍 ${message}`, data), data || '');
        }
    },
    
    // 获取日志历史
    getHistory: function() {
        return [...this.messageHistory];
    },
    
    // 清除日志历史
    clearHistory: function() {
        this.messageHistory = [];
    }
};

// 安全工具
const Security = {
    // XSS防护 - 清理HTML字符串
    sanitizeHTML: function(str) {
        if (!str) return '';
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#039;');
    },
    
    // 安全地插入HTML内容
    safeInsertHTML: function(element, html) {
        if (!element) return;
        
        // 首先创建一个安全的文档片段
        const fragment = document.createDocumentFragment();
        const tempDiv = document.createElement('div');
        tempDiv.innerHTML = html;
        
        // 移除潜在的危险内容（脚本、样式等）
        const scripts = tempDiv.querySelectorAll('script');
        scripts.forEach(script => script.remove());
        
        const styles = tempDiv.querySelectorAll('style');
        styles.forEach(style => style.remove());
        
        // 复制净化后的内容
        while (tempDiv.firstChild) {
            fragment.appendChild(tempDiv.firstChild);
        }
        
        // 清空目标元素并插入安全内容
        while (element.firstChild) {
            element.removeChild(element.firstChild);
        }
        element.appendChild(fragment);
    },
    
    // CSRF令牌
    csrfToken: null,
    
    // 初始化CSRF保护
    initCSRF: function() {
        this.csrfToken = localStorage.getItem('csrf_token');
        
        if (!this.csrfToken) {
            // 生成随机令牌
            this.csrfToken = this.generateRandomToken(32);
            localStorage.setItem('csrf_token', this.csrfToken);
        }
    },
    
    // 获取CSRF令牌
    getCSRFToken: function() {
        if (!this.csrfToken) {
            this.initCSRF();
        }
        return this.csrfToken;
    },
    
    // 生成随机令牌
    generateRandomToken: function(length) {
        const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
        let result = '';
        const crypto = window.crypto || window.msCrypto;
        
        if (crypto && crypto.getRandomValues) {
            const values = new Uint32Array(length);
            crypto.getRandomValues(values);
            for (let i = 0; i < length; i++) {
                result += chars[values[i] % chars.length];
            }
        } else {
            for (let i = 0; i < length; i++) {
                result += chars.charAt(Math.floor(Math.random() * chars.length));
            }
        }
        return result;
    }
};

// UI工具
const UI = {
    // 显示提示消息
    showToast: function(message, type = 'info', duration = 3000) {
        // 检查是否已存在toast容器
        let toastContainer = document.getElementById('toast-container');
        if (!toastContainer) {
            // 创建toast容器
            toastContainer = document.createElement('div');
            toastContainer.id = 'toast-container';
            toastContainer.style.position = 'fixed';
            toastContainer.style.bottom = '20px';
            toastContainer.style.right = '20px';
            toastContainer.style.zIndex = '9999';
            document.body.appendChild(toastContainer);
        }
        
        // 创建新的toast
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;
        
        // 设置样式
        toast.style.padding = '10px 15px';
        toast.style.marginBottom = '10px';
        toast.style.borderRadius = '3px';
        toast.style.width = '250px';
        toast.style.transition = 'opacity 0.3s ease';
        toast.style.opacity = '0';
        
        // 按照类型设置背景色
        switch (type) {
            case 'success':
                toast.style.backgroundColor = '#4CAF50';
                toast.style.color = 'white';
                break;
            case 'error':
                toast.style.backgroundColor = '#F44336';
                toast.style.color = 'white';
                break;
            case 'warning':
                toast.style.backgroundColor = '#FF9800';
                toast.style.color = 'white';
                break;
            default:
                toast.style.backgroundColor = '#2196F3';
                toast.style.color = 'white';
                break;
        }
        
        // 设置消息
        toast.textContent = message;
        
        // 添加到容器
        toastContainer.appendChild(toast);
        
        // 淡入
        setTimeout(() => {
            toast.style.opacity = '1';
        }, 10);
        
        // 自动删除
        setTimeout(() => {
            toast.style.opacity = '0';
            toast.addEventListener('transitionend', function() {
                if (toast.parentNode === toastContainer) {
                    toastContainer.removeChild(toast);
                }
            });
        }, duration);
    },
    
    // 显示确认对话框
    showConfirmDialog: function(message, confirmCallback, cancelCallback, options = {}) {
        // 默认选项
        const defaultOptions = {
            title: '确认',
            confirmButtonText: '确认',
            cancelButtonText: '取消'
        };
        
        const settings = { ...defaultOptions, ...options };
        
        // 创建对话框背景
        const overlay = document.createElement('div');
        overlay.style.position = 'fixed';
        overlay.style.top = '0';
        overlay.style.left = '0';
        overlay.style.right = '0';
        overlay.style.bottom = '0';
        overlay.style.backgroundColor = 'rgba(0, 0, 0, 0.5)';
        overlay.style.zIndex = '10000';
        overlay.style.display = 'flex';
        overlay.style.alignItems = 'center';
        overlay.style.justifyContent = 'center';
        
        // 创建对话框
        const dialog = document.createElement('div');
        dialog.style.backgroundColor = 'white';
        dialog.style.padding = '20px';
        dialog.style.borderRadius = '5px';
        dialog.style.width = '300px';
        dialog.style.maxWidth = '90%';
        
        // 标题
        const titleElem = document.createElement('h3');
        titleElem.textContent = settings.title;
        titleElem.style.margin = '0 0 15px 0';
        dialog.appendChild(titleElem);
        
        // 消息
        const messageElem = document.createElement('p');
        messageElem.textContent = message;
        messageElem.style.margin = '0 0 20px 0';
        dialog.appendChild(messageElem);
        
        // 按钮容器
        const buttonContainer = document.createElement('div');
        buttonContainer.style.display = 'flex';
        buttonContainer.style.justifyContent = 'flex-end';
        
        // 取消按钮
        const cancelButton = document.createElement('button');
        cancelButton.textContent = settings.cancelButtonText;
        cancelButton.style.marginRight = '10px';
        cancelButton.style.padding = '8px 12px';
        cancelButton.style.border = '1px solid #ddd';
        cancelButton.style.borderRadius = '3px';
        cancelButton.style.backgroundColor = '#f5f5f5';
        cancelButton.addEventListener('click', () => {
            document.body.removeChild(overlay);
            if (cancelCallback) cancelCallback();
        });
        buttonContainer.appendChild(cancelButton);
        
        // 确认按钮
        const confirmButton = document.createElement('button');
        confirmButton.textContent = settings.confirmButtonText;
        confirmButton.style.padding = '8px 12px';
        confirmButton.style.border = 'none';
        confirmButton.style.borderRadius = '3px';
        confirmButton.style.backgroundColor = '#4CAF50';
        confirmButton.style.color = 'white';
        confirmButton.addEventListener('click', () => {
            document.body.removeChild(overlay);
            if (confirmCallback) confirmCallback();
        });
        buttonContainer.appendChild(confirmButton);
        
        dialog.appendChild(buttonContainer);
        overlay.appendChild(dialog);
        document.body.appendChild(overlay);
    },
    
    // 显示加载指示器
    showLoader: function(message = '加载中...') {
        // 检查是否已存在loader
        let loader = document.getElementById('global-loader');
        if (loader) {
            return;
        }
        
        // 创建loader容器
        loader = document.createElement('div');
        loader.id = 'global-loader';
        loader.style.position = 'fixed';
        loader.style.top = '0';
        loader.style.left = '0';
        loader.style.width = '100%';
        loader.style.height = '100%';
        loader.style.backgroundColor = 'rgba(0, 0, 0, 0.5)';
        loader.style.display = 'flex';
        loader.style.flexDirection = 'column';
        loader.style.alignItems = 'center';
        loader.style.justifyContent = 'center';
        loader.style.zIndex = '10001';
        
        // 创建loader动画
        const spinner = document.createElement('div');
        spinner.style.border = '5px solid #f3f3f3';
        spinner.style.borderTop = '5px solid #3498db';
        spinner.style.borderRadius = '50%';
        spinner.style.width = '50px';
        spinner.style.height = '50px';
        spinner.style.animation = 'spin 2s linear infinite';
        
        // 添加CSS动画
        const style = document.createElement('style');
        style.innerHTML = '@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }';
        document.head.appendChild(style);
        
        // 消息
        const messageElem = document.createElement('div');
        messageElem.textContent = message;
        messageElem.style.marginTop = '15px';
        messageElem.style.color = 'white';
        
        loader.appendChild(spinner);
        loader.appendChild(messageElem);
        document.body.appendChild(loader);
    },
    
    // 隐藏加载指示器
    hideLoader: function() {
        const loader = document.getElementById('global-loader');
        if (loader) {
            document.body.removeChild(loader);
        }
    }
};

// 错误处理工具
const ErrorHandler = {
    // 存储错误处理程序
    handlers: {},
    
    // 全局错误处理
    init: function() {
        // 处理JS错误
        window.onerror = (message, source, line, column, error) => {
            this.handleError({
                type: 'javascript',
                message: message,
                source: source,
                line: line,
                column: column,
                stack: error && error.stack,
                timestamp: new Date()
            });
            // 不阻止默认处理
            return false;
        };
        
        // 处理未捕获的Promise错误
        window.addEventListener('unhandledrejection', (event) => {
            this.handleError({
                type: 'promise',
                message: event.reason.message || 'Unhandled Promise rejection',
                stack: event.reason.stack,
                timestamp: new Date()
            });
        });
        
        // 处理网络错误
        window.addEventListener('error', (event) => {
            // 仅处理资源错误
            if (event.target && (event.target.tagName === 'SCRIPT' || 
                               event.target.tagName === 'LINK' || 
                               event.target.tagName === 'IMG')) {
                this.handleError({
                    type: 'resource',
                    message: `Failed to load ${event.target.tagName.toLowerCase()}: ${event.target.src || event.target.href}`,
                    timestamp: new Date()
                });
            }
        }, true);
        
        Logger.info('ErrorHandler initialized');
    },
    
    // 注册自定义错误处理程序
    register: function(type, handler) {
        if (typeof handler !== 'function') {
            throw new Error('Error handler must be a function');
        }
        
        if (!this.handlers[type]) {
            this.handlers[type] = [];
        }
        
        this.handlers[type].push(handler);
        Logger.debug(`Registered error handler for type: ${type}`);
    },
    
    // 处理错误
    handleError: function(error) {
        // 首先记录错误
        Logger.error(`[${error.type}] ${error.message}`, error);
        
        // 调用注册的处理程序
        const generalHandlers = this.handlers['*'] || [];
        const typeHandlers = this.handlers[error.type] || [];
        
        const allHandlers = [...generalHandlers, ...typeHandlers];
        
        for (const handler of allHandlers) {
            try {
                handler(error);
            } catch (handlerError) {
                console.error('Error in error handler:', handlerError);
            }
        }
        
        // 发送到远程服务器（如果配置了）
        this.reportError(error);
    },
    
    // 将错误报告到服务器
    reportError: function(error) {
        // 这里可以实现发送到服务器的逻辑
        // 例如：
        /*
        if (window.xiaozhi && window.xiaozhi.api) {
            try {
                window.xiaozhi.api.request('POST', '/error/report', error)
                    .catch(e => console.error('Failed to report error:', e));
            } catch (e) {
                console.error('Error reporting failed:', e);
            }
        }
        */
    }
};

// 网络状态监控
const NetworkMonitor = {
    // 存储回调函数
    callbacks: {
        online: [],
        offline: []
    },
    
    // 初始化网络监视器
    init: function() {
        window.addEventListener('online', () => {
            Logger.info('网络已恢复连接');
            UI.showToast('网络已恢复连接', 'success');
            this.notifyCallbacks('online');
        });
        
        window.addEventListener('offline', () => {
            Logger.warn('网络连接已断开');
            UI.showToast('网络连接已断开', 'error', 5000);
            this.notifyCallbacks('offline');
        });
        
        Logger.info('NetworkMonitor initialized');
    },
    
    // 注册回调
    onOnline: function(callback) {
        if (typeof callback === 'function') {
            this.callbacks.online.push(callback);
        }
    },
    
    onOffline: function(callback) {
        if (typeof callback === 'function') {
            this.callbacks.offline.push(callback);
        }
    },
    
    // 通知回调
    notifyCallbacks: function(type) {
        for (const callback of this.callbacks[type]) {
            try {
                callback();
            } catch (error) {
                Logger.error(`Error in ${type} callback:`, error);
            }
        }
    },
    
    // 检查当前网络状态
    isOnline: function() {
        return navigator.onLine;
    }
};

// 性能监控
const PerformanceMonitor = {
    // 存储性能指标
    metrics: {
        pageLoad: null,
        resources: [],
        apiCalls: [],
        websocketMessages: []
    },
    
    // 初始化性能监视器
    init: function() {
        // 页面加载性能
        window.addEventListener('load', () => {
            setTimeout(() => {
                const perfData = window.performance.timing;
                const pageLoadTime = perfData.loadEventEnd - perfData.navigationStart;
                this.metrics.pageLoad = {
                    total: pageLoadTime,
                    network: perfData.responseEnd - perfData.fetchStart,
                    domProcessing: perfData.domComplete - perfData.domLoading,
                    rendering: perfData.loadEventStart - perfData.domComplete
                };
                
                Logger.debug(`页面加载完成，总耗时: ${pageLoadTime}ms`, this.metrics.pageLoad);
            }, 0);
        });
        
        // 资源加载性能
        if (window.performance && window.performance.getEntriesByType) {
            setTimeout(() => {
                const resourceEntries = window.performance.getEntriesByType('resource');
                this.metrics.resources = resourceEntries.map(entry => ({
                    name: entry.name,
                    type: entry.initiatorType,
                    duration: entry.duration,
                    size: entry.transferSize
                }));
                
                // 计算总资源加载时间和大小
                const totalSize = this.metrics.resources.reduce((sum, res) => sum + (res.size || 0), 0);
                Logger.debug(`加载了 ${this.metrics.resources.length} 个资源，总大小: ${Math.round(totalSize / 1024)}KB`);
            }, 1000);
        }
        
        Logger.info('PerformanceMonitor initialized');
    },
    
    // 记录API调用性能
    recordApiCall: function(endpoint, method, duration, status) {
        this.metrics.apiCalls.push({
            endpoint,
            method,
            duration,
            status,
            timestamp: new Date()
        });
    },
    
    // 记录WebSocket消息性能
    recordWebSocketMessage: function(endpoint, messageType, size, duration) {
        this.metrics.websocketMessages.push({
            endpoint,
            messageType,
            size,
            duration,
            timestamp: new Date()
        });
    },
    
    // 获取性能报告
    getReport: function() {
        return {
            pageLoad: this.metrics.pageLoad,
            resources: {
                count: this.metrics.resources.length,
                totalSize: this.metrics.resources.reduce((sum, res) => sum + (res.size || 0), 0),
                slowest: this.metrics.resources.slice().sort((a, b) => b.duration - a.duration).slice(0, 5)
            },
            api: {
                count: this.metrics.apiCalls.length,
                totalDuration: this.metrics.apiCalls.reduce((sum, call) => sum + call.duration, 0),
                avgDuration: this.metrics.apiCalls.length ? 
                    this.metrics.apiCalls.reduce((sum, call) => sum + call.duration, 0) / this.metrics.apiCalls.length : 0,
                slowest: this.metrics.apiCalls.slice().sort((a, b) => b.duration - a.duration).slice(0, 5)
            },
            websocket: {
                count: this.metrics.websocketMessages.length,
                totalSize: this.metrics.websocketMessages.reduce((sum, msg) => sum + (msg.size || 0), 0),
                avgDuration: this.metrics.websocketMessages.length ?
                    this.metrics.websocketMessages.reduce((sum, msg) => sum + msg.duration, 0) / 
                    this.metrics.websocketMessages.length : 0
            }
        };
    }
};

// 小智ESP32工具集命名空间
window.xiaozhi = window.xiaozhi || {};
window.xiaozhi.utils = {
    Logger,
    Security,
    UI,
    ErrorHandler,
    NetworkMonitor,
    PerformanceMonitor
};

// 初始化组件
document.addEventListener('DOMContentLoaded', function() {
    // 初始化安全组件
    window.xiaozhi.utils.Security.initCSRF();
    
    // 初始化错误处理
    window.xiaozhi.utils.ErrorHandler.init();
    
    // 初始化网络监控
    window.xiaozhi.utils.NetworkMonitor.init();
    
    // 初始化性能监控
    window.xiaozhi.utils.PerformanceMonitor.init();
    
    console.log('小智ESP32工具库初始化完成');
}); 