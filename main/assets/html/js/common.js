/**
 * common.js - 网页应用通用JavaScript功能库
 * 提供在多个页面中共享的公共函数和工具
 */

// API URL 构建工具
function getApiUrl(path) {
  return '/api/' + path;
}

// 消息显示工具
function showMessage(message, type = 'info') {
  console.log(`[${type.toUpperCase()}] ${message}`);
  
  // 检查页面上是否存在消息容器
  let messageContainer = document.getElementById('message-container');
  if (!messageContainer) {
    // 创建消息容器
    messageContainer = document.createElement('div');
    messageContainer.id = 'message-container';
    messageContainer.style.position = 'fixed';
    messageContainer.style.top = '10px';
    messageContainer.style.right = '10px';
    messageContainer.style.zIndex = '1000';
    document.body.appendChild(messageContainer);
  }
  
  // 创建消息元素
  const messageElement = document.createElement('div');
  messageElement.className = `alert alert-${type}`;
  messageElement.style.marginBottom = '10px';
  messageElement.style.padding = '10px';
  messageElement.innerHTML = message;
  
  // 添加消息到容器
  messageContainer.appendChild(messageElement);
  
  // 3秒后自动移除消息
  setTimeout(() => {
    messageElement.style.opacity = '0';
    messageElement.style.transition = 'opacity 0.5s';
    setTimeout(() => {
      messageContainer.removeChild(messageElement);
    }, 500);
  }, 3000);
}

// WebSocket连接管理
class WebSocketClient {
  constructor(url = null) {
    this.url = url || `ws://${window.location.host}/ws`;
    this.socket = null;
    this.isConnected = false;
    this.reconnectTimer = null;
    this.messageHandlers = {};
  }
  
  connect() {
    if (this.socket) {
      this.close();
    }
    
    try {
      this.socket = new WebSocket(this.url);
      
      this.socket.onopen = () => {
        console.log('WebSocket连接已建立');
        this.isConnected = true;
        this.startHeartbeat();
        
        // 发送客户端注册消息
        this.send({
          type: 'register_client',
          client_type: 'web'
        });
      };
      
      this.socket.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data);
          this.handleMessage(message);
        } catch (error) {
          console.error('处理WebSocket消息出错:', error);
        }
      };
      
      this.socket.onclose = () => {
        console.log('WebSocket连接已关闭');
        this.isConnected = false;
        this.scheduleReconnect();
      };
      
      this.socket.onerror = (error) => {
        console.error('WebSocket错误:', error);
        this.isConnected = false;
      };
      
    } catch (error) {
      console.error('创建WebSocket连接出错:', error);
      this.scheduleReconnect();
    }
  }
  
  close() {
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    this.isConnected = false;
    
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
    
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }
  
  scheduleReconnect(delay = 5000) {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
    }
    
    this.reconnectTimer = setTimeout(() => {
      console.log('尝试重新连接WebSocket...');
      this.connect();
    }, delay);
  }
  
  startHeartbeat(interval = 30000) {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
    }
    
    this.heartbeatTimer = setInterval(() => {
      if (this.isConnected) {
        this.send({ type: 'heartbeat' });
      }
    }, interval);
  }
  
  send(data) {
    if (!this.isConnected) {
      console.warn('WebSocket未连接，无法发送消息');
      return false;
    }
    
    try {
      const message = typeof data === 'string' ? data : JSON.stringify(data);
      this.socket.send(message);
      return true;
    } catch (error) {
      console.error('发送WebSocket消息出错:', error);
      return false;
    }
  }
  
  registerHandler(type, callback) {
    if (!this.messageHandlers[type]) {
      this.messageHandlers[type] = [];
    }
    this.messageHandlers[type].push(callback);
  }
  
  handleMessage(message) {
    const type = message.type || 'unknown';
    
    if (this.messageHandlers[type]) {
      this.messageHandlers[type].forEach(handler => {
        try {
          handler(message);
        } catch (error) {
          console.error(`处理 ${type} 消息时出错:`, error);
        }
      });
    }
    
    // 处理所有消息的处理器
    if (this.messageHandlers['*']) {
      this.messageHandlers['*'].forEach(handler => {
        try {
          handler(message);
        } catch (error) {
          console.error('处理通用消息时出错:', error);
        }
      });
    }
  }
}

// 导出全局变量
window.wsClient = null;

// 页面加载完成后自动初始化WebSocket
document.addEventListener('DOMContentLoaded', () => {
  // 创建全局WebSocket客户端
  window.wsClient = new WebSocketClient();
  window.wsClient.connect();
  
  // 注册通用消息处理器
  window.wsClient.registerHandler('error', (message) => {
    showMessage(message.message || '发生错误', 'danger');
  });
  
  window.wsClient.registerHandler('pong', (message) => {
    console.log('收到服务器心跳响应:', message);
  });
  
  window.wsClient.registerHandler('status_response', (message) => {
    console.log('收到系统状态:', message);
    // 可以在这里更新UI上的系统状态信息
  });
}); 