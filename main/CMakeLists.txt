set(SOURCES "audio_codecs/audio_codec.cc"
            "audio_codecs/no_audio_codec.cc"
            "audio_codecs/box_audio_codec.cc"
            "audio_codecs/es8311_audio_codec.cc"
            "audio_codecs/es8374_audio_codec.cc"
            "audio_codecs/es8388_audio_codec.cc"
            "led/single_led.cc"
            "led/circular_strip.cc"
            "led/gpio_led.cc"
            "display/display.cc"
            "display/lcd_display.cc"
            "display/oled_display.cc"
            "protocols/protocol.cc"
            "protocols/mqtt_protocol.cc"
            "protocols/websocket_protocol.cc"
            "iot/thing.cc"
            "iot/thing_manager.cc"
            "mcp_server.cc"
            "system_info.cc"
            "application.cc"
            "ota.cc"
            "settings.cc"
            "background_task.cc"
            "main.cc"
            "components.cc"
            "move/move_controller.cc"
            "move/move_content.cc"
            "ai/ai_controller.cc"
            "ai/ai_content.cc"
            "vision/vision_controller.cc"
            "vision/vision_content.cc"
            "iot/things/motor.cc"
            "iot/things/servo.cc"
            "iot/things/us.cc"
            "iot/things/battery.cc"
            "iot/things/cam.cc"
            "iot/things/imu.cc"
            "iot/things/lamp.cc"
            "iot/things/light.cc"
            "iot/things/screen.cc"
            "iot/things/speaker.cc"
            "ext/pcf8575.c"
            "ext/lu9685.c"
            "web/web_server.cc"
            "web/web_content.cc"
            "web/html_content.cc"
            "web/api_router.cc"
            "web/api_handlers.cc"
            )

set(INCLUDE_DIRS "." "display" "audio_codecs" "protocols" "audio_processing" "location")

# HTML文件列表 - 只在启用web内容时使用
set(EMBED_FILES "")
if(CONFIG_ENABLE_WEB_SERVER AND CONFIG_ENABLE_WEB_CONTENT)
    # 指定绝对路径，确保正确找到HTML文件
    set(HTML_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/html/index.html"
                   "${CMAKE_CURRENT_SOURCE_DIR}/assets/html/move.html"
                   "${CMAKE_CURRENT_SOURCE_DIR}/assets/html/vision.html"
                   "${CMAKE_CURRENT_SOURCE_DIR}/assets/html/servo_control.html"
                   "${CMAKE_CURRENT_SOURCE_DIR}/assets/html/ai.html")
    
    # JavaScript文件列表
    set(JS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/html/js/api_client.js")
    
    # 检查文件是否存在
    foreach(HTML_FILE ${HTML_FILES})
        if(NOT EXISTS ${HTML_FILE})
            message(WARNING "HTML文件不存在: ${HTML_FILE}")
        else()
            message(STATUS "找到HTML文件: ${HTML_FILE}")
        endif()
    endforeach()
    
    foreach(JS_FILE ${JS_FILES})
        if(NOT EXISTS ${JS_FILE})
            message(WARNING "JavaScript文件不存在: ${JS_FILE}")
        else()
            message(STATUS "找到JavaScript文件: ${JS_FILE}")
        endif()
    endforeach()
    
    list(APPEND EMBED_FILES ${HTML_FILES})
    list(APPEND EMBED_FILES ${JS_FILES})
    message(STATUS "HTML和JavaScript文件将被嵌入到固件中")
endif()

# 根据配置添加Web组件
if(CONFIG_ENABLE_WEB_SERVER)
    list(APPEND SOURCES 
        "web/web_server.cc"
    )
    list(APPEND INCLUDE_DIRS "web")
    list(APPEND INCLUDE_DIRS "assets/html")
    
    # 添加web组件所需依赖
    if(NOT DEFINED COMPONENT_REQUIRES)
        set(COMPONENT_REQUIRES "")
    endif()
    list(APPEND COMPONENT_REQUIRES "lwip" "esp_http_server")
    
    if(CONFIG_ENABLE_WEB_CONTENT)
        list(APPEND SOURCES 
            "web/web_content.cc" 
            "web/html_content.cc"
        )
    endif()
endif()

# 根据配置添加Location组件
if(CONFIG_ENABLE_LOCATION_CONTROLLER)
    list(APPEND SOURCES 
        "location/location_controller.cc"
        "location/location_content.cc"
    )
    message(STATUS "位置定位组件已启用")
endif()

# 使用file(GLOB...)来收集额外的源文件
file(GLOB IOT_THINGS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/iot/things/*.cc)
file(GLOB WEB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/web/*.cc)

# 收集其他protocols文件(排除已经明确列出的主要文件)
file(GLOB_RECURSE ALL_PROTOCOLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/protocols/*.cc)
list(FILTER ALL_PROTOCOLS_SOURCES EXCLUDE REGEX ".*(protocol|mqtt_protocol|websocket_protocol)\\.cc$")
set(PROTOCOLS_SOURCES ${ALL_PROTOCOLS_SOURCES})

# 收集其他move组件文件(排除已经明确列出的主要文件)
file(GLOB ADDITIONAL_MOVE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/move/*.cc)
list(FILTER ADDITIONAL_MOVE_SOURCES EXCLUDE REGEX ".*(move_controller|move_content)\\.cc$")
set(MOVE_SOURCES ${ADDITIONAL_MOVE_SOURCES})

# 将所有收集的文件添加到SOURCES列表
list(APPEND SOURCES ${IOT_THINGS_SOURCES})
list(APPEND SOURCES ${WEB_SOURCES})
list(APPEND SOURCES ${PROTOCOLS_SOURCES})
list(APPEND SOURCES ${MOVE_SOURCES})

# 添加板级公共文件
file(GLOB BOARD_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/boards/common/*.cc)
list(APPEND SOURCES ${BOARD_COMMON_SOURCES})
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/common)

# 根据 BOARD_TYPE 配置添加对应的板级文件
if(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI)
    set(BOARD_TYPE "bread-compact-wifi")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ML307)
    set(BOARD_TYPE "bread-compact-ml307")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ESP32)
    set(BOARD_TYPE "bread-compact-esp32")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_ESP32_LCD)
    set(BOARD_TYPE "bread-compact-esp32-lcd")
elseif(CONFIG_BOARD_TYPE_DF_K10)
    set(BOARD_TYPE "df-k10")
elseif(CONFIG_BOARD_TYPE_ESP_BOX_3)
    set(BOARD_TYPE "esp-box-3")
elseif(CONFIG_BOARD_TYPE_ESP_BOX)
    set(BOARD_TYPE "esp-box")
elseif(CONFIG_BOARD_TYPE_ESP_BOX_LITE)
    set(BOARD_TYPE "esp-box-lite")
elseif(CONFIG_BOARD_TYPE_KEVIN_BOX_1)
    set(BOARD_TYPE "kevin-box-1")
elseif(CONFIG_BOARD_TYPE_KEVIN_BOX_2)
    set(BOARD_TYPE "kevin-box-2")
elseif(CONFIG_BOARD_TYPE_KEVIN_C3)
    set(BOARD_TYPE "kevin-c3")
elseif(CONFIG_BOARD_TYPE_KEVIN_SP_V3_DEV)
    set(BOARD_TYPE "kevin-sp-v3-dev")
elseif(CONFIG_BOARD_TYPE_KEVIN_SP_V4_DEV)
    set(BOARD_TYPE "kevin-sp-v4-dev")
elseif(CONFIG_BOARD_TYPE_KEVIN_YUYING_313LCD)
    set(BOARD_TYPE "kevin-yuying-313lcd")
elseif(CONFIG_BOARD_TYPE_LICHUANG_DEV)
    set(BOARD_TYPE "lichuang-dev")
elseif(CONFIG_BOARD_TYPE_LICHUANG_C3_DEV)
    set(BOARD_TYPE "lichuang-c3-dev")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_2P4)
    set(BOARD_TYPE "magiclick-2p4")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_2P5)
    set(BOARD_TYPE "magiclick-2p5")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_C3)
    set(BOARD_TYPE "magiclick-c3")
elseif(CONFIG_BOARD_TYPE_MAGICLICK_C3_V2)
    set(BOARD_TYPE "magiclick-c3-v2")
elseif(CONFIG_BOARD_TYPE_M5STACK_CORE_S3)
    set(BOARD_TYPE "m5stack-core-s3")
elseif(CONFIG_BOARD_TYPE_M5STACK_CORE_TAB5)
    set(BOARD_TYPE "m5stack-tab5")
elseif(CONFIG_BOARD_TYPE_ATOMS3_ECHO_BASE)
    set(BOARD_TYPE "atoms3-echo-base")
elseif(CONFIG_BOARD_TYPE_ATOMS3R_ECHO_BASE)
    set(BOARD_TYPE "atoms3r-echo-base")
elseif(CONFIG_BOARD_TYPE_ATOMS3R_CAM_M12_ECHO_BASE)
    set(BOARD_TYPE "atoms3r-cam-m12-echo-base")
elseif(CONFIG_BOARD_TYPE_ATOMMATRIX_ECHO_BASE)
    set(BOARD_TYPE "atommatrix-echo-base")
elseif(CONFIG_BOARD_TYPE_XMINI_C3)
    set(BOARD_TYPE "xmini-c3")
elseif(CONFIG_BOARD_TYPE_ESP32S3_KORVO2_V3)
    set(BOARD_TYPE "esp32s3-korvo2-v3")
elseif(CONFIG_BOARD_TYPE_ESP_SPARKBOT)
    set(BOARD_TYPE "esp-sparkbot")
elseif(CONFIG_BOARD_TYPE_ESP_SPOT_S3)
    set(BOARD_TYPE "esp-spot-s3")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_8)
    set(BOARD_TYPE "esp32-s3-touch-amoled-1.8")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_75)
    set(BOARD_TYPE "waveshare-s3-touch-amoled-1.75")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85c")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.85")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_46)
    set(BOARD_TYPE "esp32-s3-touch-lcd-1.46")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_3_5)
    set(BOARD_TYPE "esp32-s3-touch-lcd-3.5")
elseif(CONFIG_BOARD_TYPE_ESP32P4_NANO)
    set(BOARD_TYPE "waveshare-p4-nano")
elseif(CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_LCD)
    set(BOARD_TYPE "bread-compact-wifi-lcd")
elseif(CONFIG_BOARD_TYPE_TUDOUZI)
    set(BOARD_TYPE "tudouzi")
elseif(CONFIG_BOARD_TYPE_LILYGO_T_CIRCLE_S3)
    set(BOARD_TYPE "lilygo-t-circle-s3")
elseif(CONFIG_BOARD_TYPE_LILYGO_T_CAMERAPLUS_S3)
    set(BOARD_TYPE "lilygo-t-cameraplus-s3")
elseif(CONFIG_BOARD_TYPE_LILYGO_T_DISPLAY_S3_PRO_MVSRLORA)
    set(BOARD_TYPE "lilygo-t-display-s3-pro-mvsrlora")
elseif(CONFIG_BOARD_TYPE_LILYGO_T_DISPLAY_S3_PRO_MVSRLORA_NO_BATTERY)
    set(BOARD_TYPE "lilygo-t-display-s3-pro-mvsrlora")
elseif(CONFIG_BOARD_TYPE_MOVECALL_MOJI_ESP32S3)
    set(BOARD_TYPE "movecall-moji-esp32s3")
    elseif(CONFIG_BOARD_TYPE_MOVECALL_CUICAN_ESP32S3)
    set(BOARD_TYPE "movecall-cuican-esp32s3")
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3)
    set(BOARD_TYPE "atk-dnesp32s3")
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3_BOX)
    set(BOARD_TYPE "atk-dnesp32s3-box")
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3_BOX0)
    set(BOARD_TYPE "atk-dnesp32s3-box0")
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3M_WIFI)
    set(BOARD_TYPE "atk-dnesp32s3m-wifi")
elseif(CONFIG_BOARD_TYPE_ATK_DNESP32S3M_4G)
    set(BOARD_TYPE "atk-dnesp32s3m-4g")
elseif(CONFIG_BOARD_TYPE_DU_CHATX)
    set(BOARD_TYPE "du-chatx")
elseif(CONFIG_BOARD_TYPE_ESP32S3_Taiji_Pi)
    set(BOARD_TYPE "taiji-pi-s3")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_0_85TFT_WIFI)
    set(BOARD_TYPE "xingzhi-cube-0.85tft-wifi")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_0_85TFT_ML307)
    set(BOARD_TYPE "xingzhi-cube-0.85tft-ml307")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_0_96OLED_WIFI)
    set(BOARD_TYPE "xingzhi-cube-0.96oled-wifi")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_0_96OLED_ML307)
    set(BOARD_TYPE "xingzhi-cube-0.96oled-ml307")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_1_54TFT_WIFI)
    set(BOARD_TYPE "xingzhi-cube-1.54tft-wifi")
elseif(CONFIG_BOARD_TYPE_XINGZHI_Cube_1_54TFT_ML307)
    set(BOARD_TYPE "xingzhi-cube-1.54tft-ml307")
elseif(CONFIG_BOARD_TYPE_SENSECAP_WATCHER)
    set(BOARD_TYPE "sensecap-watcher")
elseif(CONFIG_BOARD_TYPE_DOIT_S3_AIBOX)
    set(BOARD_TYPE "doit-s3-aibox")
elseif(CONFIG_BOARD_TYPE_MIXGO_NOVA)
    set(BOARD_TYPE "mixgo-nova")
elseif(CONFIG_BOARD_TYPE_GENJUTECH_S3_1_54TFT)
    set(BOARD_TYPE "genjutech-s3-1.54tft")
elseif(CONFIG_BOARD_TYPE_ESP32_CGC)
    set(BOARD_TYPE "esp32-cgc")
elseif(CONFIG_BOARD_TYPE_ESP_S3_LCD_EV_Board)
    set(BOARD_TYPE "esp-s3-lcd-ev-board")
elseif(CONFIG_BOARD_TYPE_ZHENGCHEN_1_54TFT_WIFI)
    set(BOARD_TYPE "zhengchen-1.54tft-wifi")
elseif(CONFIG_BOARD_TYPE_MINSI_K08_DUAL)
    set(BOARD_TYPE "minsi-k08-dual")
elseif(CONFIG_BOARD_TYPE_ZHENGCHEN_1_54TFT_ML307)
    set(BOARD_TYPE "zhengchen-1.54tft-ml307")
elseif(CONFIG_BOARD_TYPE_ESP32_S3_1_54_MUMA)
    set(BOARD_TYPE "sp-esp32-s3-1.54-muma")
endif()
file(GLOB BOARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_TYPE}/*.c
)
list(APPEND SOURCES ${BOARD_SOURCES})

if(CONFIG_USE_AUDIO_PROCESSOR)
    list(APPEND SOURCES "audio_processing/afe_audio_processor.cc")
else()
    list(APPEND SOURCES "audio_processing/dummy_audio_processor.cc")
endif()
if(CONFIG_USE_WAKE_WORD_DETECT)
    list(APPEND SOURCES "audio_processing/wake_word_detect.cc")
endif()

# 根据Kconfig选择语言目录
if(CONFIG_LANGUAGE_ZH_CN)
    set(LANG_DIR "zh-CN")
elseif(CONFIG_LANGUAGE_ZH_TW)
    set(LANG_DIR "zh-TW")
elseif(CONFIG_LANGUAGE_EN_US)
    set(LANG_DIR "en-US")
elseif(CONFIG_LANGUAGE_JA_JP)
    set(LANG_DIR "ja-JP")
endif()

# 定义生成路径
set(LANG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/assets/${LANG_DIR}/language.json")
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")
file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/${LANG_DIR}/*.p3)
file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.p3)

# 添加语音文件到嵌入列表
list(APPEND EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS})

# 如果目标芯片是 ESP32，则排除特定文件
if(CONFIG_IDF_TARGET_ESP32)
    list(REMOVE_ITEM SOURCES "audio_codecs/box_audio_codec.cc"
                             "audio_codecs/es8388_audio_codec.cc"
                             "led/gpio_led.cc"
                             )
endif()

# 添加外部扩展组件文件
file(GLOB EXT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ext/*.c)
file(GLOB EXT_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/ext/include/*.h)
list(APPEND SOURCES ${EXT_SOURCES})
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/ext ${CMAKE_CURRENT_SOURCE_DIR}/ext/include)

idf_component_register(SRCS ${SOURCES}
                    EMBED_FILES ${EMBED_FILES}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    REQUIRES ${COMPONENT_REQUIRES}
                    WHOLE_ARCHIVE
                    )

# 使用 target_compile_definitions 来定义 BOARD_TYPE, BOARD_NAME
# 如果 BOARD_NAME 为空，则使用 BOARD_TYPE
if(NOT BOARD_NAME)
    set(BOARD_NAME ${BOARD_TYPE})
endif()
target_compile_definitions(${COMPONENT_LIB}
                    PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
                    )

# 添加生成规则
add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${PROJECT_DIR}/scripts/gen_lang.py
            --input "${LANG_JSON}"
            --output "${LANG_HEADER}"
    DEPENDS
        ${LANG_JSON}
        ${PROJECT_DIR}/scripts/gen_lang.py
    COMMENT "Generating ${LANG_DIR} language config"
)

# 强制建立生成依赖
add_custom_target(lang_header ALL
    DEPENDS ${LANG_HEADER}
)
