CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

INCLUDE_DIRECTORIES(
                ./
                ${PROJECT_SOURCE_DIR}/tfcard/
                ${PROJECT_SOURCE_DIR}/tfcard/include
                ${PROJECT_SOURCE_DIR}/tfcard/other/
                ${PROJECT_SOURCE_DIR}/p2p_lib/
                ${PROJECT_SOURCE_DIR}/hapRtsp_server/
                ${PROJECT_SOURCE_DIR}/onvif_server/
                ${PROJECT_SOURCE_DIR}/user_func/
                ${PROJECT_SOURCE_DIR}/config/
                ${PROJECT_SOURCE_DIR}/net_config/
                ${PROJECT_SOURCE_DIR}/network/
                ${PROJECT_SOURCE_DIR}/encrypt/
                ${PROJECT_SOURCE_DIR}/third_lib/include
                ${PROJECT_SOURCE_DIR}/third_lib/aac_enc/include/
                ${PROJECT_SOURCE_DIR}/media_stream
                ${PROJECT_SOURCE_DIR}/ipc/
                ${PROJECT_SOURCE_DIR}/drivers/
                ${PROJECT_SOURCE_DIR}/drivers/wdt/
                ${PROJECT_SOURCE_DIR}/serial/
                ${PROJECT_SOURCE_DIR}/audio_encoder/
                ${PROJECT_SOURCE_DIR}/record/
                ${PROJECT_SOURCE_DIR}/common_utiles/include/
                ${PROJECT_SOURCE_DIR}/common/
                ${PROJECT_SOURCE_DIR}/rtmp/
                ${PROJECT_SOURCE_DIR}/rtmp/librtmp/
                ${PROJECT_SOURCE_DIR}/public_h/
                ${PROJECT_SOURCE_DIR}/procon/
                ${PROJECT_SOURCE_DIR}/msg_mod/
                ${PROJECT_SOURCE_DIR}/osd_manage/
                ${PROJECT_SOURCE_DIR}/flv_hls_server/
                ${PROJECT_SOURCE_DIR}/platform_bsp/
                ${PROJECT_SOURCE_DIR}/logLib/
                ${PROJECT_SOURCE_DIR}/curlLib/
                ${PROJECT_SOURCE_DIR}/user_func/sleep_manage/
                )

if (${SOC_CHIP} STREQUAL "SSC337DE")

    INCLUDE_DIRECTORIES(
        ${SOC_SDK}/SSC337DE/include/
        ${PROJECT_SOURCE_DIR}/wechat_lib/
        ${PROJECT_SOURCE_DIR}/third_lib/include/zbar/
        ${PROJECT_SOURCE_DIR}/third_lib/include/zbar/zbar/
        ${PROJECT_SOURCE_DIR}/third_lib/include/iconv/
    )

    LINK_DIRECTORIES(
        ${PROJECT_SOURCE_DIR}/third_lib/soc_lib/SSC337DE/
        ${SOC_SDK}/SSC337DE/mi_libs/dynamic/
        ${SOC_SDK}/SSC337DE/ex_libs/dynamic/
    )

    SET(SOC_LIB platform_bsp cam_fs_wrapper cam_os_wrapper mi_sys mi_common mi_isp cus3a ispalgo mi_sensor
        mi_vif mi_vpe mi_venc mi_rgn mi_divp mi_iqserver mi_vdf mi_shadow OD_LINUX MD_LINUX VG_LINUX mi_ive mi_ao mi_ai)

elseif (${SOC_CHIP} STREQUAL "SSC3XX_AI")

    INCLUDE_DIRECTORIES(
        ${SOC_SDK}/SSC336Q/include/
        ${PROJECT_SOURCE_DIR}/wechat_lib/
        ${PROJECT_SOURCE_DIR}/third_lib/include/zbar/
        ${PROJECT_SOURCE_DIR}/third_lib/include/zbar/zbar/
        ${PROJECT_SOURCE_DIR}/third_lib/include/iconv/
    )

    LINK_DIRECTORIES(
        ${PROJECT_SOURCE_DIR}/third_lib/soc_lib/SSC336Q/
        ${SOC_SDK}/SSC336Q/mi_libs/dynamic/
        ${SOC_SDK}/SSC336Q/ex_libs/dynamic/
        ${PROJECT_SOURCE_DIR}/platform_bsp/SSC336Q/internal/algo/libs
    )

    SET(SOC_LIB platform_bsp cam_fs_wrapper cam_os_wrapper mi_sys mi_common mi_isp cus3a ispalgo mi_sensor sstar_algo_detection
        mi_vif mi_vpe mi_venc mi_rgn mi_divp mi_iqserver mi_sed mi_ipu mi_vdf mi_shadow OD_LINUX MD_LINUX VG_LINUX mi_ive mi_ao fbc_decode cus3a ispalgo mi_ai)

elseif (${SOC_CHIP} STREQUAL "SSC3XX_AI_V046")

    INCLUDE_DIRECTORIES(
        ${SOC_SDK}/SSC336Q_V046/include/
        ${PROJECT_SOURCE_DIR}/wechat_lib/
        ${PROJECT_SOURCE_DIR}/third_lib/include/zbar/
        ${PROJECT_SOURCE_DIR}/third_lib/include/zbar/zbar/
        ${PROJECT_SOURCE_DIR}/third_lib/include/iconv/
    )

    LINK_DIRECTORIES(
        ${PROJECT_SOURCE_DIR}/third_lib/soc_lib/SSC336Q/
        ${SOC_SDK}/SSC336Q_V046/mi_libs/dynamic/
        ${SOC_SDK}/SSC336Q_V046/ex_libs/dynamic/
    )

    SET(SOC_LIB platform_bsp cam_fs_wrapper cam_os_wrapper mi_sys mi_common mi_isp cus3a ispalgo mi_sensor AEC_LINUX AED_LINUX APC_LINUX BF_LINUX SSL_LINUX SRC_LINUX g711
        mi_vif mi_vpe mi_venc mi_rgn mi_divp mi_iqserver mi_sed mi_ipu mi_vdf mi_shadow OD_LINUX MD_LINUX VG_LINUX mi_ive mi_ao fbc_decode cus3a ispalgo mi_ai)

elseif ( SOC_CHIP STREQUAL "JZ_T40")

    INCLUDE_DIRECTORIES(
            ${PROJECT_SOURCE_DIR}/soc_mpp_lib/JZ_T40/include/
        )

    LINK_DIRECTORIES(
            ${PROJECT_SOURCE_DIR}/third_lib/soc_lib/T40/
            ${PROJECT_SOURCE_DIR}/soc_mpp_lib/JZ_T40/uclibc/
        )

    SET(SOC_LIB platform_bsp imp alog sysutils audioProcess)

endif()

SET(SYS_LIB pthread dl c m rt stdc++)

SET(THIRD_LIB media_stream serial config rtmp user_func drivers audio_encoder osd_manage encrypt net_config curlLib curl common_utiles
        common procon network msg_mod flv_hls_server ipc tcp_lib hapRtsp_server onvif_server logLib sqlite_db sqlite3)
SET(THIRD_LIB_SK_MAIN media_stream config rtmp user_func drivers audio_encoder osd_manage encrypt common_utiles
        common procon network flv_hls_server ipc tcp_lib hapRtsp_server onvif_server logLib sqlite_db sqlite3 tfcard common zbar iconv)

if( TRANS_TYPE STREQUAL "P2P_NET" )
    SET(TRANS_LIB  p2p_lib p2ptran zbar iconv crypto ssl tfcard)
elseif( TRANS_TYPE STREQUAL "WECHAT_NET" )
    SET(TRANS_LIB  wechat_lib WebrtcMasterc zbar iconv crypto ssl tfcard)
endif()

AUX_SOURCE_DIRECTORY(./ src)
SET(video_server_src)
FOREACH(src_file ${src})
    GET_FILENAME_COMPONENT(src_name ${src_file} NAME)
    IF(NOT src_name STREQUAL "homey_test.cpp"
       AND NOT src_name STREQUAL "homey_service.cpp"
       AND NOT src_name STREQUAL "main_homey.cpp"
       AND NOT src_name STREQUAL "sk_aplay.cpp"
       AND NOT src_name STREQUAL "sk_arecord.cpp"
       AND NOT src_name STREQUAL "sk_live.cpp"
       AND NOT src_name STREQUAL "sk_live_basic.cpp"
       AND NOT src_name STREQUAL "sk_humid_temp.cpp"
       AND NOT src_name STREQUAL "sk_radar.cpp"
       AND NOT src_name STREQUAL "sk_thermal.cpp"
       AND NOT src_name STREQUAL "sk_mic.cpp"
       AND NOT src_name STREQUAL "sk_main.cpp")
        LIST(APPEND video_server_src ${src_file})
    ENDIF()
ENDFOREACH()
SET(src ${video_server_src})
ADD_EXECUTABLE(video_server ${src})

SET_TARGET_PROPERTIES(video_server PROPERTIES LINK_FLAGS "-no-pie")

TARGET_LINK_LIBRARIES(video_server ${SOC_LIB} ${THIRD_LIB} ${TRANS_LIB} fdk-aac cam_fs_wrapper cam_os_wrapper ${SYS_LIB})

SET(video_server_homey_src)
FOREACH(src_file ${video_server_src})
    GET_FILENAME_COMPONENT(src_name ${src_file} NAME)
    IF(NOT src_name STREQUAL "main.cpp")
        LIST(APPEND video_server_homey_src ${src_file})
    ENDIF()
ENDFOREACH()
LIST(APPEND video_server_homey_src ${CMAKE_CURRENT_SOURCE_DIR}/main_homey.cpp)
LIST(APPEND video_server_homey_src ${CMAKE_CURRENT_SOURCE_DIR}/homey_service.cpp)
ADD_EXECUTABLE(video_server_homey ${video_server_homey_src})
SET_TARGET_PROPERTIES(video_server_homey PROPERTIES LINK_FLAGS "-no-pie")
TARGET_LINK_LIBRARIES(video_server_homey ${SOC_LIB} ${THIRD_LIB} ${TRANS_LIB} fdk-aac cam_fs_wrapper cam_os_wrapper ${SYS_LIB})

SET(sk_main_src ${CMAKE_CURRENT_SOURCE_DIR}/media_server.cpp)
LIST(APPEND sk_main_src ${CMAKE_CURRENT_SOURCE_DIR}/sk_main.cpp)
LIST(APPEND sk_main_src ${PROJECT_SOURCE_DIR}/serial/GEN2Protocol.cpp)
ADD_EXECUTABLE(sk_main ${sk_main_src})
SET_TARGET_PROPERTIES(sk_main PROPERTIES LINK_FLAGS "-no-pie")
TARGET_INCLUDE_DIRECTORIES(sk_main PRIVATE ${PROJECT_SOURCE_DIR}/serial/)
TARGET_LINK_LIBRARIES(sk_main ${SOC_LIB} ${THIRD_LIB_SK_MAIN} drivers fdk-aac cam_fs_wrapper cam_os_wrapper ${SYS_LIB})

ADD_EXECUTABLE(sk_aplay sk_aplay.cpp)
SET_TARGET_PROPERTIES(sk_aplay PROPERTIES LINK_FLAGS "-no-pie")
TARGET_LINK_LIBRARIES(sk_aplay mi_ao mi_sys mi_common cam_os_wrapper cam_fs_wrapper ${SYS_LIB})

ADD_EXECUTABLE(sk_arecord sk_arecord.cpp)
SET_TARGET_PROPERTIES(sk_arecord PROPERTIES LINK_FLAGS "-no-pie")
TARGET_LINK_LIBRARIES(sk_arecord mi_ai mi_sys mi_common cam_os_wrapper cam_fs_wrapper ${SYS_LIB})

ADD_EXECUTABLE(sk_humid_temp sk_humid_temp.cpp)
SET_TARGET_PROPERTIES(sk_humid_temp PROPERTIES LINK_FLAGS "-no-pie")
TARGET_LINK_LIBRARIES(sk_humid_temp drivers ${SYS_LIB})

ADD_EXECUTABLE(sk_radar sk_radar.cpp)
SET_TARGET_PROPERTIES(sk_radar PROPERTIES LINK_FLAGS "-no-pie")
TARGET_LINK_LIBRARIES(sk_radar ${SYS_LIB})

ADD_EXECUTABLE(sk_thermal sk_thermal.cpp ${PROJECT_SOURCE_DIR}/serial/GEN2Protocol.cpp)
SET_TARGET_PROPERTIES(sk_thermal PROPERTIES LINK_FLAGS "-no-pie")
TARGET_INCLUDE_DIRECTORIES(sk_thermal PRIVATE ${PROJECT_SOURCE_DIR}/serial/)
TARGET_LINK_LIBRARIES(sk_thermal ${SYS_LIB})

ADD_EXECUTABLE(sk_mic sk_mic.cpp)
SET_TARGET_PROPERTIES(sk_mic PROPERTIES LINK_FLAGS "-no-pie")
TARGET_LINK_LIBRARIES(sk_mic mi_ai mi_sys mi_common cam_os_wrapper cam_fs_wrapper ${SYS_LIB})

SET(OPUS_ROOT "/tmp/opus-root/usr/local" CACHE PATH "Opus install prefix")
IF(NOT EXISTS "${OPUS_ROOT}/include/opus/opus.h")
    SET(OPUS_ROOT "${PROJECT_SOURCE_DIR}/../../opus/tmp/opus-root/usr" CACHE PATH "Opus install prefix" FORCE)
ENDIF()

ADD_EXECUTABLE(sk_live sk_live.cpp)
SET_TARGET_PROPERTIES(sk_live PROPERTIES LINK_FLAGS "-no-pie")
TARGET_INCLUDE_DIRECTORIES(sk_live PRIVATE ${OPUS_ROOT}/include)
TARGET_LINK_LIBRARIES(sk_live mi_ai mi_ao mi_sys mi_common cam_os_wrapper cam_fs_wrapper ${OPUS_ROOT}/lib/libopus.a ${SYS_LIB})

ADD_EXECUTABLE(sk_live_basic sk_live_basic.cpp)
SET_TARGET_PROPERTIES(sk_live_basic PROPERTIES LINK_FLAGS "-no-pie")
TARGET_INCLUDE_DIRECTORIES(sk_live_basic PRIVATE ${OPUS_ROOT}/include)
TARGET_LINK_LIBRARIES(sk_live_basic mi_ai mi_ao mi_sys mi_common cam_os_wrapper cam_fs_wrapper ${OPUS_ROOT}/lib/libopus.a ${SYS_LIB})
