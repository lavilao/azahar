add_library(citra-android SHARED
    android_common/android_common.cpp
    android_common/android_common.h
    applets/mii_selector.cpp
    applets/mii_selector.h
    applets/swkbd.cpp
    applets/swkbd.h
    input_manager.cpp
    input_manager.h
    camera/ndk_camera.cpp
    camera/ndk_camera.h
    camera/still_image_camera.cpp
    camera/still_image_camera.h
    cheats/cheat.cpp
    cheats/cheat.h
    cheats/cheat_engine.cpp
    config.cpp
    config.h
    default_ini.h
    emu_window/emu_window.cpp
    emu_window/emu_window.h
    game_info.cpp
    id_cache.cpp
    id_cache.h
    jni_setting_keys.cpp
    multiplayer.cpp
    multiplayer.h
    native.cpp
    ndk_motion.cpp
    ndk_motion.h
    system_save_game.cpp
    native_log.cpp
    crash_handler.cpp
    crash_handler.h
)

target_link_libraries(citra-android PRIVATE audio_core citra_common citra_core input_common network)
# camera2ndk y mediandk NO se enlazan: se cargan con dlopen desde
# camera/lazy_ndk.h al abrir la camara, para no arrastrar el framework de
# camara (libcamera_client/libgui/libbinder) al arranque de la app
target_link_libraries(citra-android PRIVATE android inih jnigraphics log yuv)

if (ENABLE_OPENGL)
    target_sources(citra-android PRIVATE
        emu_window/emu_window_gl.cpp
        emu_window/emu_window_gl.h
    )
    target_link_libraries(citra-android PRIVATE EGL glad)
endif()
if (ENABLE_VULKAN)
    target_sources(citra-android PRIVATE
        emu_window/emu_window_vk.cpp
        emu_window/emu_window_vk.h
    )
    if ("arm64" IN_LIST ARCHITECTURE)
        target_link_libraries(citra-android PRIVATE adrenotools)
    endif()
endif()

set(CPACK_PACKAGE_EXECUTABLES ${CPACK_PACKAGE_EXECUTABLES} citra-android)
