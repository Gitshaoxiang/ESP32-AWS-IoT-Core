## ESP32 AWS IoT Core

- Environment: ESP IDF v4.3

### Certificate 

- 1. copy the Certificate/Key file to the ./main/certs path. 

```
aws-root-ca.pem
certificate.pem.crt
private.pem.key
```

### Config

- 2. use `idf.py menuconfig` to config the wifi and host address, thing name

![config_01](./assets/config_01.jpg)
