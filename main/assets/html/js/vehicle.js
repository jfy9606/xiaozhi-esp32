/**
 * Xiaozhi ESP32 Vehicle Control System
 * Vehicle control JavaScript
 */

$(document).ready(function () {
    // Check camera status
    $.get('/api/camera/status', function (data) {
        if (!data.has_camera) {
            $('#camera-feed').addClass('d-none');
            $('#camera-offline').removeClass('d-none');
        }
    }).fail(function () {
        $('#camera-feed').addClass('d-none');
        $('#camera-offline').removeClass('d-none');
    });

    // Initialize hardware manager integration
    initHardwareManagerIntegration();

    // Capture button
    $('#btn-capture').click(function () {
        window.open('/api/camera/capture', '_blank');
    });

    // Toggle stream button
    let streaming = true;
    $('#btn-toggle-stream').click(function () {
        if (streaming) {
            $('#camera-feed').attr('src', '');
            $(this).html('<i class="bi bi-play-circle"></i>');
            streaming = false;
        } else {
            $('#camera-feed').attr('src', '/api/camera/stream');
            $(this).html('<i class="bi bi-pause-circle"></i>');
            streaming = true;
        }
    });

    // Fullscreen button
    $('#btn-fullscreen').click(function () {
        const elem = document.getElementById('camera-feed');
        if (elem.requestFullscreen) {
            elem.requestFullscreen();
        } else if (elem.webkitRequestFullscreen) { /* Safari */
            elem.webkitRequestFullscreen();
        } else if (elem.msRequestFullscreen) { /* IE11 */
            elem.msRequestFullscreen();
        }
    });

    // Control mode selector
    $('#control-mode-joystick').change(function () {
        if ($(this).is(':checked')) {
            $('.control-panel').addClass('d-none');
            $('#joystick-control').removeClass('d-none');
        }
    });

    $('#control-mode-buttons').change(function () {
        if ($(this).is(':checked')) {
            $('.control-panel').addClass('d-none');
            $('#button-control').removeClass('d-none');
        }
    });

    $('#control-mode-auto').change(function () {
        if ($(this).is(':checked')) {
            $('.control-panel').addClass('d-none');
            $('#auto-control').removeClass('d-none');
        }
    });

    // Initialize joystick
    const joystickManager = nipplejs.create({
        zone: document.getElementById('joystick-zone'),
        mode: 'static',
        position: { left: '50%', top: '50%' },
        color: 'blue',
        size: 150
    });

    joystickManager.on('move', function (evt, data) {
        const angle = data.angle.degree;
        const force = Math.min(1, data.force / 2);
        const direction = getDirection(angle);

        $('#joystick-direction').text(direction);
        $('#joystick-force').text(Math.round(force * 100) + '%');

        // Send control command to vehicle
        sendControlCommand(direction, force);
    });

    joystickManager.on('end', function () {
        $('#joystick-direction').text('Center');
        $('#joystick-force').text('0%');

        // Send stop command
        sendControlCommand('stop', 0);
    });

    function getDirection(angle) {
        if (angle >= 337.5 || angle < 22.5) return 'Forward';
        if (angle >= 22.5 && angle < 67.5) return 'Forward Right';
        if (angle >= 67.5 && angle < 112.5) return 'Right';
        if (angle >= 112.5 && angle < 157.5) return 'Backward Right';
        if (angle >= 157.5 && angle < 202.5) return 'Backward';
        if (angle >= 202.5 && angle < 247.5) return 'Backward Left';
        if (angle >= 247.5 && angle < 292.5) return 'Left';
        if (angle >= 292.5 && angle < 337.5) return 'Forward Left';
        return 'Center';
    }

    // Button controls
    $('#btn-forward').on('mousedown touchstart', function () {
        sendControlCommand('forward', 1);
    }).on('mouseup touchend', function () {
        sendControlCommand('stop', 0);
    });

    $('#btn-backward').on('mousedown touchstart', function () {
        sendControlCommand('backward', 1);
    }).on('mouseup touchend', function () {
        sendControlCommand('stop', 0);
    });

    $('#btn-left').on('mousedown touchstart', function () {
        sendControlCommand('left', 1);
    }).on('mouseup touchend', function () {
        sendControlCommand('stop', 0);
    });

    $('#btn-right').on('mousedown touchstart', function () {
        sendControlCommand('right', 1);
    }).on('mouseup touchend', function () {
        sendControlCommand('stop', 0);
    });

    $('#btn-stop').click(function () {
        sendControlCommand('stop', 0);
    });

    // Pan-Tilt controls
    $('#pan-slider').on('input', function () {
        const value = $(this).val();
        $('#pan-value').text(value + '°');
        sendServoCommand('pan', parseInt(value));
    });

    $('#tilt-slider').on('input', function () {
        const value = $(this).val();
        $('#tilt-value').text(value + '°');
        sendServoCommand('tilt', parseInt(value));
    });

    $('#btn-center-servos').click(function () {
        $('#pan-slider').val(0);
        $('#tilt-slider').val(0);
        $('#pan-value').text('0°');
        $('#tilt-value').text('0°');
        sendServoCommand('pan', 0);
        sendServoCommand('tilt', 0);
    });

    // Auto control buttons
    $('#auto-follow-line').click(function () {
        sendAutoCommand('follow_line');
        $(this).addClass('active').siblings().removeClass('active');
    });

    $('#auto-obstacle-avoidance').click(function () {
        sendAutoCommand('obstacle_avoidance');
        $(this).addClass('active').siblings().removeClass('active');
    });

    $('#auto-face-tracking').click(function () {
        sendAutoCommand('face_tracking');
        $(this).addClass('active').siblings().removeClass('active');
    });

    $('#auto-color-tracking').click(function () {
        sendAutoCommand('color_tracking');
        $(this).addClass('active').siblings().removeClass('active');
    });

    // Toggle obstacle detection
    $('#toggle-obstacle-detection').change(function () {
        const enabled = $(this).is(':checked');
        sendCommand('set_obstacle_detection', { enabled: enabled });
    });

    // WebSocket connection for real-time updates
    let socket = new WebSocket(`ws://${window.location.host}/ws/vehicle`);

    socket.onopen = function () {
        console.log('Vehicle WebSocket connected');
        $('#status-badge').removeClass('d-none');
        $('#status-badge-disconnected').addClass('d-none');
    };

    socket.onclose = function () {
        console.log('Vehicle WebSocket disconnected');
        $('#status-badge').addClass('d-none');
        $('#status-badge-disconnected').removeClass('d-none');

        // Try to reconnect after a delay
        setTimeout(function () {
            socket = new WebSocket(`ws://${window.location.host}/ws/vehicle`);
        }, 5000);
    };

    socket.onmessage = function (event) {
        try {
            const data = JSON.parse(event.data);

            // Handle different message types
            if (data.type === 'status') {
                updateStatus(data);
            } else if (data.type === 'sensor') {
                updateSensorData(data);
            } else if (data.type === 'ultrasonic') {
                updateUltrasonicData(data);
            } else if (data.type === 'servo') {
                updateServoData(data);
            } else if (data.type === 'obstacle') {
                updateObstacleData(data);
            } else if (data.type === 'error') {
                showError(data.message);
            }
        } catch (e) {
            console.error('Error parsing WebSocket message:', e);
        }
    };

    // Send control command to vehicle
    function sendControlCommand(direction, force) {
        const command = {
            type: 'control',
            direction: direction,
            force: force
        };

        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify(command));
        }
    }

    // Send servo command
    function sendServoCommand(servo, angle) {
        const command = {
            type: 'servo',
            servo: servo,
            angle: angle
        };

        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify(command));
        }
    }

    // Send auto mode command
    function sendAutoCommand(mode) {
        const command = {
            type: 'auto',
            mode: mode
        };

        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify(command));
        }
    }

    // Send generic command
    function sendCommand(cmd, params) {
        const command = {
            type: 'command',
            cmd: cmd,
            ...params
        };

        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify(command));
        }
    }

    // Update vehicle status display
    function updateStatus(data) {
        if (data.battery !== undefined) {
            const battery = data.battery;
            $('#battery-level').css('width', battery + '%');

            if (battery > 60) {
                $('#battery-level').css('background-color', '#28a745');
            } else if (battery > 20) {
                $('#battery-level').css('background-color', '#ffc107');
            } else {
                $('#battery-level').css('background-color', '#dc3545');
            }
        }

        if (data.speed !== undefined) {
            $('#speed-value').text(data.speed);
        }

        if (data.mode !== undefined) {
            $('#mode-value').text(data.mode);
        }

        if (data.status !== undefined) {
            $('#status-value').text(data.status);
        }
    }

    // Update sensor data display
    function updateSensorData(data) {
        if (data.temperature !== undefined) {
            $('#temperature-value').text(data.temperature + '°C');
        }

        if (data.humidity !== undefined) {
            $('#humidity-value').text(data.humidity + '%');
        }

        if (data.light !== undefined) {
            $('#light-value').text(data.light);
        }

        if (data.motion !== undefined) {
            $('#motion-status').text(data.motion ? '检测到移动' : '无移动');
            $('#motion-status').toggleClass('active', data.motion);
        }
    }

    // Update ultrasonic data display
    function updateUltrasonicData(data) {
        if (data.front !== undefined) {
            $('#front-distance').text(data.front);
            $('#front-distance-icon').toggleClass('alert', data.front < 30);
        }

        if (data.rear !== undefined) {
            $('#rear-distance').text(data.rear);
            $('#rear-distance-icon').toggleClass('alert', data.rear < 30);
        }
    }

    // Update servo data display
    function updateServoData(data) {
        if (data.pan !== undefined) {
            $('#pan-slider').val(data.pan);
            $('#pan-value').text(data.pan + '°');
        }

        if (data.tilt !== undefined) {
            $('#tilt-slider').val(data.tilt);
            $('#tilt-value').text(data.tilt + '°');
        }
    }

    // Update obstacle data display
    function updateObstacleData(data) {
        $('#obstacle-status').toggleClass('active', data.obstacle);
        $('#obstacle-status').text(data.obstacle ? '障碍物检测到' : '无障碍物');
    }

    // Show error message
    function showError(message) {
        console.error('Vehicle error:', message);
        // You can add code here to show the error to the user if needed
    }

    // Initialize hardware manager integration
    function initHardwareManagerIntegration() {
        // Enhanced motor controls
        $('#left-motor-speed').on('input', function () {
            const speed = parseInt($(this).val());
            $('#left-motor-value').text(speed);

            // Send motor control command via hardware manager
            if (window.HardwareManager) {
                window.HardwareManager.controlMotor(0, speed)
                    .then(response => {
                        if (response.success) {
                            updateMotorStatus('left-motor', speed !== 0, speed > 0 ? 'Forward' : speed < 0 ? 'Backward' : 'Stopped');
                        }
                    })
                    .catch(error => {
                        console.error('Failed to control left motor:', error);
                    });
            }
        });

        $('#right-motor-speed').on('input', function () {
            const speed = parseInt($(this).val());
            $('#right-motor-value').text(speed);

            // Send motor control command via hardware manager
            if (window.HardwareManager) {
                window.HardwareManager.controlMotor(1, speed)
                    .then(response => {
                        if (response.success) {
                            updateMotorStatus('right-motor', speed !== 0, speed > 0 ? 'Forward' : speed < 0 ? 'Backward' : 'Stopped');
                        }
                    })
                    .catch(error => {
                        console.error('Failed to control right motor:', error);
                    });
            }
        });

        // Enhanced servo controls with hardware manager
        $('#pan-slider').off('input').on('input', function () {
            const value = $(this).val();
            $('#pan-value').text(value + '°');

            // Send servo control command via hardware manager
            if (window.HardwareManager) {
                const angle = parseInt(value) + 90; // Convert from -90/90 to 0/180
                window.HardwareManager.controlServo(0, angle)
                    .then(response => {
                        if (response.success) {
                            updateServoStatus('pan-servo', true, 'Active');
                        }
                    })
                    .catch(error => {
                        console.error('Failed to control pan servo:', error);
                    });
            }
        });

        $('#tilt-slider').off('input').on('input', function () {
            const value = $(this).val();
            $('#tilt-value').text(value + '°');

            // Send servo control command via hardware manager
            if (window.HardwareManager) {
                const angle = parseInt(value) + 90; // Convert from -90/90 to 0/180
                window.HardwareManager.controlServo(1, angle)
                    .then(response => {
                        if (response.success) {
                            updateServoStatus('tilt-servo', true, 'Active');
                        }
                    })
                    .catch(error => {
                        console.error('Failed to control tilt servo:', error);
                    });
            }
        });

        // Hardware status refresh button
        $('#refresh-hardware-status').click(function () {
            if (window.HardwareManager) {
                window.HardwareManager.refreshHardwareStatus();
            }
        });

        // Motor mode selection
        $('input[name="motor-mode"]').change(function () {
            const mode = $(this).attr('id').replace('motor-mode-', '');
            console.log('Motor mode changed to:', mode);
            // Here you could send a message to switch motor control mode
        });

        // Servo mode selection
        $('input[name="servo-mode"]').change(function () {
            const mode = $(this).attr('id').replace('servo-mode-', '');
            console.log('Servo mode changed to:', mode);
            // Here you could send a message to switch servo control mode
        });

        // Initial hardware status refresh
        setTimeout(function () {
            if (window.HardwareManager) {
                window.HardwareManager.refreshHardwareStatus();
                window.HardwareManager.refreshSensors();
            }
        }, 1000);
    }

    // Update motor status display
    function updateMotorStatus(motorId, active, statusText) {
        const statusIcon = $(`#${motorId}-status`);
        const statusTextElement = $(`#${motorId}-status-text`);

        if (statusIcon.length) {
            statusIcon.removeClass('text-success text-danger text-secondary');
            if (active) {
                statusIcon.addClass('text-success');
            } else {
                statusIcon.addClass('text-secondary');
            }
        }

        if (statusTextElement.length) {
            statusTextElement.text(statusText);
        }
    }

    // Update servo status display
    function updateServoStatus(servoId, active, statusText) {
        const statusIcon = $(`#${servoId}-status`);
        const statusTextElement = $(`#${servoId}-status-text`);

        if (statusIcon.length) {
            statusIcon.removeClass('text-success text-danger text-secondary');
            if (active) {
                statusIcon.addClass('text-success');
            } else {
                statusIcon.addClass('text-secondary');
            }
        }

        if (statusTextElement.length) {
            statusTextElement.text(statusText);
        }
    }
}); 