idf_component_register(SRCS "tracker_backup.c" "tracker_adr.c" "tracker_credentials.c" "tracker_timezones.c" "main.c" "radio_settings.c" "gnss_nmea.c" "tracker_display.c" "tracker_web.c" "tracker_ota.c" "track_ring.c" "tracker_log.c" "tracker_mqtt.c" "sat_ring.c" "sat_import.c" "satellite_log.c" "gnss_diagnostics.c" "import_log.c" "status_cache.c"
  INCLUDE_DIRS "." REQUIRES loramac nvs_flash esp_timer esp_driver_uart esp_driver_gpio st7735 esp_wifi esp_netif esp_event esp_http_server json app_update esp_app_format mqtt
  EMBED_TXTFILES "web/index.html" "web/dashboard.js" "web/extras.js" "web/orbits.js" "web/firmware.js" "web/display.js" "web/credentials.js" "web/backup.js" "web/adr.js" "web/timezones.js" "web/satellite.min.js" "web/earth-land.json" "web/satellite.LICENSE.txt" "web/recording.js" "web/history-cache.js" "web/satellite-catalog.js" "web/ttn-position-decoder.js")
target_compile_definitions(__idf_sx1262 PRIVATE TRACKER_V2_FEM=1)
target_compile_definitions(__idf_st7735 PRIVATE TRACKER_V2_DISPLAY=1)

# Compress on the build host. Browsers decompress; the ESP32 serves flash bytes directly.
set(i18n_sources)
foreach(name i18n i18n-ui i18n-inline i18n-dashboard i18n-sky i18n-settings i18n-server)
  list(APPEND i18n_sources "${CMAKE_CURRENT_SOURCE_DIR}/web/${name}.js")
endforeach()
idf_build_get_property(i18n_python PYTHON)
set(i18n_bundle "${CMAKE_CURRENT_BINARY_DIR}/i18n-bundle.js.gz")
add_custom_command(OUTPUT "${i18n_bundle}"
  COMMAND "${i18n_python}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_i18n_bundle.py"
    --web "${CMAKE_CURRENT_SOURCE_DIR}/web" --output "${i18n_bundle}"
  DEPENDS ${i18n_sources} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_i18n_bundle.py"
  VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${i18n_bundle}" BINARY RENAME_TO i18n_bundle_gz)
