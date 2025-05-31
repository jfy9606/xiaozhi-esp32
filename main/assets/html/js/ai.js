// DOM Elements
const chatContainer = document.getElementById('chat-container');
const messageInput = document.getElementById('message-input');
const sendButton = document.getElementById('send-button');
const startVoiceButton = document.getElementById('start-voice-button');
const stopVoiceButton = document.getElementById('stop-voice-button');
const connectionStatus = document.getElementById('connection-status');
const statusText = document.getElementById('status-text');

// AI conversation state
let isProcessing = false;
let isRecording = false;
let conversationHistory = [];

// Initialize page
document.addEventListener('DOMContentLoaded', initPage);

function initPage() {
    // Setup event listeners
    setupEventListeners();
    
    // Initial connection status - disconnected
    updateConnectionStatus(false);
    
    // Connect to WebSocket
    connectWebSocket();
    
    // Load conversation history if available
    loadConversationHistory();
    
    // Disconnect WebSocket when page closes
    window.addEventListener('beforeunload', () => {
        window.xiaozhi.api.disconnectAllWebSockets();
    });
}

function setupEventListeners() {
    // Send message on button click
    sendButton.addEventListener('click', sendMessage);
    
    // Send message on Enter key press
    messageInput.addEventListener('keypress', (e) => {
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            sendMessage();
        }
    });
    
    // Start voice input
    startVoiceButton.addEventListener('click', startVoiceInput);
    
    // Stop voice input
    stopVoiceButton.addEventListener('click', stopVoiceInput);
}

function connectWebSocket() {
    try {
        window.xiaozhi.api.connectAI({
            onOpen: () => {
                updateConnectionStatus(true);
                addLogMessage('已连接到AI服务', 'system');
            },
            onMessage: (data) => {
                handleAIResponse(data);
            },
            onClose: () => {
                updateConnectionStatus(false);
                addLogMessage('与AI服务断开连接', 'system');
            },
            onError: (error) => {
                addLogMessage(`WebSocket错误: ${error}`, 'error');
            }
        });
    } catch (error) {
        addLogMessage(`连接失败: ${error.message}`, 'error');
    }
}

function updateConnectionStatus(connected) {
    connectionStatus.className = `status-indicator ${connected ? 'status-connected' : 'status-disconnected'}`;
    statusText.textContent = connected ? '已连接' : '未连接';
    
    // Enable/disable controls
    messageInput.disabled = !connected;
    sendButton.disabled = !connected;
    startVoiceButton.disabled = !connected;
}

function sendMessage() {
    if (isProcessing || !messageInput.value.trim()) return;
    
    const message = messageInput.value.trim();
    messageInput.value = '';
    
    // Add user message to chat
    addChatMessage(message, 'user');
    
    // Add to conversation history
    conversationHistory.push({ role: 'user', content: message });
    
    // Send to AI
    isProcessing = true;
    sendButton.disabled = true;
    
    try {
        window.xiaozhi.api.sendAIMessage({
            message: message,
            history: conversationHistory
        });
        
        // Show thinking indicator
        addThinkingIndicator();
    } catch (error) {
        addLogMessage(`发送消息失败: ${error.message}`, 'error');
        isProcessing = false;
        sendButton.disabled = false;
        removeThinkingIndicator();
    }
}

function handleAIResponse(data) {
    removeThinkingIndicator();
    
    if (data.status === 'ok') {
        const response = data.response;
        
        // Add AI response to chat
        addChatMessage(response, 'ai');
        
        // Add to conversation history
        conversationHistory.push({ role: 'assistant', content: response });
        
        // Save conversation history
        saveConversationHistory();
        
        // Optional: Text-to-speech for AI response
        if (data.audio) {
            playResponseAudio(data.audio);
        }
    } else if (data.status === 'error') {
        addLogMessage(`AI响应错误: ${data.message}`, 'error');
    }
    
    isProcessing = false;
    sendButton.disabled = false;
}

function addChatMessage(message, sender) {
    const messageElement = document.createElement('div');
    messageElement.className = `message message-${sender}`;
    
    // Process markdown and sanitize HTML in AI responses
    if (sender === 'ai') {
        // Here we would process markdown and sanitize HTML
        // This is a simplified version
        messageElement.innerHTML = message.replace(/\n/g, '<br>');
    } else {
        messageElement.textContent = message;
    }
    
    const timeElement = document.createElement('div');
    timeElement.className = 'message-time';
    timeElement.textContent = new Date().toLocaleTimeString();
    
    messageElement.appendChild(timeElement);
    chatContainer.appendChild(messageElement);
    
    // Scroll to bottom
    chatContainer.scrollTop = chatContainer.scrollHeight;
}

function addLogMessage(message, type) {
    const logElement = document.createElement('div');
    logElement.className = `log-message log-${type}`;
    logElement.textContent = message;
    
    chatContainer.appendChild(logElement);
    chatContainer.scrollTop = chatContainer.scrollHeight;
}

function addThinkingIndicator() {
    const thinkingElement = document.createElement('div');
    thinkingElement.className = 'message message-ai thinking';
    thinkingElement.id = 'thinking-indicator';
    
    const dots = document.createElement('div');
    dots.className = 'thinking-dots';
    dots.innerHTML = '<span>.</span><span>.</span><span>.</span>';
    
    thinkingElement.appendChild(dots);
    chatContainer.appendChild(thinkingElement);
    chatContainer.scrollTop = chatContainer.scrollHeight;
}

function removeThinkingIndicator() {
    const thinkingElement = document.getElementById('thinking-indicator');
    if (thinkingElement) {
        thinkingElement.remove();
    }
}

function startVoiceInput() {
    if (isRecording) return;
    
    try {
        window.xiaozhi.api.startVoiceRecording();
        isRecording = true;
        startVoiceButton.classList.add('active');
        startVoiceButton.disabled = true;
        stopVoiceButton.disabled = false;
        addLogMessage('开始语音输入', 'system');
    } catch (error) {
        addLogMessage(`启动语音输入失败: ${error.message}`, 'error');
    }
}

function stopVoiceInput() {
    if (!isRecording) return;
    
    try {
        window.xiaozhi.api.stopVoiceRecording();
        isRecording = false;
        startVoiceButton.classList.remove('active');
        startVoiceButton.disabled = false;
        stopVoiceButton.disabled = true;
        addLogMessage('正在处理语音输入...', 'system');
    } catch (error) {
        addLogMessage(`停止语音输入失败: ${error.message}`, 'error');
    }
}

function playResponseAudio(audioData) {
    // Play audio response
    try {
        const audio = new Audio(`data:audio/wav;base64,${audioData}`);
        audio.play();
    } catch (error) {
        console.error('播放语音响应失败:', error);
    }
}

function saveConversationHistory() {
    try {
        localStorage.setItem('aiConversationHistory', JSON.stringify(conversationHistory));
    } catch (error) {
        console.error('保存对话历史失败:', error);
    }
}

function loadConversationHistory() {
    try {
        const savedHistory = localStorage.getItem('aiConversationHistory');
        if (savedHistory) {
            conversationHistory = JSON.parse(savedHistory);
            
            // Display loaded conversation history
            conversationHistory.forEach(entry => {
                const sender = entry.role === 'user' ? 'user' : 'ai';
                addChatMessage(entry.content, sender);
            });
        }
    } catch (error) {
        console.error('加载对话历史失败:', error);
    }
}

function clearConversation() {
    // Clear chat UI
    chatContainer.innerHTML = '';
    
    // Clear conversation history
    conversationHistory = [];
    
    // Save empty history
    saveConversationHistory();
    
    addLogMessage('对话已清空', 'system');
} 