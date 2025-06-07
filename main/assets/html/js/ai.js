/**
 * Xiaozhi ESP32 Vehicle Control System
 * AI Chat Module
 */

// Configuration and state for jQuery implementation
const aiConfig = {
  ws: null,
  thinking: false,
  messages: [],
  recognition: null,
  speechSynthesis: window.speechSynthesis,
  isListening: false,
  isSpeaking: false
};

// Initialize when document is ready - legacy jQuery implementation
$(document).ready(function() {
  if (!window.aiSystem) {
    // If OOP implementation is not already initialized
    console.log('Initializing legacy AI chat system');
    initSpeechRecognition();
    initWebSocket();
    initChatUI();
    applyCustomStyles();
  }
});

// Initialize speech recognition - legacy method
function initSpeechRecognition() {
  // Check if browser supports speech recognition
  if ('SpeechRecognition' in window || 'webkitSpeechRecognition' in window) {
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    aiConfig.recognition = new SpeechRecognition();
    aiConfig.recognition.continuous = false;
    aiConfig.recognition.interimResults = true;
    aiConfig.recognition.lang = 'en-US';
    
    aiConfig.recognition.onstart = function() {
      aiConfig.isListening = true;
      $('#voice-status-indicator').addClass('active');
      $('#voice-status-text').text('Listening...');
      $('#btn-mic').addClass('btn-danger').removeClass('btn-secondary');
      $('#btn-mic i').removeClass('bi-mic').addClass('bi-mic-fill');
    };
    
    aiConfig.recognition.onresult = function(event) {
      const transcript = Array.from(event.results)
        .map(result => result[0])
        .map(result => result.transcript)
        .join('');
      
      $('#transcript-text').text(transcript);
      $('#voice-transcript').removeClass('d-none');
      
      if (event.results[0].isFinal) {
        $('#chat-input').val(transcript);
      }
    };
    
    aiConfig.recognition.onend = function() {
      aiConfig.isListening = false;
      $('#voice-status-indicator').removeClass('active');
      $('#voice-status-text').text('Voice recognition ready');
      $('#btn-mic').removeClass('btn-danger').addClass('btn-secondary');
      $('#btn-mic i').removeClass('bi-mic-fill').addClass('bi-mic');
      
      // If we have a transcript, send the message
      if ($('#chat-input').val().trim() !== '') {
        $('#btn-send').click();
      }
      
      setTimeout(function() {
        $('#voice-transcript').addClass('d-none');
      }, 5000);
    };
    
    aiConfig.recognition.onerror = function(event) {
      aiConfig.isListening = false;
      $('#voice-status-indicator').removeClass('active').addClass('error');
      $('#voice-status-text').text('Error: ' + event.error);
      $('#btn-mic').removeClass('btn-danger').addClass('btn-secondary');
      $('#btn-mic i').removeClass('bi-mic-fill').addClass('bi-mic');
      
      setTimeout(function() {
        $('#voice-status-indicator').removeClass('error');
        $('#voice-status-text').text('Voice recognition ready');
      }, 3000);
    };
  } else {
    $('#btn-mic').prop('disabled', true);
    $('#voice-status-text').text('Voice recognition not supported in this browser');
    $('#toggle-voice-commands').prop('checked', false).prop('disabled', true);
  }
}

// Initialize WebSocket connection - legacy method
function initWebSocket() {
  if (aiConfig.ws) {
    aiConfig.ws.close();
  }
  
  const wsUrl = `ws://${window.location.host}/ws/ai`;
  aiConfig.ws = new WebSocket(wsUrl);
  
  aiConfig.ws.onopen = function() {
    console.log('WebSocket connected');
    $('#status-badge').removeClass('d-none');
    $('#status-badge-disconnected').addClass('d-none');
  };
  
  aiConfig.ws.onclose = function() {
    console.log('WebSocket disconnected');
    $('#status-badge').addClass('d-none');
    $('#status-badge-disconnected').removeClass('d-none');
    
    // Try to reconnect after a delay
    setTimeout(initWebSocket, 5000);
  };
  
  aiConfig.ws.onmessage = function(event) {
    try {
      const data = JSON.parse(event.data);
      
      if (data.type === 'ai_response') {
        addMessage('ai', data.text);
        
        // Speak the response if text-to-speech is enabled
        if ($('#toggle-speech').is(':checked')) {
          speak(data.text);
        }
      } else {
        handleWebSocketMessage(data);
      }
    } catch (e) {
      console.error("Error parsing WebSocket message:", e);
    }
  };
  
  aiConfig.ws.onerror = function(error) {
    console.error('WebSocket error:', error);
  };
}

// Send message through WebSocket - legacy method
function sendWebSocketMessage(message) {
  if (aiConfig.ws && aiConfig.ws.readyState === WebSocket.OPEN) {
    aiConfig.ws.send(JSON.stringify(message));
  } else {
    console.warn("WebSocket not connected");
  }
}

// Handle incoming WebSocket messages - legacy method
function handleWebSocketMessage(message) {
  console.log("Received message:", message);
  
  if (message.type === 'chat_message') {
    showChatMessage(message.data);
  } else if (message.type === 'chat_history') {
    loadChatHistory(message.data);
  } else if (message.type === 'thinking_start') {
    showThinking(true);
  } else if (message.type === 'thinking_end') {
    showThinking(false);
  }
}

// Initialize chat UI components - legacy method
function initChatUI() {
  // Send button click
  $('#btn-send').click(function() {
    const text = $('#chat-input').val();
    sendMessage(text);
  });
  
  // Enter key press in input
  $('#chat-input').keypress(function(e) {
    if (e.which === 13) {
      $('#btn-send').click();
    }
  });
  
  // Microphone button click
  $('#btn-mic').click(function() {
    if (!aiConfig.recognition) return;
    
    if (aiConfig.isListening) {
      aiConfig.recognition.stop();
    } else {
      aiConfig.recognition.start();
    }
  });
  
  // Clear chat button
  $('#btn-clear-chat').click(function() {
    $('#chat-container').empty();
    addMessage('ai', 'Chat cleared. How can I help you?');
  });
  
  // Speech rate slider
  $('#speech-rate').on('input', function() {
    const value = $(this).val();
    $('#speech-rate-value').text(value);
  });
  
  // Speech pitch slider
  $('#speech-pitch').on('input', function() {
    const value = $(this).val();
    $('#speech-pitch-value').text(value);
  });
  
  // Quick command buttons
  $('.quick-command').click(function() {
    const command = $(this).data('command');
    $('#chat-input').val(command);
    $('#btn-send').click();
  });
  
  // Voice commands toggle
  $('#toggle-voice-commands').change(function() {
    if ($(this).is(':checked')) {
      $('#btn-mic').prop('disabled', false);
      $('#voice-status-text').text('Voice recognition ready');
    } else {
      $('#btn-mic').prop('disabled', true);
      $('#voice-status-text').text('Voice commands disabled');
      if (aiConfig.isListening && aiConfig.recognition) {
        aiConfig.recognition.stop();
      }
    }
  });
}

// Send message function - legacy method
function sendMessage(text) {
  if (text.trim() === '') return;
  
  // Add message to chat
  addMessage('user', text);
  
  // Clear input
  $('#chat-input').val('');
  
  // Send message to server
  if (aiConfig.ws && aiConfig.ws.readyState === WebSocket.OPEN) {
    const message = {
      cmd: 'chat',
      text: text,
      personality: $('#ai-personality').val()
    };
    sendWebSocketMessage(message);
  }
}

// Add message to chat - legacy method
function addMessage(type, text) {
  const time = new Date().toLocaleTimeString();
  const messageHtml = `
    <div class="chat-message ${type}">
      <div class="message-content">
        ${text}
      </div>
      <div class="message-time">
        <small class="text-muted">${time}</small>
      </div>
    </div>
  `;
  
  $('#chat-container').append(messageHtml);
  
  // Scroll to bottom
  const chatContainer = document.getElementById('chat-container');
  chatContainer.scrollTop = chatContainer.scrollHeight;
  
  // Add to messages array
  aiConfig.messages.push({
    sender: type,
    text: text,
    timestamp: new Date().toISOString()
  });
}

// Text to speech function - legacy method
function speak(text) {
  if (aiConfig.isSpeaking) {
    aiConfig.speechSynthesis.cancel();
  }
  
  if ('speechSynthesis' in window) {
    const utterance = new SpeechSynthesisUtterance(text);
    utterance.rate = parseFloat($('#speech-rate').val());
    utterance.pitch = parseFloat($('#speech-pitch').val());
    utterance.lang = 'en-US';
    
    utterance.onstart = function() {
      aiConfig.isSpeaking = true;
    };
    
    utterance.onend = function() {
      aiConfig.isSpeaking = false;
    };
    
    aiConfig.speechSynthesis.speak(utterance);
  }
}

// Show chat message in UI - legacy method
function showChatMessage(message) {
  const messagesContainer = document.getElementById('chat-messages');
  if (!messagesContainer) return;
  
  // Add to messages array if not already done
  if (!aiConfig.messages.includes(message)) {
    aiConfig.messages.push(message);
  }
  
  // Create message element
  const messageElement = document.createElement('div');
  messageElement.className = `message message-${message.sender}`;
  
  const contentElement = document.createElement('div');
  contentElement.className = 'message-content';
  contentElement.textContent = message.text;
  
  const timeElement = document.createElement('div');
  timeElement.className = 'message-time';
  timeElement.textContent = formatMessageTime(message.timestamp);
  
  messageElement.appendChild(contentElement);
  contentElement.appendChild(timeElement);
  
  messagesContainer.appendChild(messageElement);
  
  // Scroll to bottom
  messagesContainer.scrollTop = messagesContainer.scrollHeight;
  
  // Hide thinking indicator if we received an AI message
  if (message.sender === 'ai') {
    showThinking(false);
  }
}

// Load chat history - legacy method
function loadChatHistory(history) {
  const messagesContainer = document.getElementById('chat-messages');
  if (!messagesContainer) return;
  
  // Clear container
  messagesContainer.innerHTML = '';
  
  // Reset state
  aiConfig.messages = [];
  
  // Add each message
  if (Array.isArray(history)) {
    history.forEach(message => {
      showChatMessage(message);
    });
  }
}

// Show/hide thinking indicator - legacy method
function showThinking(isThinking) {
  const messagesContainer = document.getElementById('chat-messages');
  if (!messagesContainer) return;
  
  // Update state
  aiConfig.thinking = isThinking;
  
  // Remove existing thinking indicators
  const existingIndicator = document.getElementById('ai-thinking-indicator');
  if (existingIndicator) {
    existingIndicator.remove();
  }
  
  // Add new thinking indicator if needed
  if (isThinking) {
    const thinkingElement = document.createElement('div');
    thinkingElement.className = 'ai-thinking';
    thinkingElement.id = 'ai-thinking-indicator';
    
    thinkingElement.innerHTML = `
      AI is thinking<span class="dots">
        <span class="dot"></span>
        <span class="dot"></span>
        <span class="dot"></span>
      </span>
    `;
    
    messagesContainer.appendChild(thinkingElement);
    messagesContainer.scrollTop = messagesContainer.scrollHeight;
  }
}

// Format timestamp for display - legacy method
function formatMessageTime(timestamp) {
  if (!timestamp) return '';
  
  const date = new Date(timestamp);
  const hours = date.getHours().toString().padStart(2, '0');
  const minutes = date.getMinutes().toString().padStart(2, '0');
  
  return `${hours}:${minutes}`;
}

// Apply custom styles programmatically - legacy method
function applyCustomStyles() {
  // Add stylesheet for voice status styles that were previously inline
  $('<style>').text(`
    .main-content {
      display: flex;
      flex-direction: column;
    }
    
    @media (min-width: 768px) {
      .main-content {
        flex-direction: row;
      }
      
      .chat-section {
        flex: 2;
        margin-right: 15px;
      }
      
      .controls-section {
        flex: 1;
      }
    }
    
    .chat-container {
      height: 400px;
      overflow-y: auto;
    }
    
    .voice-status-indicator {
      display: flex;
      align-items: center;
    }
    
    .voice-status-dot {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      background-color: #ccc;
      margin-right: 8px;
      transition: background-color 0.3s;
    }
    
    .voice-status-indicator.active .voice-status-dot {
      background-color: #dc3545;
      animation: pulse 1.5s infinite;
    }
    
    .voice-status-indicator.error .voice-status-dot {
      background-color: #dc3545;
    }
    
    @keyframes pulse {
      0% { opacity: 1; }
      50% { opacity: 0.5; }
      100% { opacity: 1; }
    }
    
    .voice-transcript {
      padding: 8px;
      background-color: #f8f9fa;
      border-radius: 4px;
      margin-top: 8px;
    }
    
    .chat-input-container {
      display: flex;
      gap: 8px;
      margin-top: 15px;
    }
    
    .chat-input-container input {
      flex: 1;
    }
    
    .list-group-item {
      cursor: pointer;
      transition: background-color 0.2s;
    }
    
    .list-group-item:hover {
      background-color: #f8f9fa;
    }
  `).appendTo('head');
}

// OOP Implementation of AI Chat system
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
        if (this.container) {
            this.init();
        }
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
        
        console.log('AI chat system initialized (OOP)');
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
 * Initialize AI Chat - OOP method
 * @param {string} containerId - ID of the container element
 */
function initAIChat(containerId) {
    return new AIChat(containerId);
} 