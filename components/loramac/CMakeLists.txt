# GNSS Live Tracker V2 modifications, 2026; see NOTICE and docs/UPSTREAM.md.
file(GLOB LORAMAC_CORE_SRCS "loramac/*.c")
file(GLOB LORAMAC_REGION_SRCS "loramac/region/*.c")
file(GLOB LORAMAC_RADIO_SRCS "radio/*.c")
file(GLOB LORAMAC_DRIVER_SRCS "driver/*.c")

idf_component_register(
  SRCS
    "lorawan_port.c"
    "adr_history.c"
    "loramac_globals.c"
    ${LORAMAC_CORE_SRCS}
    ${LORAMAC_REGION_SRCS}
    ${LORAMAC_RADIO_SRCS}
    ${LORAMAC_DRIVER_SRCS}
  INCLUDE_DIRS
    "."
    "loramac"
    "loramac/region"
    "radio"
    "driver"
  REQUIRES
    esp_timer
    esp_rom
    esp_system
    freertos
    driver
    board
    sx1262
    nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  REGION_AS923
  REGION_AU915
  REGION_AU915_SB2
  REGION_CN470
  REGION_CN779
  REGION_EU433
  REGION_EU868
  REGION_KR920
  REGION_IN865
  REGION_US915
  REGION_US915_HYBRID
  REGION_RU864
)
