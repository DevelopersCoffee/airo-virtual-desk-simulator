# The main component: ESP-IDF glue in this folder plus the portable firmware
# shared with the laptop host. Nothing under airo-core/ or board/ includes ESP-IDF.
set(FW ${CMAKE_CURRENT_LIST_DIR}/../..)
file(GLOB CORE_SRCS ${FW}/airo-core/src/*.cpp)
file(GLOB BOARD_SRCS ${FW}/board/src/*.cpp)

idf_component_register(
  SRCS main.cpp esp_io.cpp web.cpp console.cpp settings.cpp ${CORE_SRCS} ${BOARD_SRCS}
  INCLUDE_DIRS . ${FW}/airo-core/include ${FW}/board/include
  REQUIRES driver esp_http_server esp_wifi esp_netif esp_event esp_timer nvs_flash esp_system)

target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
