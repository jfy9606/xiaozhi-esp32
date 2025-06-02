/**
 * Xiaozhi ESP32 Vehicle Control System
 * AI Chat Module
 */

// AI Chat system
class AIChat {
    constructor(containerId) {
        // DOM Elements
        this.container = document.getElementById(containerId);
        
        // Chat state
        this.isListening = false;
        this.isSpeaking = false;
        this.messageHistory = [];
        this.pendingResponse = false;
        
        // Speech recognition and synthesis
        this.recognition = null;
        this.speechSynthesis = window.speechSynthesis;
        this.voices = [];
        
        // Speech settings
        this.settings = {
            language: 'zh-CN',
            voiceURI: null,
            pitch: 1,
            rate: 1,
            volume: 1,
            autoSpeak: true
        };
        
        // UI elements
        this.messagesContainer = null;
        this.inputField = null;
        this.voiceButton = null;
        
        // Initialize
        this.init();
    }
    
    /**
     * Initialize AI chat
     */
    init() {
        this.createChatInterface();
        this.setupSpeechRecognition();
        this.loadVoices();
        this.setupEventListeners();
        
        // Register for WebSocket messages
        if (window.app) {
            window.app.registerWebSocketHandler('ai_response', this.handleResponse.bind(this));
        }
        
        // Set as global instance
        window.aiSystem = this;
        
        console.log('AI chat system initialized');
    }
    
    /**
     * Create chat interface
     */
    createChatInterface() {
        if (!this.container) return;
        
        // Clear container
        this.container.innerHTML = '';
        
        // Create chat interface
        this.container.innerHTML = `
            <div class="chat-messages" id="chat-messages"></div>
            <div class="message-input">
                <input type="text" id="message-input" placeholder="输入消息或点击麦克风语音输入..." />
                <button class="voice-button" id="voice-button">🎤</button>
                <button class="btn btn-primary" id="send-button">发送</button>
            </div>
        `;
        
        // Get references to UI elements
        this.messagesContainer = document.getElementById('chat-messages');
        this.inputField = document.getElementById('message-input');
        this.voiceButton = document.getElementById('voice-button');
        this.sendButton = document.getElementById('send-button');
        
        // Add welcome message
        this.addSystemMessage('您好，我是您的AI助手，您可以向我提问或指令我控制车辆。');
    }
    
    /**
     * Load available voices for speech synthesis
     */
    loadVoices() {
        // Get available voices
        this.voices = this.speechSynthesis.getVoices();
        
        // If no voices available yet, wait for them to load
        if (this.voices.length === 0) {
            this.speechSynthesis.onvoiceschanged = () => {
                this.voices = this.speechSynthesis.getVoices();
                this.selectVoice();
            };
        } else {
            this.selectVoice();
        }
    }
    
    /**
     * Select the best voice for the current language
     */
    selectVoice() {
        // Try to find a voice matching the current language
        const langVoices = this.voices.filter(voice => voice.lang.includes(this.settings.language));
        
        if (langVoices.length > 0) {
            // Prefer female voices if available
            const femaleVoice = langVoices.find(voice => voice.name.includes('Female'));
            this.settings.voiceURI = femaleVoice ? femaleVoice.voiceURI : langVoices[0].voiceURI;
        }
    }
    
    /**
     * Setup speech recognition
     */
    setupSpeechRecognition() {
        // Check if speech recognition is supported
        const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
        
        if (!SpeechRecognition) {
            console.warn('Speech recognition not supported in this browser');
            return;
        }
        
        // Create speech recognition instance
        this.recognition = new SpeechRecognition();
        this.recognition.continuous = false;
        this.recognition.interimResults = false;
        this.recognition.lang = this.settings.language;
        
        // Set event handlers
        this.recognition.onstart = () => {
            this.isListening = true;
            this.updateVoiceButton(true);
        };
        
        this.recognition.onend = () => {
            this.isListening = false;
            this.updateVoiceButton(false);
        };
        
        this.recognition.onresult = (event) => {
            const transcript = event.results[0][0].transcript;
            this.inputField.value = transcript;
            this.sendMessage();
        };
        
        this.recognition.onerror = (event) => {
            console.error('Speech recognition error:', event.error);
            this.isListening = false;
            this.updateVoiceButton(false);
        };
    }
    
    /**
     * Setup event listeners
     */
    setupEventListeners() {
        // Voice button click
        if (this.voiceButton) {
            this.voiceButton.addEventListener('click', () => {
                this.toggleVoiceInput();
            });
        }
        
        // Send button click
        if (this.sendButton) {
            this.sendButton.addEventListener('click', () => {
                this.sendMessage();
            });
        }
        
        // Input field enter key
        if (this.inputField) {
            this.inputField.addEventListener('keydown', (e) => {
                if (e.key === 'Enter') {
                    this.sendMessage();
                }
            });
        }
    }
    
    /**
     * Toggle voice input
     */
    toggleVoiceInput() {
        if (!this.recognition) {
            window.app?.showNotification('您的浏览器不支持语音输入', 'error');
            return;
        }
        
        if (this.isListening) {
            this.recognition.stop();
        } else {
            this.recognition.start();
        }
    }
    
    /**
     * Update voice button UI
     * @param {boolean} isActive - Whether voice input is active
     */
    updateVoiceButton(isActive) {
        if (!this.voiceButton) return;
        
        if (isActive) {
            this.voiceButton.classList.add('active');
        } else {
            this.voiceButton.classList.remove('active');
        }
    }
    
    /**
     * Send message to AI
     */
    sendMessage() {
        if (!this.inputField) return;
        
        const message = this.inputField.value.trim();
        if (!message) return;
        
        // Add message to chat
        this.addUserMessage(message);
        
        // Clear input field
        this.inputField.value = '';
        
        // Disable input while waiting for response
        this.setInputEnabled(false);
        
        // Send message to server
        if (window.app) {
            window.app.sendWebSocketMessage({
                type: 'ai_message',
                message: message,
                history: this.messageHistory.slice(-10) // Send last 10 messages as context
            });
        } else {
            // If no WebSocket connection, simulate a response
            setTimeout(() => {
                this.handleResponse({
                    message: '抱歉，我目前无法连接到服务器。请检查您的网络连接。'
                });
            }, 1000);
        }
    }
    
    /**
     * Add user message to chat
     * @param {string} text - Message text
     */
    addUserMessage(text) {
        const message = {
            role: 'user',
            content: text,
            timestamp: new Date().getTime()
        };
        
        this.messageHistory.push(message);
        
        // Add message to UI
        this.addMessageElement('message-user', text);
        
        // Scroll to bottom
        this.scrollToBottom();
    }
    
    /**
     * Add AI message to chat
     * @param {string} text - Message text
     */
    addAIMessage(text) {
        const message = {
            role: 'assistant',
            content: text,
            timestamp: new Date().getTime()
        };
        
        this.messageHistory.push(message);
        
        // Add message to UI
        this.addMessageElement('message-ai', text);
        
        // Scroll to bottom
        this.scrollToBottom();
        
        // Speak the message if enabled
        if (this.settings.autoSpeak) {
            this.speak(text);
        }
    }
    
    /**
     * Add system message to chat (not included in history)
     * @param {string} text - Message text
     */
    addSystemMessage(text) {
        // Add message to UI
        this.addMessageElement('message-system', text);
        
        // Scroll to bottom
        this.scrollToBottom();
    }
    
    /**
     * Add message element to chat
     * @param {string} className - Message CSS class
     * @param {string} text - Message text
     */
    addMessageElement(className, text) {
        if (!this.messagesContainer) return;
        
        const messageElement = document.createElement('div');
        messageElement.className = `message ${className}`;
        messageElement.textContent = text;
        
        this.messagesContainer.appendChild(messageElement);
    }
    
    /**
     * Scroll chat to bottom
     */
    scrollToBottom() {
        if (!this.messagesContainer) return;
        
        this.messagesContainer.scrollTop = this.messagesContainer.scrollHeight;
    }
    
    /**
     * Handle AI response
     * @param {Object} response - Response object
     */
    handleResponse(response) {
        if (!response || !response.message) return;
        
        // Add AI message to chat
        this.addAIMessage(response.message);
        
        // Enable input
        this.setInputEnabled(true);
        
        // Process any commands in the response
        if (response.commands) {
            this.processCommands(response.commands);
        }
    }
    
    /**
     * Process commands from AI response
     * @param {Array} commands - Commands to process
     */
    processCommands(commands) {
        commands.forEach(command => {
            switch (command.type) {
                case 'vehicle_control':
                    // Forward to vehicle control system
                    if (window.vehicleSystem) {
                        window.app?.sendWebSocketMessage({
                            type: 'vehicle_control',
                            ...command.params
                        });
                    }
                    break;
                    
                case 'camera_control':
                    // Forward to camera system
                    if (window.cameraSystem) {
                        window.app?.sendWebSocketMessage({
                            type: 'camera_control',
                            ...command.params
                        });
                    }
                    break;
                    
                default:
                    console.log(`Unknown command type: ${command.type}`);
                    break;
            }
        });
    }
    
    /**
     * Set input enabled state
     * @param {boolean} enabled - Whether input is enabled
     */
    setInputEnabled(enabled) {
        if (this.inputField) {
            this.inputField.disabled = !enabled;
        }
        
        if (this.voiceButton) {
            this.voiceButton.disabled = !enabled;
        }
        
        if (this.sendButton) {
            this.sendButton.disabled = !enabled;
        }
        
        this.pendingResponse = !enabled;
    }
    
    /**
     * Speak text using speech synthesis
     * @param {string} text - Text to speak
     */
    speak(text) {
        if (!this.speechSynthesis) return;
        
        // Cancel any current speech
        this.speechSynthesis.cancel();
        
        // Create utterance
        const utterance = new SpeechSynthesisUtterance(text);
        
        // Set language and voice
        utterance.lang = this.settings.language;
        
        // Find the selected voice if available
        if (this.settings.voiceURI) {
            const voice = this.voices.find(v => v.voiceURI === this.settings.voiceURI);
            if (voice) {
                utterance.voice = voice;
            }
        }
        
        // Set other properties
        utterance.pitch = this.settings.pitch;
        utterance.rate = this.settings.rate;
        utterance.volume = this.settings.volume;
        
        // Set event handlers
        utterance.onstart = () => {
            this.isSpeaking = true;
        };
        
        utterance.onend = () => {
            this.isSpeaking = false;
        };
        
        utterance.onerror = (event) => {
            console.error('Speech synthesis error:', event);
            this.isSpeaking = false;
        };
        
        // Speak the utterance
        this.speechSynthesis.speak(utterance);
    }
    
    /**
     * Clear chat history
     */
    clearChat() {
        this.messageHistory = [];
        
        if (this.messagesContainer) {
            this.messagesContainer.innerHTML = '';
        }
        
        this.addSystemMessage('聊天记录已清空');
    }
}

/**
 * Initialize AI Chat
 * @param {string} containerId - ID of the container element
 */
function initAIChat(containerId) {
    return new AIChat(containerId);
} 