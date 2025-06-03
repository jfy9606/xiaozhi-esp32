#include "vehicle.h"
#include "web/web.h"
#include "display/display.h"
#include "board.h"
#include "esp_log.h"
#include "settings.h"

#include <vector>
#include <cJSON.h>
#include <driver/gpio.h>

#define TAG "Vehicle"

// 静态函数用于处理传感器数据上报
void Vehicle::SendUltrasonicData(Web* server, iot::Thing* thing) {
    if (!server || !thing) return;
    
    // 获取超声波传感器距离数据
    float distance = thing->GetValue("distance");
    char data_str[64];
    snprintf(data_str, sizeof(data_str), "{\"distance\":%.2f}", distance);
    
    // 发送WebSocket消息给所有客户端
    server->BroadcastWebSocketMessage(data_str);
}

void Vehicle::SendServoData(Web* server, iot::Thing* thing) {
    if (!server || !thing) return;
    
    // 获取舵机角度数据
    std::vector<int> angles;
    auto values = thing->GetValues();
    for (const auto& pair : values) {
        if (pair.first.find("angle") != std::string::npos) {
            angles.push_back(static_cast<int>(pair.second));
        }
    }
    
    if (angles.empty()) return;
    
    // 构建JSON消息
    cJSON* root = cJSON_CreateObject();
    cJSON* angles_array = cJSON_CreateArray();
    
    for (size_t i = 0; i < angles.size(); i++) {
        cJSON_AddItemToArray(angles_array, cJSON_CreateNumber(angles[i]));
    }
    
    cJSON_AddItemToObject(root, "servo_angles", angles_array);
    
    // 发送WebSocket消息
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        server->BroadcastWebSocketMessage(json_str);
        free(json_str);
    }
    
    cJSON_Delete(root);
}

// 电机小车构造函数实现
Vehicle::Vehicle(Web* server, int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin)
    : vehicle_type_(VEHICLE_TYPE_MOTOR),
      running_(false),
      webserver_(server),
      ena_pin_(ena_pin),
      enb_pin_(enb_pin),
      in1_pin_(in1_pin),
      in2_pin_(in2_pin),
      in3_pin_(in3_pin),
      in4_pin_(in4_pin),
      steering_servo_pin_(-1),
      throttle_servo_pin_(-1),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(0),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(90),
      steering_angle_(90),
      throttle_position_(0),
      camera_h_servo_pin_(-1),
      camera_v_servo_pin_(-1)
{
    ESP_LOGI(TAG, "Creating vehicle with motor control");
}

// 舵机小车构造函数实现
Vehicle::Vehicle(Web* server, int steering_servo_pin, int throttle_servo_pin)
    : vehicle_type_(VEHICLE_TYPE_SERVO),
      running_(false),
      webserver_(server),
      ena_pin_(-1),
      enb_pin_(-1),
      in1_pin_(-1),
      in2_pin_(-1),
      in3_pin_(-1),
      in4_pin_(-1),
      steering_servo_pin_(steering_servo_pin),
      throttle_servo_pin_(throttle_servo_pin),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(0),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(90),
      steering_angle_(90),
      throttle_position_(0),
      camera_h_servo_pin_(-1),
      camera_v_servo_pin_(-1)
{
    ESP_LOGI(TAG, "Creating vehicle with servo control");
}

// 带云台的电机小车构造函数
Vehicle::Vehicle(Web* server, int ena_pin, int enb_pin, int in1_pin, int in2_pin, int in3_pin, int in4_pin,
        int camera_h_servo_pin, int camera_v_servo_pin)
    : vehicle_type_(VEHICLE_TYPE_MOTOR_CAMERA),
      running_(false),
      webserver_(server),
      ena_pin_(ena_pin),
      enb_pin_(enb_pin),
      in1_pin_(in1_pin),
      in2_pin_(in2_pin),
      in3_pin_(in3_pin),
      in4_pin_(in4_pin),
      steering_servo_pin_(-1),
      throttle_servo_pin_(-1),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(0),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(90),
      steering_angle_(90),
      throttle_position_(0),
      camera_h_servo_pin_(camera_h_servo_pin),
      camera_v_servo_pin_(camera_v_servo_pin)
{
    ESP_LOGI(TAG, "Creating vehicle with motor control and camera");
}

// 带云台的舵机小车构造函数
Vehicle::Vehicle(Web* server, int steering_servo_pin, int throttle_servo_pin,
        int camera_h_servo_pin, int camera_v_servo_pin)
    : vehicle_type_(VEHICLE_TYPE_SERVO_CAMERA),
      running_(false),
      webserver_(server),
      ena_pin_(-1),
      enb_pin_(-1),
      in1_pin_(-1),
      in2_pin_(-1),
      in3_pin_(-1),
      in4_pin_(-1),
      steering_servo_pin_(steering_servo_pin),
      throttle_servo_pin_(throttle_servo_pin),
      direction_x_(0),
      direction_y_(0),
      motor_speed_(0),
      distance_percent_(0),
      last_dir_x_(0),
      last_dir_y_(0),
      cached_angle_degrees_(90),
      steering_angle_(90),
      throttle_position_(0),
      camera_h_servo_pin_(camera_h_servo_pin),
      camera_v_servo_pin_(camera_v_servo_pin)
{
    ESP_LOGI(TAG, "Creating vehicle with servo control and camera");
}

Vehicle::~Vehicle() {
    Stop();
}

bool Vehicle::Start() {
    if (running_) {
        return true;
    }

    ESP_LOGI(TAG, "Starting vehicle component");
    
    // 初始化引脚和硬件
    InitGPIO();
    InitServos();
    
    // 注册Web处理器
    if (webserver_) {
        InitHandlers();
    }
    
    running_ = true;
    return true;
}

void Vehicle::Stop() {
    if (!running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping vehicle component");
    
    // 停止所有运动
    Stop(true);
    
    running_ = false;
}

bool Vehicle::IsRunning() const {
    return running_;
}

const char* Vehicle::GetName() const {
    return "Vehicle";
}

// 车辆控制方法
void Vehicle::SetControlParams(float distance, int dirX, int dirY) {
    distance_percent_ = distance;
    direction_x_ = dirX;
    direction_y_ = dirY;
    
    // 根据方向参数控制车辆运动
    if (dirX == 0 && dirY == 0) {
        // 停止
        Stop(true);
    } else if (dirY > 0) {
        // 前进，方向由dirX控制
        if (dirX < 0) {
            TurnLeft();
        } else if (dirX > 0) {
            TurnRight();
        } else {
            Forward();
        }
    } else if (dirY < 0) {
        // 后退，方向由dirX控制
        Backward();
    } else if (dirX < 0) {
        // 左转
        TurnLeft();
    } else if (dirX > 0) {
        // 右转
        TurnRight();
    }
    
    // 调整速度
    SetSpeed(DEFAULT_SPEED * distance_percent_);
}

// 电机控制方法
void Vehicle::Forward(int speed) {
    if (vehicle_type_ == VEHICLE_TYPE_MOTOR || vehicle_type_ == VEHICLE_TYPE_MOTOR_CAMERA) {
        // 电机控制
        ControlMotor(1, 0, 1, 0); // 双电机正转
        motor_speed_ = speed;
    } else {
        // 舵机控制
        if (throttle_servo_pin_ >= 0) {
            SetThrottlePosition(120); // 油门前进位置
        }
    }
}

void Vehicle::Backward(int speed) {
    if (vehicle_type_ == VEHICLE_TYPE_MOTOR || vehicle_type_ == VEHICLE_TYPE_MOTOR_CAMERA) {
        // 电机控制
        ControlMotor(0, 1, 0, 1); // 双电机反转
        motor_speed_ = speed;
    } else {
        // 舵机控制
        if (throttle_servo_pin_ >= 0) {
            SetThrottlePosition(60); // 油门后退位置
        }
    }
}

void Vehicle::TurnLeft(int speed) {
    if (vehicle_type_ == VEHICLE_TYPE_MOTOR || vehicle_type_ == VEHICLE_TYPE_MOTOR_CAMERA) {
        // 电机控制：左轮反转，右轮正转
        ControlMotor(0, 1, 1, 0);
        motor_speed_ = speed;
    } else {
        // 舵机控制：方向舵机左转
        SetSteeringAngle(45); // 向左打舵
    }
}

void Vehicle::TurnRight(int speed) {
    if (vehicle_type_ == VEHICLE_TYPE_MOTOR || vehicle_type_ == VEHICLE_TYPE_MOTOR_CAMERA) {
        // 电机控制：左轮正转，右轮反转
        ControlMotor(1, 0, 0, 1);
        motor_speed_ = speed;
    } else {
        // 舵机控制：方向舵机右转
        SetSteeringAngle(135); // 向右打舵
    }
}

void Vehicle::Stop(bool brake) {
    if (vehicle_type_ == VEHICLE_TYPE_MOTOR || vehicle_type_ == VEHICLE_TYPE_MOTOR_CAMERA) {
        // 电机控制
        if (brake) {
            // 制动模式：所有引脚高电平
            ControlMotor(1, 1, 1, 1);
        } else {
            // 滑行模式：所有引脚低电平
            ControlMotor(0, 0, 0, 0);
        }
        motor_speed_ = 0;
    } else {
        // 舵机控制
        if (throttle_servo_pin_ >= 0) {
            SetThrottlePosition(90); // 油门中立位置
        }
        SetSteeringAngle(90); // 方向回中
    }
}

void Vehicle::SetSpeed(int speed) {
    // 限制速度范围
    if (speed < MIN_SPEED) speed = MIN_SPEED;
    if (speed > MAX_SPEED) speed = MAX_SPEED;
    
    motor_speed_ = speed;
    
    // 根据车辆类型设置速度
    if (vehicle_type_ == VEHICLE_TYPE_MOTOR || vehicle_type_ == VEHICLE_TYPE_MOTOR_CAMERA) {
        // 电机控制：设置PWM
        if (ena_pin_ >= 0) {
            gpio_set_level((gpio_num_t)ena_pin_, speed);
        }
        if (enb_pin_ >= 0) {
            gpio_set_level((gpio_num_t)enb_pin_, speed);
        }
    } else {
        // 舵机控制：映射速度值到油门位置
        int throttle_pos = map(speed, MIN_SPEED, MAX_SPEED, 90, 180);
        SetThrottlePosition(throttle_pos);
    }
}

// 舵机控制方法
void Vehicle::SetSteeringAngle(int angle) {
    // 限制角度范围
    if (angle < MIN_SERVO_ANGLE) angle = MIN_SERVO_ANGLE;
    if (angle > MAX_SERVO_ANGLE) angle = MAX_SERVO_ANGLE;
    
    steering_angle_ = angle;
    
    // 控制转向舵机
    ControlSteeringServo(angle);
}

void Vehicle::SetThrottlePosition(int position) {
    // 限制位置范围
    if (position < MIN_SERVO_ANGLE) position = MIN_SERVO_ANGLE;
    if (position > MAX_SERVO_ANGLE) position = MAX_SERVO_ANGLE;
    
    throttle_position_ = position;
    
    // 控制油门舵机
    ControlThrottleServo(position);
}

void Vehicle::HandleWebSocketMessage(int client_index, const std::string& message) {
    // 解析JSON消息
    cJSON* root = cJSON_Parse(message.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse WebSocket message");
        return;
    }
    
    cJSON* cmd = cJSON_GetObjectItem(root, "command");
    if (cmd && cJSON_IsString(cmd)) {
        const char* cmd_str = cmd->valuestring;
        
        if (strcmp(cmd_str, "move") == 0) {
            // 解析移动参数
            cJSON* dist = cJSON_GetObjectItem(root, "distance");
            cJSON* dir_x = cJSON_GetObjectItem(root, "dirX");
            cJSON* dir_y = cJSON_GetObjectItem(root, "dirY");
            
            if (dist && dir_x && dir_y) {
                float distance = cJSON_IsNumber(dist) ? (float)dist->valuedouble : 0;
                int dirX = cJSON_IsNumber(dir_x) ? dir_x->valueint : 0;
                int dirY = cJSON_IsNumber(dir_y) ? dir_y->valueint : 0;
                
                // 控制车辆
                SetControlParams(distance, dirX, dirY);
                
                // 返回确认消息
                if (webserver_) {
                    char resp[128];
                    snprintf(resp, sizeof(resp), 
                            "{\"status\":\"ok\",\"command\":\"move\",\"distance\":%.2f,\"dirX\":%d,\"dirY\":%d}",
                            distance, dirX, dirY);
                    webserver_->SendWebSocketMessage(client_index, resp);
                }
            }
        } else if (strcmp(cmd_str, "stop") == 0) {
            Stop(true);
            
            // 返回确认消息
            if (webserver_) {
                webserver_->SendWebSocketMessage(client_index, "{\"status\":\"ok\",\"command\":\"stop\"}");
            }
        } else if (strcmp(cmd_str, "camera") == 0) {
            // 控制摄像头舵机
            cJSON* angle_h = cJSON_GetObjectItem(root, "h");
            cJSON* angle_v = cJSON_GetObjectItem(root, "v");
            
            if (angle_h && cJSON_IsNumber(angle_h) && camera_h_servo_pin_ >= 0) {
                int h = angle_h->valueint;
                ControlServoWithLU9685(2, h); // 假设水平舵机在通道2
            }
            
            if (angle_v && cJSON_IsNumber(angle_v) && camera_v_servo_pin_ >= 0) {
                int v = angle_v->valueint;
                ControlServoWithLU9685(3, v); // 假设垂直舵机在通道3
            }
            
            // 返回确认消息
            if (webserver_) {
                webserver_->SendWebSocketMessage(client_index, "{\"status\":\"ok\",\"command\":\"camera\"}");
            }
        }
    }
    
    cJSON_Delete(root);
}

// 私有辅助方法
void Vehicle::InitHandlers() {
    if (!webserver_) return;
    
    // 注册WebSocket处理器
    webserver_->RegisterWebSocketHandler("/ws/vehicle", [this](int client_index, const std::string& message) {
        HandleWebSocketMessage(client_index, message);
    });
    
    // 注册HTTP API处理器
    webserver_->RegisterApiHandler(HttpMethod::HTTP_GET, "/api/vehicle/status", [this](httpd_req_t* req) -> ApiResponse {
        // 构建状态JSON
        cJSON* root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "type", (int)vehicle_type_);
        cJSON_AddNumberToObject(root, "speed", motor_speed_);
        cJSON_AddNumberToObject(root, "dirX", direction_x_);
        cJSON_AddNumberToObject(root, "dirY", direction_y_);
        cJSON_AddNumberToObject(root, "steeringAngle", steering_angle_);
        cJSON_AddNumberToObject(root, "throttle", throttle_position_);
        
        char* json_str = cJSON_PrintUnformatted(root);
        std::string response(json_str);
        free(json_str);
        cJSON_Delete(root);
        
        // 创建ApiResponse对象
        ApiResponse api_resp;
        api_resp.content = response;
        api_resp.status_code = 200;
        api_resp.type = ApiResponseType::JSON;
        
        return api_resp;
    });
}

void Vehicle::InitGPIO() {
    // 初始化电机引脚
    if (vehicle_type_ == VEHICLE_TYPE_MOTOR || vehicle_type_ == VEHICLE_TYPE_MOTOR_CAMERA) {
        ESP_LOGI(TAG, "Initializing motor GPIO pins");
        
        // 配置电机控制引脚
        if (ena_pin_ >= 0) {
            gpio_config_t io_conf = {};
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = (1ULL << ena_pin_);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&io_conf);
        }
        
        if (enb_pin_ >= 0) {
            gpio_config_t io_conf = {};
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = (1ULL << enb_pin_);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&io_conf);
        }
        
        // 配置IN引脚
        gpio_config_t io_conf = {};
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 0;
        
        if (in1_pin_ >= 0) io_conf.pin_bit_mask |= (1ULL << in1_pin_);
        if (in2_pin_ >= 0) io_conf.pin_bit_mask |= (1ULL << in2_pin_);
        if (in3_pin_ >= 0) io_conf.pin_bit_mask |= (1ULL << in3_pin_);
        if (in4_pin_ >= 0) io_conf.pin_bit_mask |= (1ULL << in4_pin_);
        
        if (io_conf.pin_bit_mask != 0) {
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&io_conf);
        }
    }
}

void Vehicle::InitServos() {
    // 使用板级配置初始化PCA9685
    if (steering_servo_pin_ >= 0 || throttle_servo_pin_ >= 0 || 
        camera_h_servo_pin_ >= 0 || camera_v_servo_pin_ >= 0) {
        
        ESP_LOGI(TAG, "Initializing servos");
        
        // 初始化角度为默认值
        if (steering_servo_pin_ >= 0) {
            ControlSteeringServo(steering_angle_);
        }
        
        if (throttle_servo_pin_ >= 0) {
            ControlThrottleServo(throttle_position_);
        }
        
        if (camera_h_servo_pin_ >= 0) {
            ControlServoWithLU9685(2, 90); // 水平舵机默认中间位置
        }
        
        if (camera_v_servo_pin_ >= 0) {
            ControlServoWithLU9685(3, 90); // 垂直舵机默认中间位置
        }
    }
}

void Vehicle::ControlMotor(int in1, int in2, int in3, int in4) {
    // 设置电机控制引脚状态
    if (in1_pin_ >= 0) gpio_set_level((gpio_num_t)in1_pin_, in1);
    if (in2_pin_ >= 0) gpio_set_level((gpio_num_t)in2_pin_, in2);
    if (in3_pin_ >= 0) gpio_set_level((gpio_num_t)in3_pin_, in3);
    if (in4_pin_ >= 0) gpio_set_level((gpio_num_t)in4_pin_, in4);
}

void Vehicle::ControlSteeringServo(int angle) {
    // 控制转向舵机
    if (steering_servo_pin_ >= 0) {
        ControlServoWithLU9685(0, angle); // 假设转向舵机在通道0
    }
}

void Vehicle::ControlThrottleServo(int position) {
    // 控制油门舵机
    if (throttle_servo_pin_ >= 0) {
        ControlServoWithLU9685(1, position); // 假设油门舵机在通道1
    }
}

bool Vehicle::ControlServoWithLU9685(int channel, int angle) {
    // 使用iot::ThingManager访问PCA9685设备
    auto& thing_mgr = iot::ThingManager::GetInstance();
    auto things = thing_mgr.GetThings();
    
    // Find the servo thing
    iot::Thing* thing = nullptr;
    for (auto* t : things) {
        if (t && t->GetName() == std::string("Servo")) {
            thing = t;
            break;
        }
    }
    
    if (!thing) {
        ESP_LOGE(TAG, "Failed to get servo thing");
        return false;
    }
    
    // 设置舵机角度
    thing->SetValue("angle" + std::to_string(channel), angle);
    return true;
}

// 辅助数据任务
void Vehicle::UltrasonicDataTask(void* pvParameters) {
    // 实现超声波数据处理任务
    // ...
}

void Vehicle::ServoDataTask(void* pvParameters) {
    // 实现舵机数据处理任务
    // ...
}

// 全局初始化函数
void InitVehicleComponent(Web* server) {
    // 检查板级配置
    board_config_t* board_config = Board::GetBoardConfig();
    if (!board_config) {
        ESP_LOGE(TAG, "Failed to get board configuration");
        return;
    }
    
    // 根据板级配置决定车辆类型
    Vehicle* vehicle = nullptr;
    
    // 检查是否配置了电机引脚
    bool has_motor_pins = (board_config->ena_pin >= 0 && board_config->enb_pin >= 0 &&
                          board_config->in1_pin >= 0 && board_config->in2_pin >= 0 &&
                          board_config->in3_pin >= 0 && board_config->in4_pin >= 0);
    
    // 检查是否有舵机引脚
    bool has_servo_pins = false;
    int servo_count = board_config->servo_count > 0 ? board_config->servo_count : 0;
    int steering_servo_pin = -1;
    int throttle_servo_pin = -1;
    int camera_h_servo_pin = -1;
    int camera_v_servo_pin = -1;
    
    // 提取舵机引脚
    if (servo_count > 0 && board_config->servo_pins) {
        has_servo_pins = true;
        if (servo_count >= 1) steering_servo_pin = board_config->servo_pins[0];
        if (servo_count >= 2) throttle_servo_pin = board_config->servo_pins[1];
        if (servo_count >= 3) camera_h_servo_pin = board_config->servo_pins[2];
        if (servo_count >= 4) camera_v_servo_pin = board_config->servo_pins[3];
    }
    
    // 创建车辆对象
    if (has_motor_pins && has_servo_pins && camera_h_servo_pin >= 0 && camera_v_servo_pin >= 0) {
        // 带云台的电机小车
        ESP_LOGI(TAG, "Creating motor vehicle with camera");
        vehicle = new Vehicle(server, board_config->ena_pin, board_config->enb_pin,
                             board_config->in1_pin, board_config->in2_pin,
                             board_config->in3_pin, board_config->in4_pin,
                             camera_h_servo_pin, camera_v_servo_pin);
    } else if (has_servo_pins && steering_servo_pin >= 0 && camera_h_servo_pin >= 0 && camera_v_servo_pin >= 0) {
        // 带云台的舵机小车
        ESP_LOGI(TAG, "Creating servo vehicle with camera");
        vehicle = new Vehicle(server, steering_servo_pin, throttle_servo_pin,
                             camera_h_servo_pin, camera_v_servo_pin);
    } else if (has_motor_pins) {
        // 电机小车
        ESP_LOGI(TAG, "Creating motor vehicle");
        vehicle = new Vehicle(server, board_config->ena_pin, board_config->enb_pin,
                             board_config->in1_pin, board_config->in2_pin,
                             board_config->in3_pin, board_config->in4_pin);
    } else if (has_servo_pins && steering_servo_pin >= 0) {
        // 舵机小车
        ESP_LOGI(TAG, "Creating servo vehicle");
        vehicle = new Vehicle(server, steering_servo_pin, throttle_servo_pin);
    } else {
        ESP_LOGW(TAG, "Cannot create vehicle, insufficient pin configuration");
        return;
    }
    
    // 启动车辆组件
    if (vehicle) {
        auto& manager = ComponentManager::GetInstance();
        manager.RegisterComponent(vehicle);
        vehicle->Start();
        ESP_LOGI(TAG, "Vehicle component initialized and started");
    }
} 