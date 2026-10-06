"""Draw the CYD-to-5.1-amplifier wiring as a self-contained HTML page.

The picture is generated from the tables below, so the diagram and the netlist
under it cannot drift apart.

    python tools/wiring_diagram.py [out.html]

Default output is docs/wiring-netlist.html.  It deliberately refuses to write
wiring_cyd_amp.html: that file is the hand-written interactive guide, and an
earlier version of this script used to overwrite it.
"""
import html
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = (pathlib.Path(sys.argv[1]) if len(sys.argv) > 1
       else ROOT / "docs" / "wiring-netlist.html")

BG = "#111"
FG = "#ddd"
DIM = "#8a8a8a"
WIRE = "#e05252"
PWR = "#e0a020"
AUDIO = "#3fd0d0"
GND = "#9aa0a6"
BOX = "#1b1d21"
EDGE = "#4a4f57"

# --------------------------------------------------------------------- CYD pins
# Everything the board already uses, and the four pins this build lives on.
PINS = [
    ("13 / 12 / 14 / 15 / 2", "дисплей ILI9341: MOSI / MISO / SCLK / CS / DC",
     "занят"),
    ("21", "подсветка (ШИМ из прошивки)", "занят"),
    ("32 / 39 / 25 / 33 / 36", "тач XPT2046", "занят"),
    ("5 / 18 / 19 / 23", "microSD", "занят"),
    ("4 / 16 / 17", "RGB-светодиод; 16 занят mute-реле, светодиод без зелёного",
     "занят"),
    ("1 / 3", "USB-UART", "занят"),
    ("26", "внутренний ЦАП -> встроенный УНЧ 3 Вт (только сборка cyd_bt)",
     "условно"),
    ("27", "реле K1: шина 24 В на три платы", "наш"),
    ("16", "реле K2: mute, шесть входных линий на звезду GND", "наш"),
    ("22", "свободный ключ: питание тыла+саба, вентилятор, RGB", "наш"),
    ("35", "съём звука для визуализатора, только вход, ADC1_7", "наш"),
]

# ------------------------------------------------------------------- netlist
# Everything drawn in the picture, as (net, from, to, note).
NETS = [
    ("+24 В", "БП 24 В / 4 А (96 Вт)",
     "K1 -> NTC -> три платы, предохранитель 5 А до разветвлений",
     "общая шина, ~3 А на средней громкости"),
    ("+5 В", "DC-DC 24->5 В на 2-3 А",
     "катушки K1 и K2, CYD, MAX98357A (если понадобится)",
     "не от 5 В-пина CYD: катушка это 30-70 мА"),
    ("GND", "звезда на клемме БП",
     "три платы, DC-DC, ключи, CYD, аудиоземля ПК",
     "один общий провод, петля ПК<->усилитель - главный источник гула"),
    ("GPIO27", "CYD GPIO27", "Q1: база через 1 кОм, диод 1N4148 на катушке",
     "шина 24 В: реле K1 или MOSFET"),
    ("GPIO16", "CYD GPIO16", "Q2: база через 1 кОм, диод 1N4148 на катушке",
     "mute: контакт K2 замыкает шесть линий на землю"),
    ("GPIO22", "CYD GPIO22", "свободный ключ, прошивкой пока не переключается",
     "резерв: режимы, вентилятор, светодиод"),
    ("AUDIO", "ПК: джеки зелёный / чёрный / оранжевый",
     "входы AMP-1 / AMP-2 / AMP-3 напрямую, без реле и без РГ",
     "панель в звук не вмешивается, громкость в Windows"),
    ("AUDIO_TAP", "линия фронт-левого входа",
     "GPIO35: 10к последовательно, 10к на GND, 10к на 3V3, 100 нФ",
     "делитель и смещение 1.65 В"),
    ("SPK", "выходы трёх плат", "FL / FR, RL / RR, C / SW",
     "выходы мостовые: минус динамика не соединять с землёй"),
]


def esc(text):
    return html.escape(str(text))


def text(x, y, s, size=13, color=FG, anchor="start", weight="normal"):
    return ('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="%s" '
            'font-weight="%s" font-family="system-ui, sans-serif">%s</text>'
            % (x, y, size, color, anchor, weight, esc(s)))


def box(x, y, w, h, title, subs=(), edge=EDGE, fill=BOX, title_color="#fff"):
    out = ['<rect x="%g" y="%g" width="%g" height="%g" rx="8" fill="%s" '
           'stroke="%s" stroke-width="1.5"/>' % (x, y, w, h, fill, edge)]
    out.append(text(x + 10, y + 20, title, 14, title_color, weight="bold"))
    for i, sub in enumerate(subs):
        out.append(text(x + 10, y + 38 + i * 15, sub, 12, DIM))
    return "".join(out)


def wire(points, color=WIRE, width=2, dash=None, arrow=True):
    pts = " ".join("%g,%g" % p for p in points)
    dash_attr = ' stroke-dasharray="%s"' % dash if dash else ""
    marker = ' marker-end="url(#arrow_%s)"' % color.lstrip("#") if arrow else ""
    return ('<polyline points="%s" fill="none" stroke="%s" stroke-width="%g"%s%s/>'
            % (pts, color, width, dash_attr, marker))


def label(x, y, s, color=DIM, size=12, anchor="middle"):
    return text(x, y, s, size, color, anchor)


def res_h(x, y, w, name):
    """a resistor lying on the horizontal line y"""
    return ('<rect x="%g" y="%g" width="%g" height="14" rx="3" fill="#2a2d33" '
            'stroke="%s"/>' % (x, y - 7, w, EDGE)
            + text(x + w / 2, y + 4, name, 11, FG, anchor="middle"))


def defs():
    """one arrow marker per colour used on the wires"""
    out = ["<defs>"]
    for col in (WIRE, PWR, AUDIO, GND):
        out.append('<marker id="arrow_%s" viewBox="0 0 10 10" refX="9" refY="5"'
                   ' markerWidth="6" markerHeight="6" orient="auto-start-reverse">'
                   '<path d="M 0 0 L 10 5 L 0 10 z" fill="%s"/></marker>'
                   % (col.lstrip("#"), col))
    out.append("</defs>")
    return "".join(out)


def signal_panel():
    """the 5.1 signal path, left to right"""
    s = [defs(), text(20, 30, "Сигнальный тракт 5.1", 16, FG, weight="bold")]
    s.append(box(20, 60, 150, 120, "ПК",
                 ["3 аналоговых выхода:", "зелёный: FL / FR",
                  "чёрный: RL / RR", "оранжевый: C / SW"], edge=AUDIO))

    amps = [("AMP-1 · Фронт", 40, "FL / FR"), ("AMP-2 · Тыл", 170, "RL / RR"),
            ("AMP-3 · Центр + Саб", 300, "C / SW")]
    for title, y, chans in amps:
        s.append(box(270, y, 190, 80, title,
                     ["стерео, общая шина 24 В", chans], edge=AUDIO))
    s.append(wire([(170, 90), (265, 90)], AUDIO))
    s.append(wire([(170, 135), (265, 225)], AUDIO))
    s.append(wire([(170, 165), (265, 355)], AUDIO))

    speakers = [("FL", 45), ("FR", 85), ("RL", 175), ("RR", 215),
                ("C", 305), ("SW", 345)]
    for name, y in speakers:
        s.append('<rect x="560" y="%g" width="140" height="32" rx="6" fill="#12161c" '
                 'stroke="%s" stroke-width="1.2"/>' % (y, EDGE))
        s.append(text(630, y + 21, name, 13, FG, anchor="middle"))
    s.append(wire([(460, 90), (555, 61)], AUDIO, width=1.6))
    s.append(wire([(460, 105), (555, 101)], AUDIO, width=1.6))
    s.append(wire([(460, 220), (555, 191)], AUDIO, width=1.6))
    s.append(wire([(460, 235), (555, 231)], AUDIO, width=1.6))
    s.append(wire([(460, 350), (555, 321)], AUDIO, width=1.6))
    s.append(wire([(460, 360), (555, 361)], AUDIO, width=1.6))
    s.append(label(630, 400, "саб: динамик 4 Ом", DIM))

    # the tap hangs off the front-left line, before the amplifier input
    s.append('<circle cx="215" cy="90" r="4" fill="%s"/>' % AUDIO)
    s.append(wire([(215, 90), (215, 430), (230, 430)], PWR))
    s.append(box(20, 405, 205, 70, "делитель + смещение",
                 ["10к -> 10к/GND -> 10к/3V3", "100 нФ, вход на GPIO35"], edge=PWR))
    s.append(text(250, 430, "съём только с одной линии: на плате один АЦП-вход",
                  12, DIM))
    return "<svg viewBox='0 0 900 490' width='100%%'>%s</svg>" % "".join(s)


def control_panel():
    s = [defs(), text(20, 30, "Коммутация: K1 — шина 24 В, K2 — mute", 16, FG,
                      weight="bold")]
    s.append(box(20, 70, 170, 230, "CYD", [], edge=EDGE))
    pins = [("GPIO22", 110, PWR), ("GPIO27", 150, WIRE), ("GPIO16", 190, WIRE),
            ("GPIO35", 270, AUDIO), ("5 В", 300, PWR), ("GND", 320, GND)]
    for name, y, col in pins:
        s.append('<circle cx="190" cy="%g" r="4" fill="%s"/>' % (y, col))
        s.append(text(180, y + 4, name, 12, col, anchor="end"))

    s.append(res_h(230, 150, 46, "1к"))
    s.append(res_h(230, 190, 46, "1к"))
    s.append(wire([(190, 150), (228, 150)], WIRE, arrow=False))
    s.append(wire([(190, 190), (228, 190)], WIRE, arrow=False))
    s.append(wire([(276, 150), (320, 150)], WIRE))
    s.append(wire([(276, 190), (320, 190)], WIRE))

    s.append(box(320, 125, 130, 50, "Q1 2N2222", [], edge=WIRE))
    s.append(box(320, 165, 130, 50, "Q2 2N2222", [], edge=WIRE))
    s.append(wire([(450, 150), (520, 150)], WIRE))
    s.append(wire([(450, 190), (520, 190)], WIRE))

    s.append(box(520, 118, 190, 44, "K1 — катушка 5 В", [], edge=PWR))
    s.append(box(520, 168, 190, 44, "K2 — катушка 5 В", [], edge=PWR))
    s.append(box(520, 235, 190, 58, "+5 В от DC-DC", ["24->5 В, 2-3 А, отдельно",
                                                      "от 5 В-пина CYD"], edge=PWR))
    s.append(wire([(615, 235), (615, 212)], PWR))
    s.append(text(520, 310, "1N4148 — встречно-параллельно катушке;", 12, DIM))
    s.append(text(520, 326, "без диода выброс убьёт вывод CYD", 12, DIM))

    # the free key and the mute relay, as one-liners under the drawing
    s.append(wire([(190, 110), (190, 360), (780, 360)], PWR, arrow=False))
    s.append(text(200, 354, "GPIO22 — свободный ключ: пока не задействован, "
                            "обычный ключ как у K1/K2", 12, DIM))
    s.append(text(20, 380, "K1: +24 В БП -> контакты -> NTC -> шина трёх плат "
                           "(RC-снаббер 100 нФ + 100 Ом)", 12, DIM))
    s.append(text(20, 396, "K2: все шесть горячих входных линий -> контакт -> "
                           "звезда GND (mute без щелчка)", 12, DIM))
    s.append(wire([(190, 320), (190, 420), (800, 420)], GND, arrow=False))
    s.append(label(500, 438, "общий провод: три платы, DC-DC, реле, CYD, аудиоземля ПК",
                   GND))
    return "<svg viewBox='0 0 900 460' width='100%%'>%s</svg>" % "".join(s)


def pin_table():
    rows = []
    for pin, what, state in PINS:
        color = {"занят": DIM, "условно": PWR, "наш": "#7ee081"}[state]
        rows.append("<tr><td class=pin>%s</td><td>%s</td>"
                    "<td style='color:%s'>%s</td></tr>"
                    % (esc(pin), esc(what), color, esc(state)))
    return ("<table><thead><tr><th>вывод</th><th>что на нём</th>"
            "<th>доступность</th></tr></thead><tbody>%s</tbody></table>"
            % "".join(rows))


def net_table():
    rows = []
    for net, src, dst, note in NETS:
        rows.append("<tr><td class=net>%s</td><td>%s</td><td>%s</td>"
                    "<td>%s</td></tr>"
                    % (esc(net), esc(src), esc(dst), esc(note)))
    return ("<table><thead><tr><th>цепь</th><th>откуда</th><th>куда</th>"
            "<th>примечание</th></tr></thead><tbody>%s</tbody></table>"
            % "".join(rows))


def main():
    if OUT.name == "wiring_cyd_amp.html":
        print("refusing to overwrite the interactive guide (wiring_cyd_amp.html):\n"
              "use another output name, the default is docs/wiring-netlist.html")
        return 2

    page = """<!doctype html>
<meta charset="utf-8">
<title>CYD ESP32-2432S028 → усилитель 5.1: подключение</title>
<style>
 body{background:%(bg)s;color:%(fg)s;font:14px system-ui,sans-serif;margin:0;padding:24px}
 h1{font-size:20px;margin:0 0 4px}
 h2{font-size:16px;margin:28px 0 8px;color:#fff}
 p.lead{color:%(dim)s;margin:0 0 20px;max-width:74ch}
 .wrap{display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start}
 .panel{flex:1 1 420px;min-width:380px;background:#0c0d0f;border:1px solid %(edge)s;
        border-radius:10px;padding:12px}
 table{border-collapse:collapse;width:100%%;margin-top:8px}
 th,td{text-align:left;padding:6px 8px;border-bottom:1px solid #26282d;font-size:13px}
 th{color:#fff;font-weight:600}
 td.pin,td.net{color:#ffd479;white-space:nowrap;font-family:ui-monospace,monospace}
 .note{color:%(dim)s;font-size:12px;margin-top:10px;max-width:90ch}
</style>
<h1>CYD ESP32-2432S028 → усилитель 5.1</h1>
<p class="lead">Три стереоплаты на общей шине 24 В, источник — ПК с тремя аналоговыми
выходами, панель только включает питание, ставит mute и снимает сигнал для
визуализатора. Регулятора громкости и селектора входов нет: громкость в Windows,
джеки ПК идут на платы напрямую. Живой интерактивный гайд с симулятором — рядом, в
<code>wiring_cyd_amp.html</code>.</p>
<div class="wrap">
  <div class="panel">%(signal)s</div>
  <div class="panel">%(control)s</div>
</div>
<h2>Выводы платы</h2>
%(pins)s
<h2>Соединения</h2>
%(nets)s
<p class="note">Делитель: Uвх/3 + 1.65 В на входе АЦП; для источников 2 В rms и
более поставьте последовательно 22 кОм. Бюджет 24 В/4 А — 96 Вт на шесть каналов:
3x2200-3300 мкФ у плат, 4700-10000 мкФ на шине, NTC на пуске, предохранитель 5 А.
Саб лучше 4 Ом, а ещё лучше — отдельная ветка питания (§3 docs/wiring.md).</p>
""" % {"bg": BG, "fg": FG, "dim": DIM, "edge": EDGE, "signal": signal_panel(),
       "control": control_panel(), "pins": pin_table(), "nets": net_table()}

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(page, encoding="utf-8")
    print("written: %s (%d bytes)" % (OUT, len(page)))
    print("\nnets drawn:")
    for net, src, dst, note in NETS:
        print("  %-10s %-34s -> %-56s %s" % (net, src, dst, note))
    free = [p for p, _, s in PINS if s == "наш"]
    print("\nfree pins used: %s" % ", ".join(free))
    assert len(free) == 4, "the 5.1 build uses four pins: 27, 16, 22 and 35"
    return 0


if __name__ == "__main__":
    sys.exit(main())
