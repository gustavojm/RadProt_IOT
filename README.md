# RadProt_IoT

Firmware para Raspberry Pi Pico W que sirve una interfaz web embebida con conexión
WiFi y/o Ethernet (ENC28J60), soporte MQTT, actualización de firmware por web con
validación de integridad (binario firmado) y backup/restore de la configuración.

## Estado de la rama

La rama `saving_and_restoring_config` es la más adelantada del proyecto. Será la
definitiva cuando supere las pruebas funcionales.

## Estructura del directorio

```
RadProt_IOT/
├── pico-sdk/                 # SDK de Raspberry Pi Pico (no versionado, clon o symlink)
├── cmake/                    # FreeRTOS_Kernel_import.cmake, StaticAnalyzers.cmake
├── src/                      # Aplicación: main, post, settings, MQTT, firmware_update...
├── hal/                      # Drivers: serial, spi, gpio
├── enc28j60/                 # Driver de Ethernet ENC28J60
├── httpserver/               # Servidor web (httpd), sistema de archivos y makefsdata
├── inc/                      # Cabeceras compartidas (p. ej. firmware_common.h)
├── lwip_patch/               # Parche lwIP (IP secundaria)
├── paho.mqtt.embedded-c/     # Cliente MQTT (vendored)
├── dhcpserver/  dns/         # Servidores DHCP y DNS
├── patch_lwip.sh             # Aplica el parche de lwIP al SDK
└── bump_version.sh           # Incrementa la versión del firmware
```

## Herramientas requeridas (Debian/Ubuntu)

```bash
sudo apt install build-essential cmake git python3 \
                 gcc-arm-none-eabi libnewlib-arm-none-eabi \
                 libstdc++-arm-none-eabi-newlib
```

Opcionales (depuración y flasheo con debugger):

```bash
sudo apt install openocd gdb-multiarch picotool
```

## Descarga e instalación de dependencias

El proyecto espera el SDK dentro del propio repositorio (la ruta está en
`CMakeLists.txt`: `set(PICO_SDK_PATH ./pico-sdk)`, y `pico-sdk/` está en
`.gitignore`).

```bash
cd RadProt_IOT

# 1) pico-sdk dentro del repo (clon completo, incluye lwIP como submódulo):
#    Requiere pico-sdk 2.x o superior (el código usa APIs de 2.x, p. ej.
#    pico/platform/sections.h e irq_num_t; las versiones 1.x no compilan).
git clone --recursive https://github.com/raspberrypi/pico-sdk
#    o, si ya tenés un SDK instalado, un symlink:
#    ln -s <ruta-al-sdk> pico-sdk

# 2) FreeRTOS-Kernel: CMakeLists.txt lo busca en ../../FreeRTOS-Kernel
#    (dos niveles arriba del repo). Ej. con el repo en /home/<user>/pico/RadProt_IOT,
#    debe existir /home/<user>/FreeRTOS-Kernel. Si está en otra ruta, ajustá
#    FREERTOS_KERNEL_PATH en CMakeLists.txt o creá un symlink.
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel ../../FreeRTOS-Kernel

# 3) Aplicar el parche de lwIP (agrega soporte de IP secundaria):
./patch_lwip.sh
```

ArduinoJson y ETL no se descargan a mano: se obtienen automáticamente con
`FetchContent` (GIT_REPOSITORY) durante el primer `cmake`.

## Compilación

```bash
# 1) Herramienta host makefsdata (convierte httpserver/fs/ en fsdata.c).
#    Debe compilarse antes del firmware principal:
cmake -S httpserver/makefsdata -B httpserver/makefsdata/build
cmake --build httpserver/makefsdata/build

# 2) Firmware principal:
cmake -S . -B build
cmake --build build -j$(nproc)
```

Artefactos generados en `build/`:

| Archivo                            | Descripción                                   |
|------------------------------------|-----------------------------------------------|
| `RadProt_IoT.elf` / `.uf2` / `.bin`| Firmware estándar                             |
| `RadProt_IoT-v1.2.4-signed.bin`    | Binario firmado (para actualización por web)  |

La versión del binario firmado depende de `FIRMWARE_VERSION` en `CMakeLists.txt`.

## Versionado

```bash
./bump_version.sh {major|minor|patch}
```

El script edita `set(FIRMWARE_VERSION "vX.Y.Z")` en `CMakeLists.txt`, que es la
única fuente de verdad. El binario firmado refleja la versión:
`RadProt_IoT-vX.Y.Z-signed.bin`.

## Carga del firmware

Primera carga:

- **BOOTSEL + UF2:** mantener el botón BOOT mientras se conecta por USB y copiar
  `build/RadProt_IoT.uf2` al volumen que aparece.
- **OpenOCD + GDB:** depuración paso a paso (configuración lista para VS Code con
  cortex-debug).

Actualizaciones posteriores:

- Desde la interfaz web (`/firmware_upload.cgi`): se sube el binario firmado
  (`RadProt_IoT-vX.Y.Z-signed.bin`). El firmware valida la cabecera (magic `WUPR`,
  versión y CRC-32 del payload) antes de escribir la flash, por lo que un binario
  corrupto o no firmado se rechaza sin tocar el firmware vigente.

## Firmware firmado (sign_firmware)

El POST_BUILD invoca la herramienta host `build/sign_firmware`, compilada desde
`src/sign_firmware.cpp`, que antepone una cabecera de 16 bytes al `.bin`:

| Campo          | Tamaño | Contenido                                   |
|----------------|--------|---------------------------------------------|
| `magic`        | 4 B    | `0x50554657` ("WUPR")                       |
| `version`      | 4 B    | 1                                           |
| `payload_size` | 4 B    | Tamaño del firmware                         |
| `payload_crc32`| 4 B    | CRC-32 del firmware (semilla 0)             |

La definición de la cabecera es compartida entre la herramienta y el dispositivo
en `inc/firmware_common.h`.
