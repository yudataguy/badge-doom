add_library(common_pico INTERFACE)
target_sources(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_glob.c
        ${CMAKE_CURRENT_LIST_DIR}/i_system.c
        ${CMAKE_CURRENT_LIST_DIR}/i_timer.c
        ${CMAKE_CURRENT_LIST_DIR}/stubs.c
)
if (PICO_ON_DEVICE)
    target_sources(common_pico INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/picoflash.c
            )
    pico_wrap_function(common_pico malloc)
    pico_wrap_function(common_pico calloc)
    pico_wrap_function(common_pico free)
    target_compile_definitions(common_pico INTERFACE
            PICO_HEAP_SIZE=0
            USE_ZONE_FOR_MALLOC=1
    )
endif()
target_include_directories(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../../textscreen
        )

target_compile_definitions(common_pico INTERFACE
        NO_USE_MOUSE=1
        PICO_USE_SW_SPIN_LOCKS=0
        PICO_USE_STACK_GUARDS=0 # todo we can actually use these sensibly, but right now we do overflow!
)

target_link_libraries(common_pico INTERFACE pico_stdlib pico_multicore)

add_library(doom_sound_pico_i2s INTERFACE)
target_sources(doom_sound_pico_i2s INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_picosound.c
)
target_compile_definitions(doom_sound_pico_i2s INTERFACE
        PICO_AUDIO_I2S_PIO=1
        PICO_AUDIO_I2S_DMA_IRQ=1
)
target_link_libraries(doom_sound_pico_i2s INTERFACE
        common_pico
        pico_audio_i2s
        hardware_gpio
)

add_library(doom_sound_pico_null INTERFACE)
target_sources(doom_sound_pico_null INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_picosound_null.c
)
target_link_libraries(doom_sound_pico_null INTERFACE
        common_pico
)

add_library(doom_input_pico_uart_usb INTERFACE)
target_sources(doom_input_pico_uart_usb INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_input.c
)
target_link_libraries(doom_input_pico_uart_usb INTERFACE
        common_pico
)

add_library(doom_input_badger_stub INTERFACE)
target_sources(doom_input_badger_stub INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_input_badger_stub.c
)
target_link_libraries(doom_input_badger_stub INTERFACE
        common_pico
)

add_library(doom_net_pico_i2c INTERFACE)
target_sources(doom_net_pico_i2c INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/piconet.c
)
if (PICO_ON_DEVICE)
    target_link_libraries(doom_net_pico_i2c INTERFACE
            hardware_i2c
    )
endif()
target_link_libraries(doom_net_pico_i2c INTERFACE
        common_pico
)

add_library(doom_video_vga INTERFACE)
target_sources(doom_video_vga INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_video.c
)
if (PICO_ON_DEVICE)
    target_sources(doom_video_vga INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/blit.S
    )
endif()
pico_generate_pio_header(doom_video_vga ${CMAKE_CURRENT_LIST_DIR}/video_doom.pio)
target_link_libraries(doom_video_vga INTERFACE
        common_pico
        pico_scanvideo_dpi
)

add_library(doom_video_badger_stub INTERFACE)
target_sources(doom_video_badger_stub INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_video_badger_stub.c
)
target_link_libraries(doom_video_badger_stub INTERFACE
        common_pico
)

add_library(doom_badger_hw_tufty INTERFACE)
target_sources(doom_badger_hw_tufty INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/badger_hw_tufty.c
)
pico_generate_pio_header(doom_badger_hw_tufty ${CMAKE_CURRENT_LIST_DIR}/st7789_parallel.pio)
target_link_libraries(doom_badger_hw_tufty INTERFACE
        common_pico
        hardware_gpio
        hardware_dma
        hardware_pio
)

add_library(pico_backend_vga INTERFACE)
target_compile_definitions(pico_backend_vga INTERFACE
        USE_PICO_NET=1
        PICO_DEFAULT_I2C=1
        PICO_DEFAULT_I2C_SDA_PIN=18
        PICO_DEFAULT_I2C_SCL_PIN=19
)
target_link_libraries(pico_backend_vga INTERFACE
        doom_input_pico_uart_usb
        doom_net_pico_i2c
        doom_video_vga
        doom_sound_pico_i2s
)

add_library(pico_backend_badger_scaffold INTERFACE)
target_link_libraries(pico_backend_badger_scaffold INTERFACE
        doom_badger_hw_tufty
        doom_input_badger_stub
        doom_video_badger_stub
        doom_sound_pico_null
)

add_library(pico_cd INTERFACE)
if (TARGET tinyusb_host)
    target_link_libraries(pico_cd INTERFACE tinyusb_host)
endif()
