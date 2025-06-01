#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "sdkconfig.h"

// Include board-specific configurations
#include "board_config.h"

// Include multiplexer drivers
#ifdef CONFIG_ENABLE_PCA9548A
#include "include/pca9548a.h"
#endif

#ifdef CONFIG_ENABLE_HW178
#include "include/hw178.h"
#endif

// 引入PCF8575头文件
#include "include/pcf8575.h"

// 引入多路复用器头文件
#include "include/multiplexer.h"

static const char *TAG = "multiplexer";

// 前向声明
#ifdef CONFIG_ENABLE_PCA9548A
esp_err_t pca9548a_init(i2c_master_bus_handle_t external_bus_handle);
i2c_master_bus_handle_t lvgl_port_get_i2c_bus_handle(void);
#endif

// Multiplexer handles
#ifdef CONFIG_ENABLE_PCA9548A
static pca9548a_handle_t pca9548a_handle = NULL;
// 修改为全局变量，移除 static 关键字，使之对其他文件可见
i2c_master_bus_handle_t i2c_bus_handle = NULL;
i2c_master_dev_handle_t i2c_dev_handle = NULL;
// 默认I2C端口，可通过函数参数修改
static int default_i2c_port = DEFAULT_MULTIPLEXER_I2C_PORT;
#endif

#ifdef CONFIG_ENABLE_HW178
static hw178_handle_t hw178_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

// 添加gpio_to_adc_channel函数的前向声明
static int gpio_to_adc_channel(gpio_num_t gpio_num);
#endif

// 全局状态标志
static bool g_pca9548a_initialized = false;
static bool g_hw178_initialized = false;
static bool g_multiplexer_initialized = false;

// 删除i2c_master_init函数，不再自行初始化I2C总线

#ifdef CONFIG_ENABLE_PCA9548A
// 通过已存在的总线句柄配置设备
static esp_err_t pca9548a_config_device_with_handles(i2c_master_bus_handle_t bus_handle)
{
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 保存传入的总线句柄
    i2c_bus_handle = bus_handle;

    // 配置PCA9548A设备 - 与OLED显示屏保持一致的I2C配置
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9548A_I2C_ADDR,
        .scl_speed_hz = 400 * 1000, // 设置为400 kHz，与OLED显示屏一致
    };
    
    ESP_LOGI(TAG, "Adding PCA9548A device to shared I2C bus at address 0x%02X with SCL speed %" PRIu32 " Hz", 
             PCA9548A_I2C_ADDR, dev_cfg.scl_speed_hz);
    
    // 创建设备句柄
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        // 如果添加设备失败，我们不删除总线句柄，因为它是外部提供的
            i2c_bus_handle = NULL;
        return ret;
    }

    return ESP_OK;
}

// 修改PCA9548A初始化函数，不再创建I2C总线
esp_err_t pca9548a_init(i2c_master_bus_handle_t external_bus_handle)
{
    ESP_LOGI(TAG, "Initializing PCA9548A multiplexer with external bus handle");

    if (external_bus_handle == NULL) {
        ESP_LOGI(TAG, "External I2C bus handle is NULL, skipping PCA9548A initialization");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

        // 配置PCA9548A I2C设备
    ret = pca9548a_config_device_with_handles(external_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to configure PCA9548A device: %s (this is normal if device is not connected)", esp_err_to_name(ret));
        return ret;
        }

    // 只有在成功获取到设备句柄的情况下才继续
    if (i2c_dev_handle == NULL) {
        ESP_LOGI(TAG, "No I2C device handle available, skipping PCA9548A initialization");
        return ESP_ERR_INVALID_STATE;
    }

    // 创建PCA9548A设备，使用更短的超时时间
    pca9548a_config_t pca_config = {
        .i2c_port = default_i2c_port,
        .i2c_addr = PCA9548A_I2C_ADDR,
        .i2c_timeout_ms = 20,  // 进一步降低超时时间到20ms，减少等待时间
        .reset_pin = PCA9548A_RESET_PIN,
    };
    
    pca9548a_handle = pca9548a_create(&pca_config);
    if (pca9548a_handle == NULL) {
        ESP_LOGI(TAG, "Failed to create PCA9548A device, device may not be connected");
        // 清理已创建的资源，但保持显示功能正常
        if (i2c_dev_handle != NULL) {
            i2c_master_bus_rm_device(i2c_dev_handle);
            i2c_dev_handle = NULL;
        }
        i2c_bus_handle = NULL; // 不删除总线，只清空句柄
        return ESP_OK; // 返回OK允许程序继续运行
    }

    // 进行快速检测，如果设备不响应，则记录信息并退出
    uint8_t test_channel = 0;
    ret = pca9548a_get_selected_channels(pca9548a_handle, &test_channel);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "PCA9548A communication test failed: %s (device likely not present)", esp_err_to_name(ret));
        // 清理已创建的资源
        pca9548a_delete(pca9548a_handle);
        pca9548a_handle = NULL;
        
        if (i2c_dev_handle != NULL) {
            i2c_master_bus_rm_device(i2c_dev_handle);
            i2c_dev_handle = NULL;
        }
        i2c_bus_handle = NULL; // 不删除总线，只清空句柄
        return ESP_OK; // 返回OK允许程序继续运行
    }

    // 简化重置过程，不进行长时间等待
    ESP_LOGI(TAG, "PCA9548A device detected, performing quick reset");
    if (pca9548a_reset(pca9548a_handle) == ESP_OK) {
        ESP_LOGI(TAG, "PCA9548A reset successful");
    } else {
        ESP_LOGI(TAG, "PCA9548A reset failed, continuing anyway");
    }

    ESP_LOGI(TAG, "PCA9548A initialized successfully");
    return ESP_OK;
}
#endif

#ifdef CONFIG_ENABLE_HW178
// Initialize ADC
static adc_oneshot_unit_handle_t adc_init(void)
{
    adc_oneshot_unit_handle_t adc_handle = NULL;
    
    // 尝试从配置文件获取优先ADC单元
#ifdef PREFER_ADC_UNIT
    // 优先使用配置文件中指定的ADC单元
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = PREFER_ADC_UNIT,
    };
    
    ESP_LOGI(TAG, "尝试初始化首选ADC单元: %d", PREFER_ADC_UNIT);
#else
    // 如果没有配置，则默认尝试ADC2
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_2,
    };
    
    ESP_LOGI(TAG, "尝试初始化ADC2单元");
#endif
    
    int retry_count = 0;
    const int max_retries = 3; // 尝试多次获取ADC资源
    esp_err_t ret = ESP_FAIL;
    
    while (retry_count < max_retries) {
        ret = adc_oneshot_new_unit(&init_config, &adc_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "成功初始化ADC单元 %d", init_config.unit_id);
            break;
        }
        
        ESP_LOGI(TAG, "ADC单元 %d 初始化失败: %s (尝试 %d/%d)", 
                 init_config.unit_id, esp_err_to_name(ret), retry_count+1, max_retries);
        
        // 等待短暂时间后重试
        vTaskDelay(pdMS_TO_TICKS(10));
        retry_count++;
    }
    
    // 如果首选ADC单元初始化失败，尝试备用单元
    if (ret != ESP_OK) {
#ifdef FALLBACK_ADC_UNIT
        ESP_LOGI(TAG, "尝试使用备用ADC单元: %d", FALLBACK_ADC_UNIT);
        init_config.unit_id = FALLBACK_ADC_UNIT;
        
        // 重置重试计数
        retry_count = 0;
        
        while (retry_count < max_retries) {
            ret = adc_oneshot_new_unit(&init_config, &adc_handle);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "成功初始化备用ADC单元 %d", init_config.unit_id);
                break;
    }
    
            ESP_LOGI(TAG, "备用ADC单元 %d 初始化失败: %s (尝试 %d/%d)", 
                     init_config.unit_id, esp_err_to_name(ret), retry_count+1, max_retries);
            
            // 等待短暂时间后重试
            vTaskDelay(pdMS_TO_TICKS(10));
            retry_count++;
        }
#endif
    }
    
    // 如果仍然失败，记录错误并返回NULL
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "所有ADC单元初始化失败");
        return NULL;
    }
    
    // 使用GPIO14对应的ADC通道
    int adc_channel = gpio_to_adc_channel(HW178_SIG_PIN);
    
    // Check if the channel is valid
    if (adc_channel < 0) {
        ESP_LOGW(TAG, "HW178_SIG_PIN (GPIO %d) 没有对应的有效ADC通道", HW178_SIG_PIN);
        // 释放已分配的资源
        adc_oneshot_del_unit(adc_handle);
        return NULL;
    }
    
    // 配置ADC通道
    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    
    ret = adc_oneshot_config_channel(adc_handle, static_cast<adc_channel_t>(adc_channel), &adc_chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC通道配置失败: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "ADC初始化成功，使用单元ID=%d, 通道=%d", init_config.unit_id, adc_channel);
    return adc_handle;
}

// 获取GPIO对应的ADC通道号
static int gpio_to_adc_channel(gpio_num_t gpio_num)
{
    // GPIO到ADC通道的映射关系，根据ESP32的硬件规格
    // 这里需要根据具体的ESP32型号来定义正确的映射关系
#if CONFIG_IDF_TARGET_ESP32
    // ESP32的ADC1映射关系
    switch (gpio_num) {
        case GPIO_NUM_36: return 0;  // ADC1_CH0
        case GPIO_NUM_37: return 1;  // ADC1_CH1
        case GPIO_NUM_38: return 2;  // ADC1_CH2
        case GPIO_NUM_39: return 3;  // ADC1_CH3
        case GPIO_NUM_32: return 4;  // ADC1_CH4
        case GPIO_NUM_33: return 5;  // ADC1_CH5
        case GPIO_NUM_34: return 6;  // ADC1_CH6
        case GPIO_NUM_35: return 7;  // ADC1_CH7
        default: return -1;
    }
#elif CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3的ADC1映射关系
    if (gpio_num >= GPIO_NUM_1 && gpio_num <= GPIO_NUM_10) {
        return gpio_num - 1;  // ADC1: GPIO1-10 -> 通道0-9
    }
    // ESP32-S3的ADC2映射关系
    else if (gpio_num == GPIO_NUM_11) return 0;  // ADC2_CH0
    else if (gpio_num == GPIO_NUM_12) return 1;  // ADC2_CH1
    else if (gpio_num == GPIO_NUM_13) return 2;  // ADC2_CH2
    else if (gpio_num == GPIO_NUM_14) return 3;  // ADC2_CH3
    else if (gpio_num == GPIO_NUM_15) return 4;  // ADC2_CH4
    else if (gpio_num == GPIO_NUM_16) return 5;  // ADC2_CH5
    else if (gpio_num == GPIO_NUM_17) return 6;  // ADC2_CH6
    else if (gpio_num == GPIO_NUM_18) return 7;  // ADC2_CH7
    else if (gpio_num == GPIO_NUM_19) return 8;  // ADC2_CH8
    else if (gpio_num == GPIO_NUM_20) return 9;  // ADC2_CH9
    else return -1;
#else
    // 对于其他ESP32系列，返回默认通道
    return -1;  // 我们不再使用CONFIG_HW178_ADC_CHANNEL，直接返回-1表示无效
#endif
}
#endif

#ifdef CONFIG_ENABLE_HW178
// Initialize HW178
esp_err_t hw178_init(void)
{
    ESP_LOGI(TAG, "正在初始化HW-178多路复用器");

    // 验证SIG引脚是否为有效的ADC引脚
    int adc_channel = gpio_to_adc_channel(HW178_SIG_PIN);
    if (adc_channel < 0) {
        ESP_LOGW(TAG, "HW178_SIG_PIN (GPIO %d) 不是有效的ADC引脚", HW178_SIG_PIN);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 记录SIG引脚对应的ADC通道
    ESP_LOGI(TAG, "HW-178 SIG引脚(GPIO %d)映射到ADC通道 %d", HW178_SIG_PIN, adc_channel);

    // Initialize ADC
    adc_handle = adc_init();
    if (adc_handle == NULL) {
        ESP_LOGW(TAG, "无法初始化ADC，HW-178将在无ADC功能的情况下运行");
        // 继续初始化GPIO部分，以便支持通道选择功能
    }

    // Create HW-178 device (无使能引脚)
    hw178_config_t hw178_config = {
        .s0_pin = HW178_S0_PIN,
        .s1_pin = HW178_S1_PIN,
        .s2_pin = HW178_S2_PIN,
        .s3_pin = HW178_S3_PIN,
        .sig_pin = HW178_SIG_PIN,
    };
    
    hw178_handle = hw178_create(&hw178_config);
    if (hw178_handle == NULL) {
        ESP_LOGW(TAG, "无法创建HW-178设备，GPIO配置可能不正确");
        // 如果ADC初始化成功但GPIO初始化失败，需要释放ADC资源
        if (adc_handle != NULL) {
            adc_oneshot_del_unit(adc_handle);
            adc_handle = NULL;
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    // 初始化成功
    ESP_LOGI(TAG, "HW-178初始化成功%s", 
             adc_handle == NULL ? "，但ADC不可用" : "");
    
    // 如果ADC可用，初始化完全成功
    return adc_handle != NULL ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// Function to read analog value from a specific channel
esp_err_t hw178_read_channel(hw178_channel_t channel, int *value)
{
    if (!g_hw178_initialized || hw178_handle == NULL) {
        ESP_LOGW(TAG, "HW-178未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if ADC is initialized
    if (adc_handle == NULL) {
        ESP_LOGW(TAG, "ADC未初始化，无法读取模拟值");
        *value = -1; // 设置一个无效值
        return ESP_ERR_NOT_FOUND;
    }
    
    // Select channel
    esp_err_t ret = hw178_select_channel(hw178_handle, channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "无法选择HW-178通道: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for channel switching to stabilize
    vTaskDelay(pdMS_TO_TICKS(2));
    
    // 根据SIG引脚获取对应的ADC通道
    int adc_channel = gpio_to_adc_channel(HW178_SIG_PIN);
    if (adc_channel < 0) {
        ESP_LOGW(TAG, "SIG引脚(GPIO %d)没有有效的ADC通道", HW178_SIG_PIN);
        *value = -1;
        return ESP_ERR_INVALID_ARG;
    }
    
    // 显式记录使用的ADC通道
    ESP_LOGD(TAG, "从GPIO %d使用ADC通道 %d读取模拟值", HW178_SIG_PIN, adc_channel);
    
    // Read ADC value
    ret = adc_oneshot_read(adc_handle, static_cast<adc_channel_t>(adc_channel), value);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC读取失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "HW-178通道 C%d, ADC值: %d", channel, *value);
    return ESP_OK;
}

// Function to check if HW-178 is initialized
bool hw178_is_initialized(void)
{
    return g_hw178_initialized;
}

// Function to get HW-178 handle for direct access
hw178_handle_t hw178_get_handle(void)
{
    if (!g_hw178_initialized) {
        ESP_LOGW(TAG, "HW-178 handle requested but not initialized");
    }
    return hw178_handle;
}

// 直接操作HW-178选择通道，无需读取ADC
esp_err_t hw178_set_channel(hw178_channel_t channel)
{
    if (!g_hw178_initialized || hw178_handle == NULL) {
        ESP_LOGE(TAG, "HW-178 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Select channel
    esp_err_t ret = hw178_select_channel(hw178_handle, channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 添加短暂延时以确保通道切换完成
    vTaskDelay(pdMS_TO_TICKS(2));
    
    return ESP_OK;
}

// 获取HW-178 ADC单元句柄
adc_oneshot_unit_handle_t hw178_get_adc_handle(void)
{
    if (!g_hw178_initialized) {
        ESP_LOGW(TAG, "HW-178 ADC handle requested but HW-178 not initialized");
    }
    return adc_handle;
}
#endif

// Initialize multiplexer components with provided bus
esp_err_t multiplexer_init_with_bus(i2c_master_bus_handle_t external_bus_handle)
{
    esp_err_t ret = ESP_OK;
    bool any_device_initialized = false;

    if (g_multiplexer_initialized) {
        ESP_LOGI(TAG, "多路复用器已初始化，正在重新初始化...");
        multiplexer_deinit();
    }

    if (external_bus_handle == NULL) {
        ESP_LOGI(TAG, "外部总线句柄为NULL，跳过多路复用器初始化");
        return ESP_OK; // 返回OK让程序继续运行
    }

    ESP_LOGI(TAG, "正在使用外部I2C总线初始化多路复用器");

#ifdef CONFIG_ENABLE_PCA9548A
    ret = pca9548a_init(external_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "PCA9548A初始化已跳过，继续运行但不使用PCA9548A");
    } else {
        g_pca9548a_initialized = true;
        any_device_initialized = true;
        ESP_LOGI(TAG, "PCA9548A使用共享I2C总线初始化成功");
    }
#endif

#ifdef CONFIG_ENABLE_HW178
    // HW178不依赖I2C总线，正常初始化
    ret = hw178_init();
    if (ret != ESP_OK) {
        // 区分不同的错误类型
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "HW-178初始化部分成功，但ADC不可用");
            g_hw178_initialized = true;  // GPIO部分已初始化
            any_device_initialized = true;
        } else {
            ESP_LOGW(TAG, "HW-178初始化失败: %s", esp_err_to_name(ret));
        }
    } else {
        g_hw178_initialized = true;
        any_device_initialized = true;
        ESP_LOGI(TAG, "HW-178初始化成功");
    }
#endif

    g_multiplexer_initialized = any_device_initialized;
    ESP_LOGI(TAG, "多路复用器初始化完成。状态: PCA9548A=%s, HW178=%s", 
             g_pca9548a_initialized ? "OK" : "SKIP",
             g_hw178_initialized ? "OK" : "SKIP");
             
    return ESP_OK; // 总是返回成功，让程序继续运行
}

// Initialize multiplexer components
esp_err_t multiplexer_init(void)
{
    // 首先尝试直接获取显示屏I2C总线句柄
#ifdef CONFIG_ENABLE_PCA9548A
    i2c_master_bus_handle_t display_i2c_handle = lvgl_port_get_i2c_bus_handle();
    if (display_i2c_handle != NULL) {
        ESP_LOGI(TAG, "Found display I2C bus handle, using it for initialization");
        return multiplexer_init_with_bus(display_i2c_handle);
    }
#endif
    
    // 如果找不到显示屏I2C总线句柄，回退到使用默认端口方式
    return multiplexer_init_with_i2c_port(default_i2c_port);
}

// Initialize multiplexer components with specific I2C port
esp_err_t multiplexer_init_with_i2c_port(int i2c_port)
{
    esp_err_t ret = ESP_OK;
    bool any_device_initialized = false;

    if (g_multiplexer_initialized) {
        ESP_LOGI(TAG, "Multiplexers already initialized, reinitializing...");
        multiplexer_deinit();
    }

    ESP_LOGI(TAG, "Initializing multiplexers with I2C port %d", i2c_port);
    
    // 尝试查找并使用已存在的显示屏I2C总线
    // 这里我们尝试获取ESP-IDF内部注册的I2C总线
    // 注意：这是一个实验性方法，依赖于ESP-IDF内部实现，可能在未来版本中变化
#ifdef CONFIG_ENABLE_PCA9548A
    // 尝试查找显示屏使用的I2C总线
    i2c_master_bus_handle_t display_i2c_handle = NULL;
    
    // 使用i2c_get_bus_handle函数获取显示屏I2C总线句柄
    i2c_master_bus_handle_t lvgl_port_get_i2c_bus_handle(void);
    display_i2c_handle = lvgl_port_get_i2c_bus_handle();
    
    if (display_i2c_handle != NULL) {
        ESP_LOGI(TAG, "Found existing I2C bus handle from display, using it for multiplexer");
        ret = pca9548a_init(display_i2c_handle);
    if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize PCA9548A with display I2C bus: %s", esp_err_to_name(ret));
            // 继续执行，不要中断整个初始化过程
        } else {
            g_pca9548a_initialized = true;
            any_device_initialized = true;
            ESP_LOGI(TAG, "PCA9548A initialized successfully with display's I2C bus");
        }
    } else {
        ESP_LOGW(TAG, "Could not find display's I2C bus handle");
        ESP_LOGW(TAG, "Direct I2C port initialization no longer supported due to display conflicts");
        ESP_LOGW(TAG, "Use multiplexer_init_with_bus() with display's I2C bus handle");
    }
#endif

#ifdef CONFIG_ENABLE_HW178
    ret = hw178_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize HW-178: %s", esp_err_to_name(ret));
        // 这不影响PCA9548A的功能，继续执行
            } else {
        g_hw178_initialized = true;
        any_device_initialized = true;
        ESP_LOGI(TAG, "HW-178 initialized successfully");
    }
#endif

    g_multiplexer_initialized = any_device_initialized;
    ESP_LOGI(TAG, "Multiplexer initialization completed. Status: PCA9548A=%s, HW178=%s", 
             g_pca9548a_initialized ? "OK" : "FAIL",
             g_hw178_initialized ? "OK" : "FAIL");

    return ESP_OK; // 返回OK让程序继续运行
}

// Deinitialize multiplexer components
void multiplexer_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing multiplexer components");
    
#ifdef CONFIG_ENABLE_PCA9548A
    if (pca9548a_handle != NULL) {
        // 在删除设备前禁用所有通道，避免遗留激活的通道
        pca9548a_disable_all_channels();
        
        pca9548a_delete(pca9548a_handle);
        pca9548a_handle = NULL;
        ESP_LOGI(TAG, "PCA9548A handle deleted");
    }
    
    // 释放I2C驱动，确保在使用外部总线时不会删除总线句柄
    if (i2c_dev_handle != NULL) {
        i2c_master_bus_rm_device(i2c_dev_handle);
        i2c_dev_handle = NULL;
        ESP_LOGI(TAG, "I2C device handle removed");
    }
    
    // 只是清空句柄指针，不实际删除总线
    // 总线由创建者负责删除
    i2c_bus_handle = NULL;
    ESP_LOGI(TAG, "I2C bus handle reference cleared");
    
    g_pca9548a_initialized = false;
#endif

#ifdef CONFIG_ENABLE_HW178
    if (hw178_handle != NULL) {
        hw178_delete(hw178_handle);
        hw178_handle = NULL;
        ESP_LOGI(TAG, "HW-178 handle deleted");
    }
    
    if (adc_handle != NULL) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        ESP_LOGI(TAG, "ADC handle deleted");
    }
    
    g_hw178_initialized = false;
#endif

    g_multiplexer_initialized = false;
    ESP_LOGI(TAG, "Multiplexer components deinitialized");
}

#ifdef CONFIG_ENABLE_PCA9548A
// Function to check if PCA9548A is initialized
bool pca9548a_is_initialized(void)
{
    return g_pca9548a_initialized;
}

// Function to get PCA9548A handle for direct access
pca9548a_handle_t pca9548a_get_handle(void)
{
    if (!g_pca9548a_initialized) {
        ESP_LOGW(TAG, "PCA9548A handle requested but not initialized");
    }
    return pca9548a_handle;
}

// Function to select a specific I2C channel
esp_err_t pca9548a_select_channel(uint8_t channel)
{
    if (!g_pca9548a_initialized || pca9548a_handle == NULL) {
        ESP_LOGE(TAG, "PCA9548A not initialized, cannot select channel");
        return ESP_ERR_INVALID_STATE; // 修改为返回错误代码而非ESP_OK
    }
    
    ESP_LOGI(TAG, "Selecting PCA9548A channel: 0x%02X", channel);
    
    esp_err_t ret = pca9548a_select_channels(pca9548a_handle, channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select channel: %s", esp_err_to_name(ret));
        return ret; // 修改为返回实际错误代码
    }
    
    // Read back the selected channels to confirm
    uint8_t current_channel;
    ret = pca9548a_get_selected_channels(pca9548a_handle, &current_channel);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current PCA9548A channel: 0x%02X", current_channel);
        if (current_channel != channel) {
            ESP_LOGE(TAG, "PCA9548A channel mismatch: requested=0x%02X, actual=0x%02X", 
                    channel, current_channel);
            // 尝试再次设置通道
            ret = pca9548a_select_channels(pca9548a_handle, channel);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to re-select channel: %s", esp_err_to_name(ret));
                return ret; // 修改为返回错误代码
            }
            
            // 再次验证通道
            ret = pca9548a_get_selected_channels(pca9548a_handle, &current_channel);
            if (ret != ESP_OK || current_channel != channel) {
                ESP_LOGE(TAG, "Channel verification failed after retry");
                return ESP_ERR_INVALID_RESPONSE; // 通道切换失败
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to read current PCA9548A channel: %s", esp_err_to_name(ret));
        return ret; // 返回错误代码
    }
    
    // 添加短暂延时以确保通道切换完成
    vTaskDelay(pdMS_TO_TICKS(10)); // 增加延时确保稳定
    
    return ESP_OK; // 只有切换成功才返回ESP_OK
}

// 选择级联复用器路径
esp_err_t pca9548a_select_cascade_path(const pca9548a_cascade_path_t *path)
{
    if (!g_pca9548a_initialized || pca9548a_handle == NULL) {
        ESP_LOGE(TAG, "PCA9548A not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (path == NULL || path->level_count == 0 || path->level_count > 4) {
        ESP_LOGE(TAG, "Invalid cascade path");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Selecting cascade path with %d levels", path->level_count);
    
    // 选择第一级通道
    esp_err_t ret = pca9548a_select_channel(path->channels[0]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select level 1 channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 如果只有一级，直接返回
    if (path->level_count == 1) {
        return ESP_OK;
    }
    
    // 级联选择通道（目前我们不支持级联复用器的直接驱动，而是假设用户会处理后续级联）
    ESP_LOGW(TAG, "Cascade path with %d levels requested, but only first level is directly supported", 
             path->level_count);
    ESP_LOGW(TAG, "User must handle subsequent level selections through I2C transactions");
    
    return ESP_OK;
}

// 获取当前选中的复用器通道
esp_err_t pca9548a_get_current_channel(uint8_t *channel)
{
    if (!g_pca9548a_initialized || pca9548a_handle == NULL) {
        ESP_LOGE(TAG, "PCA9548A not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = pca9548a_get_selected_channels(pca9548a_handle, channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get selected channels: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Current PCA9548A channel: 0x%02X", *channel);
    return ESP_OK;
}

// 禁用所有通道
esp_err_t pca9548a_disable_all_channels(void)
{
    if (!g_pca9548a_initialized || pca9548a_handle == NULL) {
        ESP_LOGE(TAG, "PCA9548A not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Disabling all PCA9548A channels");
    
    esp_err_t ret = pca9548a_select_channels(pca9548a_handle, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable all channels: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}
#endif

// 用于获取LVGL显示屏使用的I2C总线句柄
// 这是一个简单的实现，基于特定项目的LVGL设置
// 在实际项目中，需要根据具体LVGL配置修改此函数
i2c_master_bus_handle_t lvgl_port_get_i2c_bus_handle(void)
{
    // 尝试从LVGL端口获取I2C总线句柄
    // 定义变量存储总线句柄
    i2c_master_bus_handle_t bus_handle = NULL;
    
    // 使用各种方法尝试获取I2C总线句柄
    ESP_LOGD(TAG, "Trying to find I2C bus handle from various sources");

    // 方法1：尝试访问esp_lvgl_port组件中的公共函数（如果可用）
    // 注意：这需要esp_lvgl_port组件提供相应功能
    // 这里我们使用弱符号链接，如果函数不存在则为NULL
    ESP_LOGD(TAG, "Trying to get handle from esp_lvgl_port component");
    __attribute__((weak)) i2c_master_bus_handle_t esp_lvgl_port_get_i2c_bus_handle(void);
    
    if (esp_lvgl_port_get_i2c_bus_handle != NULL) {
        bus_handle = esp_lvgl_port_get_i2c_bus_handle();
        if (bus_handle != NULL) {
            ESP_LOGI(TAG, "Found I2C bus handle from esp_lvgl_port component");
            return bus_handle;
        }
    }
    
    // 如果无法获取，返回NULL
    ESP_LOGW(TAG, "Could not find I2C bus handle from any source");
    ESP_LOGW(TAG, "Please initialize the display first, or manually pass the I2C bus handle");
    ESP_LOGW(TAG, "You can either:");
    ESP_LOGW(TAG, "1. Call multiplexer_init_with_bus() and pass the display's I2C bus handle");
    ESP_LOGW(TAG, "2. Modify display code to make the I2C bus handle globally accessible");
    
    return NULL;
}

/**
 * 为PCF8575选择正确的PCA9548A通道
 */
esp_err_t select_pca9548a_channel(pcf8575_dev_t *dev)
{
    if (!g_pca9548a_initialized || !pca9548a_handle) {
        ESP_LOGE(TAG, "PCA9548A not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return pca9548a_select_channel(dev->pca9548a_channel);
}

/**
 * 别名函数，为了兼容性保留，实际调用select_pca9548a_channel
 */
esp_err_t select_pcf8575_channel(pcf8575_dev_t *dev)
{
    return select_pca9548a_channel(dev);
} 
