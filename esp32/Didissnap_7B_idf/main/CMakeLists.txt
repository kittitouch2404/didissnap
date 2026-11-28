file(GLOB FONT_SRCS fonts/*.c)
idf_component_register(
  SRCS main.cpp board.c slink.cpp bfile.cpp vdata.cpp skin.cpp ui.cpp ${FONT_SRCS}
  INCLUDE_DIRS .
  REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_sdmmc sdmmc fatfs vfs nvs_flash bt json esp_timer esp_psram)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-unused-function)
