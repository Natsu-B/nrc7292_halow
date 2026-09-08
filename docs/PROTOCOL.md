# Wire and storage interfaces

These are application-specific interfaces, not a Newracom or Silex supported public product protocol. Both ends must use the same definitions. AP-100AH only bridges normal Ethernet/IP traffic and does not parse these payloads.

## HVC1 video.bin storage

All integers little-endian. Header32: magic HVC1 at0; version u16=1 at4; headerbytes u16=32 at6; codec u8 at8 (1 AnnexB H264,2 JPEG); flags u8=0 at9; width u16 at10; height u16 at12; reserved u16=0 at14; fps numerator u16 at16; denominator u16 at18; framecount u32 at20; duration_ms u32 at24; CRC32(bytes0..27) at28.

Each record has16bytes: payload size u32; PTS_ms u32; flags u32 (bit0 independently decodable/key); CRC32(payload) u32. Then the compressed access unit. One record is one frame/access unit, not one NAL. Limits:256KiB/access unit;1920x1088;100000frames. Actual demo uses640x360. First record must be key. H264 is AnnexB, no B-frames, AUD delimiter and repeated SPS/PPS with IDR. The clip parser does not rescale, transcode or synthesize missing headers.

## NRCV video UDP

Port50000 by default. Big-endian. Datagram128–1400bytes,36byte header. V2 is emitted; receiver also accepts V1 fixed1400. Header0:magicNRCV,4:version,5:codec,6:flags,7:36,8:packet_seq u32,12:stream_id u32,16:frame_id u32,20:timestamp_ms u32,24:frame_bytes u32,28:fragment_index u16,30:fragment_count u16,32:payload_length u16,34:datagram_bytes u16. V1 uses0 at34.

Flags:bit0key,bit1first,bit2last,bit3padding. Payload starts36. Each non-final fragment uses datagram_bytes-36 bytes. Last fragment is padded to the declared datagram length. Padding is not decoder data.

The PC bounds in-flight frames (8) and frame allocation (2MiB). It suppresses duplicate sequence IDs, handles modulo-u32 sequence ordering, expires incomplete frames after2s, and drops dependent H264 frames until a keyframe after a reference gap. It has no RTP/RTSP/MP4 parsing. Sequence-gap loss is an estimate, not radio PER.

## NRCS telemetry UDP

Same destination port,40bytes, big-endian. 0:NRCS,4:version1,5:reserved0,6:length40 u16,8:sample_ms u32,12:RSSI i8,13:SNR i8,14:TX_MCS u8,15:RX_MCS u8,16:Duty_available u8,17:connected u8,18:host_queue_depth u16,20:host_drops u32,24:radio_drops u32,28:sent_frames u32,32:skipped_frames u32,36:CRC32 of first36bytes.

Emitted at most once/second only while playback requested, connected, native Duty available and queue not busy. This packet also consumes RF resources. Stale telemetry is not evidence of current link health. Physical RSSI/MCS values remain unvalidated without hardware.

## Feedback

Receiver optionally sends existing NRFB36byte feedback once/second to the source UDP port(50001 on ESP). Phase1 drains but does not use feedback for adaptive quality. This is intentionally not a claim that bitrate adaptation is implemented.

## ESP ↔ NRC HSPI L2

`components/nrc7292/include/nrc_l2_protocol.h` and `.c` are the source of truth and copied verbatim to `sx_newah/L2BRIDGE_HSPI`. Messages are typed and length/CRC bounded, with ETH_TX/ETH_RX, HELLO, configuration, connect/disconnect, results and periodic stats. No AT passthrough is used.

The target initially accepts no radio association. HELLO returns its factory MAC via SDK string API, parsed into6bytes. The feature HOST_OWNS_IP means user IP resides on ESP; it does NOT mean SDK lwIP initialization has been removed.

The bench target accepts only the fixed JP guard profile60,000,000/6,000,000/0µs and tx_power=-1 (retain vendor BDF), internal rate_control enabled. Identical configuration can be repeated without resetting Duty. Changing the guard is not supported in this bench release. This limitation must be considered when later integrating a flight scheduler; do not relabel this release as a finalized flight firmware.

Single-module/single-ESP instance is supported. Host boot handshake needs a matching custom L2 target, not the vendor UART sample or Linux CSPI target. HSPI/WPA/raw802.3 behavior in the closed modem libraries needs actual target-build/RF validation.

## v0.8.0 native-HSPI diagnostics extension

The NB record header and NRCV video formats are unchanged. Message 13 is
`LOOPBACK_REQ`, message 14 is `LOOPBACK_RSP`; HELLO feature bit9 advertises them.
Payload length is 1..1518 bytes, and the echo response repeats the request's
sequence and bytes. No WLAN association is required. This extension is carried
inside the byte stream provided by native HSPI slots; a native slot is not an
Ethernet packet boundary.
