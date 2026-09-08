# Source basis / version record

## User-supplied files

- `nrc7292_sdk-master.zip`: VERSION-SDK=1.7.0. API prototypes and ATCMD HSPI target support are taken from this archive.
- `sx-newah_driver-v134rev30_20240312.tar.gz`: Silex package. Its `nrc_pkg/boarddata/nrc7292_bd.dat` was used only to exercise the SDK preparation script; SHA256 c4550702d2409dbdc1b2ce6e0f81a77840d7de7887def0359a8cdb4e8d9eac41. No target firmware or RF test was built from it in this turn. SDK1.7 compatibility is not proven.
- `silex_SX-NEWAH_JP_DataSheet_DS202312.pdf`: p15–16 pin signals, p20 HSPI timing, p21 antenna/certification conditions.
- `WA107280XD_SX-NEWAH-EVK-JP_Startup.pdf`: p20/p24 JP duty/LBT examples; p30 AP/BR-100AH interoperability and SAE.
- `WA107660XA_SX-NEWAH-EVK_Standalone_Start-UP_Guide.pdf`: p4 UART sample scope; p5–8 DIP/FlashTool/factory MAC cautions.
- Prior v0.6 generated code: HSPI transport, L2 protocol and NRCV packetizer were reviewed and reused with corrections. Prior build/test claims are not inherited as evidence for this release.

## New implementation decisions

HVC1 storage, Electron application, replay scheduling, IPC backpressure, fixed safe JP bench configuration and SDK overlay behavior are this project's design choices, not Silex vendor specifications.

## External primary documentation used for APIs

- ESP-IDF v5.5.2, ESP32-S3 SPI Master and ESP-NETIF I/O driver APIs.
  https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/spi_master.html
  https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/network/esp_netif_driver.html
- Official IDF source `components/esp_netif/lwip/esp_netif_lwip.c` v5.5.2. RX buffer ownership must follow the selected Ethernet input path; transferred buffers are not freed again by the caller.
- Electron release44.2.0 and official context isolation/sandbox/security documentation.
  https://www.electronjs.org/docs/latest/tutorial/security
  https://www.electronjs.org/docs/latest/tutorial/context-isolation
- electron-builder portable Windows packaging.
  https://www.electron.build/win.html
- WebCodecs VideoDecoder and EncodedVideoChunk APIs. H264 decoder availability is checked at runtime. The actual Electron package was NOT executed here.

Top-level versions are pinned, but npm transitive dependency lockfile is not generated because package installation was unavailable. A successful developer build must capture it for reproducibility.

v0.8.0: exact native HSPI source copies and their SHA256 values are now vendored
under `third_party/newracom/`. See its README for SDK paths and licensing.
Active host code is a derived port with documented fixes, not an unchanged copy.
