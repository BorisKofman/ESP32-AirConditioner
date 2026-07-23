# Native Matter-over-Thread A/C controller.
idf_component_register(
  SRCS
    "main.cpp"
    "goodweather_ir.c"
    "dht.c"
  INCLUDE_DIRS
    "."
  REQUIRES
    driver
    nvs_flash
    esp_matter
    openthread
    esp_hw_support  # esp_efuse_mac_get_default (MAC-derived pairing identity)
)
