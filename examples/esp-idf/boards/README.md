# Local board definitions

These source YAML files preserve the product board definition from the primary
repository. They are separate from downloaded components and generated code.

From any Board Manager example directory:

```bash
idf.py bmgr -l -c ../boards
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9 -c ../boards
idf.py build
```

Install `esp-bmgr-assist==0.8.3` in the active ESP-IDF Python environment first.
The helper resolves the registry dependencies on a fresh checkout. The registry
package provides the Board Manager runtime; the local definition supplies this
product until it is available in the published board package. Generated
`components/gen_bmgr_codes` files are ignored and must not be committed.
Example `07_sd_card` uses SDMMC directly and does not need Board Manager.
