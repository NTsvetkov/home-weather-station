"""Bound AHTX0 busy waits; fail closed if the upstream implementation changes."""
from pathlib import Path

Import("env")

source_path = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "Adafruit AHTX0" / "Adafruit_AHTX0.cpp"
source = source_path.read_text(encoding="utf-8")
marker = "// Weather station: bounded AHT busy wait"
original = """  while (getStatus() & AHTX0_STATUS_BUSY) {
    delay(10);
  }"""
replacement = """  {
    // Weather station: bounded AHT busy wait
    const uint32_t busyStart = millis();
    while (getStatus() & AHTX0_STATUS_BUSY) {
      if (millis() - busyStart >= 1000UL) return false;
      delay(10);
    }
  }"""
if marker not in source:
    if source.count(original) != 3:
        raise RuntimeError("AHTX0 busy-wait implementation changed; review timeout patch before building")
    source_path.write_text(source.replace(original, replacement), encoding="utf-8")
    print("  [patch_aht_timeout] Bounded all three AHTX0 busy waits")
elif source.count(marker) != 3 or source.count(original):
    raise RuntimeError("Incomplete AHTX0 timeout patch")
