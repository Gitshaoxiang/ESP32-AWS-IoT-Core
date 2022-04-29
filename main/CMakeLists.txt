set(COMPONENT_SRCS "main.c" "wifi.c")
# set(COMPONENT_ADD_INCLUDEDIRS "./includes")
set(COMPONENT_REQUIRES "nvs_flash" "esp-aws-iot")
register_component()

target_add_binary_data(${COMPONENT_TARGET} "certs/aws-root-ca.pem" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/certificate.pem.crt" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/private.pem.key" TEXT)