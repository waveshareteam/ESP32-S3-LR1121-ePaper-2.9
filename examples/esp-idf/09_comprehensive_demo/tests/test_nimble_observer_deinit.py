"""Host regression for the SDK RX queue lifetime; run after idf.py build.

Uses the actual SDK and generated initialization/teardown statements, with a
queue mock that rejects teardown before initialization. No board is required.
"""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
SDK_SOURCE = (Path(os.environ["IDF_PATH"]) / "components/bt/host/nimble/nimble/"
              "nimble/host/src/ble_hs.c")
BUILD_SOURCE = PROJECT / "build/nimble_compat/ble_hs.c"


def run_lifecycle(source, connect):
    init = re.search(
        r"#if NIMBLE_BLE_CONNECT\s+ble_mqueue_init\(&ble_hs_rx_q,.*?\n#endif",
        source, re.DOTALL,
    )
    if init is None:
        raise AssertionError("SDK queue initialization changed; review regression")
    deinit = source.split("void\nble_hs_deinit(void)", 1)[1]
    deinit = deinit[deinit.index("    ble_hs_flow_deinit();"):]
    deinit = deinit.split("    ble_hs_stop_deinit();", 1)[0]
    harness = r"""
#include <stdlib.h>
#define BLE_MONITOR 0
static int ble_hs_rx_q, initialized, released;
static void *ble_hs_event_rx_data;
static void ble_hs_flow_deinit(void) {}
static void ble_mqueue_init(int *q, void *fn, void *arg) {
    (void)fn; (void)arg;
    if (*q) exit(22);
    *q = 1;
    ++initialized;
}
static void ble_mqueue_deinit(int *q) {
    if (!*q) exit(23); /* The SDK dereferences a NULL event here. */
    *q = 0;
    ++released;
}
int main(void) {
    for (int cycle = 0; cycle < 100; ++cycle) {
INIT
DEINIT
    }
    return initialized != released || released != 100 * NIMBLE_BLE_CONNECT;
}
""".replace("INIT\nDEINIT", init.group(0) + "\n" + deinit)
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "lifecycle.c"
        exe = Path(tmp) / "lifecycle"
        src.write_text(harness)
        subprocess.run(["cc", "-std=c11", f"-DNIMBLE_BLE_CONNECT={connect}",
                        str(src), "-o", str(exe)], check=True)
        return subprocess.run([str(exe)], check=False).returncode


class QueueLifecycleTest(unittest.TestCase):
    def test_sdk_observer_bug_or_upstream_fix(self):
        source = SDK_SOURCE.read_text()
        guarded = "#if NIMBLE_BLE_CONNECT\n    ble_mqueue_deinit(&ble_hs_rx_q);\n#endif"
        self.assertEqual(run_lifecycle(source, 0), 0 if guarded in source else 23)

    def test_observer_repeated_start_stop(self):
        source = BUILD_SOURCE if BUILD_SOURCE.exists() else SDK_SOURCE
        self.assertEqual(run_lifecycle(source.read_text(), 0), 0)

    def test_connection_queue_still_released(self):
        source = BUILD_SOURCE if BUILD_SOURCE.exists() else SDK_SOURCE
        self.assertEqual(run_lifecycle(source.read_text(), 1), 0)


if __name__ == "__main__":
    unittest.main()
