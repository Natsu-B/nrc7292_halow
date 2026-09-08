# Native NRC7292 HSPI reference sources

Input: user-uploaded `nrc7292_sdk-master.zip`, package/standalone/VERSION-SDK 1.7.0.
Upstream: https://github.com/newracom/nrc7292_sdk

`host/nrc-hspi.c,h` are exact copies from
`package/standalone/atcmd/host/raspi-atcmd-cli/`.
`target/hif.c,hif.h,hif_fifo.c,hif_hspi.c,hif_dma.h` are exact copies from
`package/standalone/sdk/apps/atcmd/`.

The MIT notices in those files are retained. SHA256.json records the exact bytes.
The SDK-preparation script copies the target files unchanged and checks that the
provided SDK has those same versions. It does not copy the AT command parser.

The active host driver in components/nrc7292/hspi is a **derived port**, not an
unchanged upstream file. It corrects zero-credit handling, adds bounded startup,
checks malformed slot/status fields, tracks transport diagnostics and rejects
uncertain queue-write replay. Its command encoding and reference transaction
boundaries remain native HSPI. See docs/NATIVE_HSPI_ja.md.

No Silex BDF, Silex confidential PDF, binary modem library, toolchain or flash tool
is redistributed in this project. Those remain separate user-provided inputs.

Archive provenance (observed locally, not inferred from the current master branch):
- SDK archive SHA256: 8d0a8d76d2a66dd722ca93fe6dba1d379b9ec3cd4a9b0e857b7c28a5c1971bbd
- GitHub ZIP comment: f10910b541f070e8a636a891efc0895fe39cfb1f
- Baseline v0.7.1 ZIP SHA256: 2b8a95cae235e543ba77d24e13027975354854d61e0bb2b9c1961369f3ee83cb

Prefer the supplied SDK archive; if obtaining it from GitHub, use that recorded
revision and verify the native file hashes. Do not silently substitute master.
