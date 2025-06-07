/**
 * Xiaozhi ESP32 Vehicle Control System
 * Index page JavaScript
 */

$(document).ready(function() {
    // Check camera status
    $.get('/api/camera/status', function(data) {
        if (!data.has_camera) {
            $('#camera-feed').addClass('d-none');
            $('#camera-offline').removeClass('d-none');
        }
    }).fail(function() {
        $('#camera-feed').addClass('d-none');
        $('#camera-offline').removeClass('d-none');
    });

    // Capture button
    $('#btn-capture').click(function() {
        window.open('/api/camera/capture', '_blank');
    });

    // Toggle stream button
    let streaming = true;
    $('#btn-toggle-stream').click(function() {
        if (streaming) {
            $('#camera-feed').attr('src', '');
            $(this).html('<i class="bi bi-play-circle"></i> Resume');
            streaming = false;
        } else {
            $('#camera-feed').attr('src', '/api/camera/stream');
            $(this).html('<i class="bi bi-pause-circle"></i> Pause');
            streaming = true;
        }
    });

    // 当页面加载完成时，初始化应用
    initApp();

    // 更新电池电量显示
    function updateBatteryLevel(batteryLevel) {
        $('#battery-level').css('width', batteryLevel + '%').text(batteryLevel + '%');
        
        if (batteryLevel > 60) {
            $('#battery-level').removeClass('bg-warning bg-danger').addClass('bg-success');
        } else if (batteryLevel > 20) {
            $('#battery-level').removeClass('bg-success bg-danger').addClass('bg-warning');
        } else {
            $('#battery-level').removeClass('bg-success bg-warning').addClass('bg-danger');
        }
    }
    
    // 更新信号强度显示
    function updateSignalStrength(signalStrength) {
        let signalText = 'Unknown';
        let signalIcon = 'bi-reception-0';
        
        if (signalStrength > 80) {
            signalText = 'Excellent';
            signalIcon = 'bi-reception-4';
        } else if (signalStrength > 60) {
            signalText = 'Good';
            signalIcon = 'bi-reception-3';
        } else if (signalStrength > 40) {
            signalText = 'Fair';
            signalIcon = 'bi-reception-2';
        } else if (signalStrength > 20) {
            signalText = 'Poor';
            signalIcon = 'bi-reception-1';
        } else {
            signalText = 'Very Poor';
            signalIcon = 'bi-reception-0';
        }
        
        $('.signal-strength i').removeClass().addClass('bi ' + signalIcon);
        $('#signal-strength').text(signalText);
    }
    
    // 更新车辆状态显示
    function updateVehicleStatus(status) {
        if (typeof status === 'object') {
            // 如果传入的是对象，解析其中的数据
            if (status.battery) {
                updateBatteryLevel(status.battery);
            }
            
            if (status.signal) {
                updateSignalStrength(status.signal);
            }
            
            if (status.speed !== undefined) {
                $('#speed-value').text(status.speed + ' m/s');
            }
            
            if (status.mode) {
                $('#mode-value').text(status.mode);
            }
            
            if (status.distance) {
                $('#distance-value').text(`前方: ${status.distance.front}cm, 后方: ${status.distance.rear}cm`);
            }
            
            if (status.vehicleStatus) {
                status = status.vehicleStatus;
            } else {
                return; // 没有状态字段
            }
        }
        
        // 更新状态标签
        $('#vehicle-status').text(status);
        
        // 根据状态设置样式
        if (status === 'Ready') {
            $('#vehicle-status').removeClass('bg-warning bg-danger').addClass('bg-success');
        } else if (status === 'Moving') {
            $('#vehicle-status').removeClass('bg-success bg-danger').addClass('bg-warning');
        } else if (status === 'Error') {
            $('#vehicle-status').removeClass('bg-success bg-warning').addClass('bg-danger');
        }
        
        // Update signal strength
        if (status.signal) {
            const signalStrength = status.signal;
            let signalText = 'Unknown';
            let signalIcon = 'bi-reception-0';
            
            if (signalStrength > 80) {
                signalText = 'Excellent';
                signalIcon = 'bi-reception-4';
            } else if (signalStrength > 60) {
                signalText = 'Good';
                signalIcon = 'bi-reception-3';
            } else if (signalStrength > 40) {
                signalText = 'Fair';
                signalIcon = 'bi-reception-2';
            } else if (signalStrength > 20) {
                signalText = 'Poor';
                signalIcon = 'bi-reception-1';
            } else {
                signalText = 'Very Poor';
                signalIcon = 'bi-reception-0';
            }
            
            $('.signal-strength i').removeClass().addClass('bi ' + signalIcon);
            $('#signal-strength').text(signalText);
        }
        
        // Update speed
        if (status.speed !== undefined) {
            $('#speed-value').text(status.speed + ' m/s');
        }
        
        // Update mode
        if (status.mode) {
            $('#mode-value').text(status.mode);
        }
        
        // Update distance
        if (status.distance) {
            $('#distance-value').text(`Front: ${status.distance.front}cm, Rear: ${status.distance.rear}cm`);
        }
        
        // Update vehicle status
        if (status.vehicleStatus) {
            $('#vehicle-status').text(status.vehicleStatus);
            
            if (status.vehicleStatus === 'Ready') {
                $('#vehicle-status').removeClass('bg-warning bg-danger').addClass('bg-success');
            } else if (status.vehicleStatus === 'Moving') {
                $('#vehicle-status').removeClass('bg-success bg-danger').addClass('bg-warning');
            } else if (status.vehicleStatus === 'Error') {
                $('#vehicle-status').removeClass('bg-success bg-warning').addClass('bg-danger');
            }
        }
    }

    function addActivityLog(activity) {
        const icon = getActivityIcon(activity.type);
        const timestamp = activity.timestamp ? new Date(activity.timestamp).toLocaleTimeString() : 'Just now';
        
        const newItem = `
            <li class="list-group-item">
                <i class="bi ${icon}"></i>
                <small class="text-muted">${timestamp}</small>
                ${activity.message}
            </li>
        `;
        
        $('#activity-log').prepend(newItem);
        
        // Keep only the last 5 items
        if ($('#activity-log li').length > 5) {
            $('#activity-log li:last').remove();
        }
    }

    function getActivityIcon(type) {
        switch (type) {
            case 'success': return 'bi-check-circle text-success';
            case 'error': return 'bi-exclamation-circle text-danger';
            case 'warning': return 'bi-exclamation-triangle text-warning';
            case 'info': return 'bi-info-circle text-info';
            case 'network': return 'bi-wifi text-primary';
            case 'camera': return 'bi-camera text-info';
            default: return 'bi-circle text-secondary';
        }
    }
}); 