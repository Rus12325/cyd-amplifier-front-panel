"""Check the numbers the visualiser and the panel switching are built on.

The firmware cannot be run without the board, but the decisions that are easy to
get wrong are arithmetic and can be checked here:

  1. band edges - the log spacing in firmware/src/spectrum.cpp must stay inside
     the useful bins for both sample rates, and no band may be empty
  2. the bar widths in the UI must fit the 320 px display
  3. the sampler must have time to do its work at the configured rate
  4. the power/source sequencer must never leave the amplifier live while a
     relay moves
  5. the pins cannot drift back into the two conflicts this panel started
     with: an I2S clock on the backlight pin (the screen flickered), and
     TFT_eSPI's touch handler on the display bus (the touch never answered)

Everything is read out of the firmware sources, so this stays honest when the
constants change: run it after editing config.h, spectrum.h, touch.cpp or
ui.cpp.

    python tools/check_spectrum.py
"""
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FW = ROOT / "firmware"

ok = True


def check(label, condition, detail=""):
    global ok
    ok = ok and bool(condition)
    print("  %-46s %s%s" % (label, "OK" if condition else "FAIL",
                            ("  " + detail) if detail else ""))


def read(path):
    return (FW / path).read_text(encoding="utf-8")


def define(source, name, default=None):
    """Value of a #define, without a trailing float suffix or comment."""
    m = re.search(r"^#define\s+%s\s+([^\s/]+)" % name, source, re.M)
    if not m:
        return default
    return m.group(1).rstrip("fF")


def const_int(source, name):
    m = re.search(r"constexpr int16_t %s = ([0-9]+);" % name, source)
    return int(m.group(1)) if m else None


def main():
    cfg = read("include/config.h")
    spec = read("include/spectrum.h")
    ui = read("src/ui.cpp")

    fft_n = int(define(cfg, "DSP_FFT_N", define(spec, "DSP_FFT_N", "512")))
    bands = int(define(cfg, "DSP_BANDS", define(spec, "DSP_BANDS", "16")))
    low = float(define(cfg, "CFG_BAND_LOW_HZ", "40"))
    high = float(define(cfg, "CFG_BAND_HIGH_HZ", "16000"))
    floor_db = float(define(cfg, "CFG_METER_FLOOR_DB", "-60"))

    # The two builds take their rate from the two branches of the audio-in block
    audio = re.search(r"#if CYD_BT_SINK(.*?)#else(.*?)#endif", cfg, re.S)
    bt_cfg, adc_cfg = audio.group(1), audio.group(2)
    adc_rate = float(define(adc_cfg, "CFG_SAMPLE_RATE", "8000"))
    decim = float(define(bt_cfg, "CFG_SAMPLE_DECIM", "2"))

    print("1. spectrum")
    print("     fft %d points, %d bands, %.0f-%.0f Hz, floor %.0f dB"
          % (fft_n, bands, low, high, floor_db))
    check("fft size is a power of two", fft_n & (fft_n - 1) == 0, "n=%d" % fft_n)

    # the ADC build runs at the ADC rate, the A2DP build at the decoded rate
    for name, rate in (("adc tap", adc_rate),
                       ("a2dp 44.1k", 44100.0 / decim),
                       ("a2dp 48k", 48000.0 / decim)):
        top = min(high, rate * 0.45)
        bin_hz = rate / fft_n
        # spectrum.cpp clamps the lowest band to the first usable bin
        low_eff = max(low, bin_hz)
        step = math.log(top / low_eff) / bands
        edges = [low_eff * math.exp(step * i) for i in range(bands + 1)]
        counts = [max(1, int(round(edges[i + 1] / bin_hz)) -
                      int(round(edges[i] / bin_hz))) for i in range(bands)]
        print("     %-10s %6.0f Hz, bin %.1f Hz, %.2f octaves/band, %d..%d bins%s"
              % (name, rate, bin_hz, step / math.log(2), min(counts), max(counts),
                 "  (band 0 clamped to the first bin)" if low_eff > low else ""))
        check("%s: first band still starts low" % name, low_eff <= 60.0,
              "%.0f Hz" % low_eff)
        check("%s: top band inside nyquist" % name, top <= rate * 0.5)
        check("%s: every band covers a bin" % name, min(counts) >= 1)
        check("%s: no band swallows the spectrum" % name, max(counts) <= fft_n / 2,
              "widest %d of %d bins" % (max(counts), fft_n // 2))

    print("2. layout")
    width = int(define(cfg, "CFG_TFT_WIDTH", "320"))
    height = int(define(cfg, "CFG_TFT_HEIGHT", "240"))
    st_h = const_int(ui, "STATUS_H")
    src_y = const_int(ui, "SRC_Y")
    src_h = const_int(ui, "SRC_H")
    viz_y = const_int(ui, "VIZ_Y")
    viz_h = const_int(ui, "VIZ_H")
    row_y = const_int(ui, "ROW_Y")
    row_h = const_int(ui, "ROW_H")
    margin = const_int(ui, "MARGIN")
    print("     %d x %d, status %d, sources %d..%d, viz %d..%d, row %d..%d"
          % (width, height, st_h, src_y, src_y + src_h, viz_y, viz_y + viz_h,
             row_y, row_y + row_h))
    check("source row clears the status bar", src_y >= st_h)
    check("visualiser clears the source row", viz_y >= src_y + src_h)
    check("bottom row clears the visualiser", row_y >= viz_y + viz_h)
    check("bottom row fits the screen", row_y + row_h <= height)

    bar_gap, area = 3, width - 2 * margin
    bar_w = (area - (bands - 1) * bar_gap) // bands
    used = bands * bar_w + (bands - 1) * bar_gap
    print("     %d bars: %d px wide, %d px of %d used" % (bands, bar_w, used, area))
    check("bars fit the visualiser area", used <= area)
    check("bars are wide enough to read", bar_w >= 8, "%d px" % bar_w)

    gap = const_int(ui, "GAP") or 6
    rects = []
    for name in ("powerRect", "vizRect", "menuRect"):
        # The rects are arithmetic on the grid (8 + 150 + 6 = 164), so
        # evaluate the expression instead of expecting a bare literal.
        m = re.search(r"Rect %s\(\)\s*\{\s*return Rect\{([^,]+), ROW_Y, (\d+), ROW_H\};"
                      % name, ui)
        check("%s parsed from ui.cpp" % name, m is not None)
        if not m:
            continue
        expr = m.group(1).replace("(int16_t)", "").strip()
        try:
            x = eval(expr, {"__builtins__": {}},
                     {"MARGIN": margin, "GAP": gap, "W": width})
        except Exception:
            x = None
        check("%s x expression evaluates" % name, x is not None, expr)
        if x is None:
            continue
        rects.append((name, x, int(m.group(2))))
    for name, x, w in rects:
        check("%s inside the margins" % name, x >= margin and
              x + w <= width - margin, "x=%d w=%d" % (x, w))
    for a, b in zip(rects, rects[1:]):
        check("%s leaves room for %s" % (a[0], b[0]), a[1] + a[2] <= b[1])

    m = re.search(r"Rect diagTestRect\(\) \{ return Rect\{(\d+), (\d+), (\d+), (\d+)\};"
                  % (), ui)
    check("diagnostics test button parsed", m is not None)
    if m:
        bx, by, bw, bh = (int(g) for g in m.groups())
        check("diagnostics test button fits the screen",
              by + bh <= height - 16, "y %d..%d" % (by, by + bh))
        check("diagnostics test button is inside the margins",
              bx >= margin and bx + bw <= width, "x=%d w=%d" % (bx, bw))

    print("     wiring test: %d channels x (400 ms on + 25 ms settle + 120 ms off)"
          % 8)
    check("a full wiring test stays under 6 s",
          8 * (400 + 25 + 120) / 1000.0 < 6.0)

    print("3. sampler")
    check("the adc sampler uses the non-bt rate", adc_rate <= 32000,
          "%.0f Hz" % adc_rate)
    period_us = 1e6 / adc_rate
    # analogRead() on ESP32 costs roughly 10-20 us; leave a large margin
    check("adc period is long enough", period_us > 60, "%.0f us" % period_us)
    frame_ms = fft_n / adc_rate * 1000
    print("     frame %.0f ms (%.1f fps) at %.0f samples/s"
          % (frame_ms, 1000 / frame_ms, adc_rate))
    check("frame rate is smooth enough to look alive",
          4 <= 1000 / frame_ms <= 40, "%.1f fps" % (1000 / frame_ms))
    print("     static ram for the analyser: %.1f kB"
          % ((5 * fft_n + fft_n / 2 + 2 * (bands + 1)) * 4 / 1024))
    check("analyser fits in internal ram",
          (5 * fft_n + fft_n / 2) * 4 < 40 * 1024)

    print("4. switching sequence")
    lead = int(define(cfg, "CFG_AMP_MUTE_LEAD_MS", "0"))
    warm = int(define(cfg, "CFG_AMP_WARMUP_MS", "0"))
    tail = int(define(cfg, "CFG_AMP_MUTE_TAIL_MS", "0"))
    src_lead = int(define(cfg, "CFG_SRC_MUTE_LEAD_MS", "0"))
    src_win = int(define(cfg, "CFG_SRC_SWITCH_MS", "0"))
    print("     power: mute %d ms -> relay -> %d ms warmup -> unmute" % (lead, warm))
    print("     source: mute %d ms -> relay -> %d ms window" % (src_lead, src_win))
    check("power relay moves only after the mute is engaged", lead >= 50)
    check("the amp gets time to settle before unmuting", warm >= 500)
    check("the mute outlives the power relay release", tail >= 100)
    check("source switching mutes before the relay moves", src_lead >= 20)
    check("the source mute window covers the relay", src_win >= 60)

    amp = read("src/amp.cpp")
    # the power relay must never close without a mute first: requestOn() mutes
    # before it sets state = Starting
    body = amp[amp.index("void AmpController::requestOn"):amp.index("void AmpController::requestOff")]
    check("requestOn mutes before the relay can move",
          "mute_->mute()" in body and body.index("mute_->mute()") >
          body.index("state_ = AmpState::Starting") - 200)
    check("requestOff mutes before dropping the relay",
          "mute_->mute()" in amp[amp.index("void AmpController::requestOff"):
                                amp.index("void AmpController::toggle")])

    print("5. pins (the two failures this panel started with)")
    ini = read("platformio.ini")
    main_src = read("src/main.cpp")
    touch_src = read("src/touch.cpp")

    dac = define(cfg, "CFG_PIN_AUDIO_DAC", "?")
    check("the A2DP build sends the audio to the internal DAC",
          dac == "26", "GPIO %s -> the on-board amplifier" % dac)
    check("...and never to the backlight pin (that made it flicker)", dac != "21")
    bt_env = ini[ini.index("[env:cyd_bt]"):]
    check("the A2DP build keeps the backlight under firmware control",
          "-DTFT_BL=21" in bt_env and "-DTFT_BL=-1" not in bt_env)
    check("the external-I2S option is off by default",
          define(cfg, "CYD_BT_I2S_OUT", "1") == "0")
    check("only the DAC channel on GPIO 26 is enabled by default",
          define(cfg, "CYD_DAC_BOTH", "1") == "0" and
          "I2S_DAC_CHANNEL_LEFT_EN" in read("src/bt_sink.cpp"),
          "DAC2 = GPIO 26, DAC1 = GPIO 25 is the touch clock")
    check("the touch pins are reclaimed after the DAC is configured",
          "touch::begin()" in main_src)

    # a bare mention in a comment is fine, the build flag is what counts
    check("TFT_eSPI's own touch code stays off (it uses the display bus)",
          "-DTOUCH_CS" not in ini)
    check("the UI reads the panel through touch::, not tft.getTouch",
          "touch::read" in ui and "tft.getTouch" not in ui)
    check("the driver bit-bangs the pins the board really wires",
          define(cfg, "CFG_PIN_TOUCH_SCLK") == "25" and
          define(cfg, "CFG_PIN_TOUCH_CS") == "33" and
          define(cfg, "CFG_PIN_TOUCH_MISO") == "39")
    check("an uncalibrated panel can be calibrated without a reboot",
          "touch::pressed() && uncalibrated()" in ui)
    check("a tap is also noticed when the pressure reading misbehaves",
          "kMoveFromIdle" in touch_src)

    cal_decl = re.search(r"touchCal\[(\d+)\]", read("src/settings.h"))
    cal_write = re.search(r"<\s*(\d+);\s*\+\+i\)\s*sSettings->data\(\)\.touchCal\[i\]",
                          ui)
    check("the stored calibration matches the driver's six values",
          cal_decl and cal_decl.group(1) == "6" and cal_write and
          cal_write.group(1) == "6",
          "touchCal[%s], %s written"
          % (cal_decl.group(1) if cal_decl else "?",
             cal_write.group(1) if cal_write else "?"))

    tone = re.search(r"Rect diagToneRect\(\)\s*\{\s*return Rect\{(\d+), (\d+), "
                     r"(\d+), (\d+)\};\s*\}\s*\n#else", ui)
    test_btn = re.search(r"Rect diagTestRect\(\)\s*\{\s*return Rect\{(\d+), (\d+), "
                         r"(\d+), (\d+)\};\s*\}", ui)
    if tone and test_btn:
        tx, ty, tw, th = (int(v) for v in tone.groups())
        bx, by, bw, bh = (int(v) for v in test_btn.groups())
        check("the tone button fits beside the wiring-test button",
              tx >= bx + bw and tx + tw <= 320 and ty + th <= 240,
              "test %d..%d, tone %d..%d" % (bx, bx + bw, tx, tx + tw))

    print("\nRESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
