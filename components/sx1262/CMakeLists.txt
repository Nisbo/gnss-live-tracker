idf_component_register(
  SRCS "sx1262.c"
  INCLUDE_DIRS "."
  REQUIRES driver esp_timer board isr_service
)

