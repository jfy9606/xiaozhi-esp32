/**
 * Xiaozhi ESP32 Vehicle Control System
 * Index page JavaScript
 */

$(document).ready(function () {
    // Check camera status
    $.get('/api/camera/status', function (data) {
        // Handle the API response correctly
        if (data && data.success && data.data) {
            if (!data.data.has_camera) {
                $('#camera-feed').addClass('d-none');
                $('#camera-offline').removeClass('d-none');
            }
        } else {
            $('#camera-feed').addClass('d-none');
            $('#camera-offline').removeClass('d-none');
        }
    }).fail(function () {
        $('#camera-feed').addClass('d-none');
        $('#camera-offline').removeClass('d-none');
    });

    // Capture button
    $('#btn-capture').click(function () {
        $.get('/api/camera/capture')
            .done(function (data) {
                if (data && data.success && data.data && data.data.capture_url) {
                    window.open(data.data.capture_url, '_blank');
                } else {
                    console.error('Failed to capture image:', data);
                }
            })
            .fail(function (error) {
                console.error('Failed to capture image:', error);
            });
    });

    // Toggle stream button
    let streaming = true;
    $('#btn-toggle-stream').click(function () {
        if (streaming) {
            $('#camera-feed').attr('src', '');
            $(this).html('<i class="bi bi-play-circle"></i> Resume');
            streaming = false;
        } else {
            $.get('/api/camera/stream')
                .done(function (data) {
                    if (data && data.success && data.data && data.data.url) {
                        $('#camera-feed').attr('src', data.data.url);
                    } else {
                        console.error('Failed to get stream URL:', data);
                    }
                })
                .fail(function (error) {
                    console.error('Failed to get stream URL:', error);
                });
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

    // Hardware Manager Integration for Index Page
    function initHardwareManagerIntegration() {
        // Sensor refresh button
        $('#btn-refresh-sensors').click(function () {
            if (window.HardwareManager) {
                window.HardwareManager.refreshSensors();
                addActivityLog({
                    type: 'info',
                    message: 'Sensor data refreshed'
                });
            }
        });

        // Sensor details button
        $('#btn-sensor-details').click(function () {
            // This could open a modal or navigate to a detailed sensor page
            if (window.HardwareManager) {
                window.HardwareManager.getSensorData()
                    .then(data => {
                        if (data.success && data.data) {
                            showSensorDetailsModal(data.data);
                        }
                    })
                    .catch(error => {
                        console.error('Failed to get sensor details:', error);
                    });
            }
        });

        // Initial sensor data load
        setTimeout(function () {
            if (window.HardwareManager) {
                window.HardwareManager.refreshSensors();
            }
        }, 2000);

        // Auto-refresh sensors every 30 seconds
        setInterval(function () {
            if (window.HardwareManager) {
                window.HardwareManager.getSensorData()
                    .then(data => {
                        if (data.success && data.data && data.data.sensors) {
                            updateIndexSensorDisplay(data.data.sensors);
                        }
                    })
                    .catch(error => {
                        console.error('Auto sensor refresh failed:', error);
                    });
            }
        }, 30000);
    }

    // Update sensor display on index page
    function updateIndexSensorDisplay(sensors) {
        // Update temperature sensor
        if (sensors.temperature !== undefined) {
            updateIndexSensorValue('temperature', sensors.temperature.toFixed(1) + '°C', sensors.temperature_valid);
        }

        // Update voltage sensor
        if (sensors.voltage !== undefined) {
            updateIndexSensorValue('voltage', sensors.voltage.toFixed(2) + 'V', sensors.voltage_valid);
        }

        // Update light sensor
        if (sensors.light !== undefined) {
            updateIndexSensorValue('light', sensors.light.toString(), sensors.light_valid);
        }

        // Update motion sensor
        if (sensors.motion !== undefined) {
            const motionText = sensors.motion ? 'Motion Detected' : 'No Motion';
            updateIndexSensorValue('motion', motionText, sensors.motion_valid);
        }
    }

    // Update individual sensor value on index page
    function updateIndexSensorValue(sensorId, value, valid = true) {
        const valueElement = $(`#sensor-${sensorId}`);
        const statusElement = $(`#sensor-${sensorId}-status i`);

        if (valueElement.length) {
            valueElement.text(value);
        }

        if (statusElement.length) {
            statusElement.removeClass('text-success text-warning text-danger text-secondary');
            if (valid) {
                statusElement.addClass('text-success');
            } else {
                statusElement.addClass('text-danger');
            }
        }
    }

    // Show sensor details modal
    function showSensorDetailsModal(sensorData) {
        // Create a simple modal to show detailed sensor information
        const modalHtml = `
            <div class="modal fade" id="sensorDetailsModal" tabindex="-1">
                <div class="modal-dialog">
                    <div class="modal-content">
                        <div class="modal-header">
                            <h5 class="modal-title">Sensor Details</h5>
                            <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                        </div>
                        <div class="modal-body">
                            <div class="sensor-details-content">
                                ${formatSensorDetails(sensorData)}
                            </div>
                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Close</button>
                        </div>
                    </div>
                </div>
            </div>
        `;

        // Remove existing modal if any
        $('#sensorDetailsModal').remove();

        // Add modal to body
        $('body').append(modalHtml);

        // Show modal
        $('#sensorDetailsModal').modal('show');
    }

    // Format sensor details for display
    function formatSensorDetails(sensorData) {
        let html = '<div class="row">';

        if (sensorData.sensors) {
            Object.keys(sensorData.sensors).forEach(key => {
                const value = sensorData.sensors[key];
                if (typeof value !== 'boolean' || key.includes('_valid')) {
                    return; // Skip boolean flags
                }

                html += `
                    <div class="col-md-6 mb-3">
                        <div class="card">
                            <div class="card-body">
                                <h6 class="card-title">${key.charAt(0).toUpperCase() + key.slice(1)}</h6>
                                <p class="card-text">${value}</p>
                            </div>
                        </div>
                    </div>
                `;
            });
        }

        html += '</div>';

        if (sensorData.timestamp) {
            html += `<p class="text-muted mt-3">Last updated: ${new Date(sensorData.timestamp).toLocaleString()}</p>`;
        }

        return html;
    }

    // Initialize hardware manager integration when page loads
    $(document).ready(function () {
        setTimeout(initHardwareManagerIntegration, 1000);
    });
});