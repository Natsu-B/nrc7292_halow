idf_component_register(SRCS "app_main.c" "clip_source.c"
  INCLUDE_DIRS "."
  REQUIRES nrc7292 nrc_video_packet hvc_clip nvs_flash esp_partition esp_timer lwip)
