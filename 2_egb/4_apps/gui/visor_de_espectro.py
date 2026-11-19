#!/usr/bin/env python3
"""Visor de espectro: version grafica de app_cli.
Corre en la Raspberry. La ventana se ve en la PC conectandose con ssh -X.
"""

import math
import os
import select
import sys
import threading

os.environ.setdefault("QT_XCB_GL_INTEGRATION", "none")
# Tamaño estandar del cursor del mouse. Sin esto Qt lo calcula con la
# resolucion del servidor X de la PC y en pantallas grandes queda enorme
os.environ.setdefault("XCURSOR_SIZE", "24")

from PySide6.QtCore import QObject, Qt, QTimer, Signal
from PySide6.QtGui import QFont, QFontDatabase
from PySide6.QtWidgets import (QApplication, QFormLayout, QHBoxLayout, QLabel,
                               QMainWindow, QProgressBar, QPushButton, QSpinBox,
                               QVBoxLayout, QWidget)
import pyqtgraph as pg

ESP32_DEV_CMD = "/dev/esp32link_cmd"
ESP32_DEV_DATA = "/dev/esp32link_data"

MAX_LINE = 128  # mismo valor que en 3_gpos/esp32_link.h
MAX_PUNTOS = 512
TIMEOUT_RESP_MS = 1000
TIMEOUT_LIMPIEZA_MS = 200
DB_MIN_INICIAL = -9  # valor con el que el eje de dB inicia, luego se agranda si no entra en ese rango
REFRESCO_MS = 50  # el grafico se redibuja cada REFRESCO_MS

# ===================== estilo (paleta LVGL) =====================

FUENTE_TTF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "RobotoMono-Regular.ttf")

C_BG = "#0B1A2E"         # fondo pantalla config
C_BG_PLOT = "#081422"    # fondo grafico
C_HDR = "#163152"        # header
C_ACCENT = "#2BB3D6"     # cian: bordes, boton principal, barra de progreso
C_ACCENT_PRESS = "#1A8FAB"
C_TXT_PRI = "#EAF2FA"
C_TXT_SEC = "#9BB6D0"
C_TXT_LBL = "#A9C2DA"
C_TXT_BTN = "#062033"    # texto sobre cian
C_TXT_BTN2 = "#C4D6E6"   # texto botones secundarios
C_BORDE = "#1C3A5A"      # separadores / grilla
C_BORDE_BTN = "#6F8AA6"
C_CAMPO = "#0F2C40"      # fondo campo editable
C_TRAZA = "#F5D020"      # traza amarilla
C_ERR = "#E05A2B"
C_ERR_TXT = "#F0A070"
C_ERR_BG = "#1A1000"


def cargar_fuente():
    """Registra RobotoMono desde el .ttf. Si no esta, usa la del sistema o monospace."""
    if os.path.exists(FUENTE_TTF):
        fid = QFontDatabase.addApplicationFont(FUENTE_TTF)
        familias = QFontDatabase.applicationFontFamilies(fid)
        if familias:
            return familias[0]
    if "Roboto Mono" in QFontDatabase.families():
        return "Roboto Mono"
    return "monospace"


def hoja_estilo(fam):
    return f"""
    QMainWindow, QWidget#central {{ background: {C_BG}; }}
    QWidget {{ color: {C_TXT_PRI}; font-family: "{fam}"; font-size: 10pt; }}

    QWidget#header {{
        background: {C_HDR};
        border-bottom: 1px solid {C_ACCENT};
    }}
    QWidget#dot {{ background: {C_ACCENT}; }}
    QLabel#titulo {{ font-size: 14pt; color: {C_TXT_PRI}; background: transparent; }}
    QLabel#recibidos {{ color: {C_TXT_SEC}; background: transparent; }}

    QWidget#lateral {{ background: {C_BG}; border-left: 1px solid {C_BORDE}; }}
    QLabel#nombre {{ color: {C_TXT_LBL}; }}
    QLabel#seccion {{ color: {C_TXT_SEC}; font-size: 9pt; letter-spacing: 1px; }}

    QSpinBox {{
        background: {C_CAMPO}; color: {C_TXT_PRI};
        border: 1px solid {C_BORDE}; border-radius: 0px;
        padding: 4px 6px; font-size: 13pt;
        selection-background-color: {C_ACCENT}; selection-color: {C_TXT_BTN};
    }}
    QSpinBox:focus {{ border: 1px solid {C_ACCENT}; }}

    QPushButton {{
        background: transparent; color: {C_TXT_BTN2};
        border: 1px solid {C_BORDE_BTN}; border-radius: 0px;
        padding: 8px; font-size: 10pt;
    }}
    QPushButton:hover {{ border-color: {C_ACCENT}; color: {C_TXT_PRI}; }}
    QPushButton:pressed {{ background: {C_ACCENT_PRESS}; color: {C_TXT_BTN}; }}
    QPushButton#primario {{
        background: {C_ACCENT}; color: {C_TXT_BTN}; border: none;
        font-weight: bold; padding: 11px; font-size: 12pt;
    }}
    QPushButton#primario:pressed {{ background: {C_ACCENT_PRESS}; }}

    QLabel#error {{
        color: {C_ERR_TXT}; background: {C_ERR_BG};
        border: 1px solid {C_ERR}; padding: 8px;
    }}

    QProgressBar {{ background: #0C2138; border: none; }}
    QProgressBar::chunk {{ background: {C_ACCENT}; }}

    QToolTip {{ background: {C_CAMPO}; color: {C_TXT_PRI}; border: 1px solid {C_ACCENT}; }}
    """


class LabelError(QLabel):
    """Label de error: se oculta solo cuando no hay texto (evita el recuadro vacio)."""

    def setText(self, texto):
        super().setText(texto)
        self.setVisible(bool(texto))


fd_cmd = -1
fd_data = -1

# Texto de cada ERR n
errores = ["",
           "Comando desconocido",
           "Error de sintaxis",
           "Valor fuera de rango",
           "Configuracion invalida para iniciar",
           "No aplica al estado actual"]

# Parametros de set/get: rango y unidad
parametros = {"frec_inicio": (10, 99999, "Hz"),
              "frec_final": (11, 100000, "Hz"),
              "puntos": (2, 512, ""),
              "tiempo": (10, 10000, "ms")}


class Barrido:

    def __init__(self):
        self.frec_inicio = self.frec_final = self.puntos = self.tiempo = 0
        self.pts = []  # lista de (freq, db)


barrido = Barrido()

# ===================== barrido =====================


def campos(linea):
    """'POINT i=3 freq=18 db=-0.29' -> {'i': '3', 'freq': '18', 'db': '-0.29'}"""
    return dict(c.split("=", 1) for c in linea.split() if "=" in c)


def procesar_datos(linea):
    """SWEEP empieza un barrido nuevo y POINT agrega un punto.
    Devuelve True cuando llega el ultimo punto del barrido."""
    try:
        c = campos(linea)
        if linea.startswith("SWEEP") and 1 <= int(c["puntos"]) <= MAX_PUNTOS:
            barrido.frec_inicio = int(c["frec_inicio"])
            barrido.frec_final = int(c["frec_final"])
            barrido.puntos = int(c["puntos"])
            barrido.tiempo = int(c["tiempo"])
            barrido.pts = []
        elif linea.startswith("POINT") and len(barrido.pts) < barrido.puntos:
            barrido.pts.append((int(c["freq"]), float(c["db"])))
            return int(c["i"]) == barrido.puntos - 1
    except (KeyError, ValueError):
        pass
    return False


class Monitor(QObject):
    """Pasa cada linea de data del hilo monitor al hilo de la ventana"""
    nueva_linea = Signal(str)


monitor = Monitor()


def hilo_monitor():
    """Hilo monitor: loop bloqueante de read() sobre data, arma las lineas
    y las manda a la ventana."""
    resto = b""
    while buf := os.read(fd_data, MAX_LINE):
        resto += buf
        *lineas, resto = resto.split(b"\n")
        for linea in lineas:
            monitor.nueva_linea.emit(linea.decode(errors="ignore"))

# ===================== comandos al ESP32 =====================


def esperar_respuesta():
    """Espera con poll() hasta TIMEOUT_RESP_MS a que haya respuesta en
    /dev/esp32link_cmd. Devuelve la linea o None si vencio el timeout."""
    p = select.poll()
    p.register(fd_cmd, select.POLLIN)
    # poll() duerme hasta que llegue la respuesta o por timeout.
    if not p.poll(TIMEOUT_RESP_MS):
        return None
    # Volvio por POLLIN: la linea ya esta en el fifo y el read no se bloquea
    return os.read(fd_cmd, MAX_LINE).decode(errors="ignore").strip()


def limpiar(fd, timeout_ms):
    """Lee y descarta todo lo que haya en el nodo fd (cmd o data) hasta que
    pasen timeout_ms sin que llegue nada."""
    p = select.poll()
    p.register(fd, select.POLLIN)
    while p.poll(timeout_ms):
        os.read(fd, MAX_LINE)


def enviar_comando(linea):
    """Manda un comando al ESP32 y devuelve el texto de la respuesta."""
    # Descarta respuestas viejas: la primera linea que llegue despues
    # del write tiene que ser la de este comando
    limpiar(fd_cmd, 0)

    try:
        os.write(fd_cmd, linea.encode())  # el driver agrega el '\n'
    except OSError as e:
        return f"write: {e}"

    resp = esperar_respuesta()
    if resp is None:
        return "sin respuesta del ESP32"
    if resp.startswith("ERR ") and resp[4:].isdigit():
        codigo = int(resp[4:])
        if 1 <= codigo <= 5:
            return f"ERR {codigo}: {errores[codigo]}"
    return resp

# ===================== ventana =====================


class Ventana(QMainWindow):
    def __init__(self, fam):
        super().__init__()
        self.setWindowTitle("Analizador Frec. - ESP32 link")
        self.resize(1050, 560)

        # ---------- header (igual que en el ESP32) ----------
        header = QWidget()
        header.setObjectName("header")
        header.setAttribute(Qt.WA_StyledBackground, True)
        header.setFixedHeight(40)
        hdr = QHBoxLayout(header)
        hdr.setContentsMargins(14, 0, 14, 0)
        hdr.setSpacing(10)
        dot = QWidget()
        dot.setObjectName("dot")
        dot.setAttribute(Qt.WA_StyledBackground, True)
        dot.setFixedSize(11, 11)
        titulo = QLabel("Analizador Frec.")
        titulo.setObjectName("titulo")
        self.lbl_recibidos = QLabel("")  # vacia hasta que llega el primer SWEEP
        self.lbl_recibidos.setObjectName("recibidos")
        hdr.addWidget(dot)
        hdr.addWidget(titulo)
        hdr.addStretch()
        hdr.addWidget(self.lbl_recibidos)

        # ---------- grafico ----------
        # Grafico: dB en funcion de la frecuencia, eje X logaritmico
        pg.setConfigOptions(antialias=True, foreground=C_TXT_SEC)
        self.grafico = pg.PlotWidget()
        self.grafico.setBackground(C_BG_PLOT)
        self.grafico.showGrid(x=True, y=True, alpha=0.55)
        estilo_lbl = {"color": C_TXT_SEC, "font-family": fam, "font-size": "10pt"}
        self.grafico.setLabel("bottom", "Frecuencia (Hz)", **estilo_lbl)
        self.grafico.setLabel("left", "Transferencia (dB)", **estilo_lbl)
        fuente_ticks = QFont(fam, 9)
        for lado in ("bottom", "left"):
            eje = self.grafico.getAxis(lado)
            eje.setPen(pg.mkPen(C_BORDE_BTN))   # tambien define el color de la grilla
            eje.setTextPen(pg.mkPen(C_TXT_SEC))
            eje.setStyle(tickFont=fuente_ticks, tickLength=0)
        self.grafico.setLogMode(x=True, y=False)
        # Eje X: numeros solo en las decadas y lineas de grilla sin numero en
        # 2..9 de cada decada. Las posiciones van en log10 por el eje logaritmico
        decadas = [(1, "10"), (2, "100"), (3, "1k"), (4, "10k"), (5, "100k")]
        intermedias = [(math.log10(m * 10 ** d), "") for d in range(1, 5) for m in range(2, 10)]
        self.grafico.getAxis("bottom").setTicks([decadas, intermedias])
        self.grafico.setYRange(DB_MIN_INICIAL, 0)
        pluma = pg.mkPen(C_TRAZA, width=2)
        pluma.setCapStyle(Qt.RoundCap)
        pluma.setJoinStyle(Qt.RoundJoin)
        self.curva = self.grafico.plot(pen=pluma)

        # Barra de progreso fina debajo del grafico (como en el ESP32)
        self.progreso = QProgressBar()
        self.progreso.setTextVisible(False)
        self.progreso.setFixedHeight(4)
        self.progreso.setRange(0, 1)
        self.progreso.setValue(0)

        zona_grafico = QVBoxLayout()
        zona_grafico.setContentsMargins(0, 0, 0, 0)
        zona_grafico.setSpacing(0)
        zona_grafico.addWidget(self.grafico, stretch=1)
        zona_grafico.addWidget(self.progreso)

        # ---------- panel lateral ----------
        # Panel lateral: parametros (set/get), control del barrido y estado
        panel = QVBoxLayout()
        panel.setContentsMargins(14, 14, 14, 14)
        panel.setSpacing(8)

        seccion = QLabel("CONFIGURACION")
        seccion.setObjectName("seccion")
        panel.addWidget(seccion)

        # Cada spinbox manda su set al terminar de editarlo (Enter o cambio de foco)
        form = QFormLayout()
        form.setVerticalSpacing(8)
        form.setHorizontalSpacing(10)
        self.spinboxes = {}
        self.invalidos = set()  # parametros con un valor que no se pudo aplicar
        for nombre, (minimo, maximo, unidad) in parametros.items():
            spinbox = QSpinBox()
            # El spinbox acepta cualquier numero; el rango se valida en
            # aplicar_parametro para poder avisar cual es
            spinbox.setRange(0, 999999)
            spinbox.setToolTip(f"rango: {minimo} - {maximo} {unidad}")
            spinbox.setButtonSymbols(QSpinBox.NoButtons)
            spinbox.setAlignment(Qt.AlignRight)
            spinbox.editingFinished.connect(lambda n=nombre: self.aplicar_parametro(n))
            etiqueta = QLabel(f"{nombre} ({unidad})" if unidad else nombre)
            etiqueta.setObjectName("nombre")
            form.addRow(etiqueta, spinbox)
            self.spinboxes[nombre] = spinbox
        panel.addLayout(form)
        panel.addSpacing(12)
        self.agregar_boton(panel, "INICIAR BARRIDO", self.iniciar, primario=True)
        fila = QHBoxLayout()
        fila.setSpacing(8)
        for cmd in ("pause", "resume"):
            self.agregar_boton(fila, {"pause": "PAUSAR", "resume": "REANUDAR"}[cmd],
                               lambda c=cmd: self.comando(c))
        panel.addLayout(fila)
        self.agregar_boton(panel, "CANCELAR", self.cancelar)
        panel.addSpacing(12)

        self.lbl_respuesta = LabelError("")  # solo muestra errores
        self.lbl_respuesta.setObjectName("error")
        self.lbl_respuesta.setWordWrap(True)
        self.lbl_respuesta.setVisible(False)
        panel.addWidget(self.lbl_respuesta)
        panel.addStretch()

        lateral = QWidget()
        lateral.setObjectName("lateral")
        lateral.setAttribute(Qt.WA_StyledBackground, True)
        lateral.setLayout(panel)
        lateral.setFixedWidth(280)

        cuerpo = QHBoxLayout()
        cuerpo.setContentsMargins(0, 0, 0, 0)
        cuerpo.setSpacing(0)
        cuerpo.addLayout(zona_grafico, stretch=1)
        cuerpo.addWidget(lateral)

        layout = QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addWidget(header)
        layout.addLayout(cuerpo, stretch=1)
        central = QWidget()
        central.setObjectName("central")
        central.setLayout(layout)
        self.setCentralWidget(central)

        self.pendiente = False
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refrescar)
        self.timer.start(REFRESCO_MS)

    @staticmethod
    def agregar_boton(panel, texto, accion, primario=False):
        boton = QPushButton(texto)
        if primario:
            boton.setObjectName("primario")
        boton.setCursor(Qt.PointingHandCursor)
        boton.clicked.connect(lambda: accion())
        panel.addWidget(boton)

    # ---------- comandos ----------

    def comando(self, linea):
        """Manda el comando. Si falla muestra el error en el panel; si sale
        bien se borra el error anterior."""
        resp = enviar_comando(linea)
        error = resp.startswith(("ERR", "sin respuesta", "write"))
        # ERR 5 solo se avisa en start; en pause/resume/cancel
        # no hace falta, el barrido sigue como estaba
        if resp.startswith("ERR 5") and linea in ("pause", "resume", "cancel"):
            error = False
        self.lbl_respuesta.setText(f"{linea}: {resp}" if error else "")
        return resp

    def cargar_valores(self, resp):
        """Pone en los spinbox los valores de una respuesta de get o de un
        SWEEP. Son valores del ESP32, asi que dejan de estar invalidos."""
        for nombre, valor in campos(resp).items():
            if nombre in self.spinboxes and valor.isdigit():
                self.spinboxes[nombre].setValue(int(valor))
                self.invalidos.discard(nombre)

    def iniciar(self):
        """start, salvo que haya un valor mal puesto."""
        if self.invalidos:
            nombre = sorted(self.invalidos)[0]
            minimo, maximo, unidad = parametros[nombre]
            self.lbl_respuesta.setText(f"{nombre} {minimo} - {maximo} {unidad}")
            return
        self.comando("start")

    def cancelar(self):
        """cancel y limpia el grafico. Con puntos = 0, procesar_datos ignora
        los POINT que todavia esten en camino hasta el proximo SWEEP."""
        self.comando("cancel")
        barrido.puntos = 0
        barrido.pts = []
        self.pendiente = False
        self.curva.setData([], [])
        self.grafico.setYRange(DB_MIN_INICIAL, 0)
        self.lbl_recibidos.setText("")
        self.progreso.setValue(0)

    def leer_config(self):
        """get config y carga los valores en los spinbox (al iniciar)."""
        self.cargar_valores(self.comando("get config"))
        self.eje_frecuencia(self.spinboxes["frec_inicio"].value(), self.spinboxes["frec_final"].value())

    def aplicar_parametro(self, nombre):
        """set del parametro editado."""
        minimo, maximo, unidad = parametros[nombre]
        valor = self.spinboxes[nombre].value()
        if not minimo <= valor <= maximo:
            self.invalidos.add(nombre)
            self.lbl_respuesta.setText(f"{nombre} {minimo} - {maximo} {unidad}")
        elif self.comando(f"set {nombre} {valor}") != "OK":
            self.invalidos.add(nombre)
        else:
            self.invalidos.discard(nombre)

    def eje_frecuencia(self, frec_inicio, frec_final):
        """Eje X logaritmico cortado justo en frec_inicio y frec_final."""
        if frec_inicio < frec_final:
            self.grafico.setXRange(math.log10(frec_inicio), math.log10(frec_final), padding=0)

    # ---------- datos ----------

    def nueva_linea(self, linea):
        procesar_datos(linea)
        if linea.startswith("SWEEP"):  # barrido nuevo: eje X y spinbox toman su config
            self.eje_frecuencia(barrido.frec_inicio, barrido.frec_final)
            self.cargar_valores(linea)
        self.pendiente = True

    def refrescar(self):
        """Redibuja el grafico y el label si llegaron datos desde el ultimo refresco."""
        if not self.pendiente:
            return
        self.pendiente = False
        completo = 0 < barrido.puntos == len(barrido.pts)

        dbs = [db for _, db in barrido.pts]
        frecs = [f for f, _ in barrido.pts]
        self.curva.setData(frecs, dbs)
        # Eje de dB: se agranda si llegan valores fuera de ese rango
        self.grafico.setYRange(min(dbs + [DB_MIN_INICIAL]), max(dbs + [0]))
        self.lbl_recibidos.setText(f"{len(barrido.pts)} / {barrido.puntos}" + ("   COMPLETO" if completo else ""))
        self.progreso.setRange(0, max(barrido.puntos, 1))
        self.progreso.setValue(len(barrido.pts))

# ===================== main =====================


def main():
    global fd_cmd, fd_data

    try:
        fd_cmd = os.open(ESP32_DEV_CMD, os.O_RDWR)
        fd_data = os.open(ESP32_DEV_DATA, os.O_RDONLY)
    except OSError as e:
        print(f"open: {e}")
        return 1

    # Descarta la basura que haya quedado en cmd por arranque del ESP32 o mensajes anteriores
    limpiar(fd_cmd, TIMEOUT_LIMPIEZA_MS)
    # cancel deja al ESP32 en un estado conocido, listo para un start.
    # ERR 5 es que no habia barrido que cancelar, tambien sirve
    resp = enviar_comando("cancel")
    if resp != "OK" and not resp.startswith("ERR 5"):
        print(f"cancel: {resp}")
    # Descarta las mediciones que quedaron en data de antes de abrir la app.
    # Va despues del cancel: con el barrido frenado ya no llegan POINT y se
    # puede esperar TIMEOUT_LIMPIEZA_MS a que terminen los que estaban en camino
    limpiar(fd_data, TIMEOUT_LIMPIEZA_MS)

    app = QApplication(sys.argv)
    fam = cargar_fuente()
    app.setFont(QFont(fam, 10))
    app.setStyleSheet(hoja_estilo(fam))
    ventana = Ventana(fam)
    monitor.nueva_linea.connect(ventana.nueva_linea)
    # daemon: el hilo termina solo al cerrar la app
    threading.Thread(target=hilo_monitor, daemon=True).start()

    ventana.show()
    ventana.leer_config()  # arranca con la configuracion actual del ESP32
    ret = app.exec()

    os.close(fd_data)
    os.close(fd_cmd)
    return ret


if __name__ == "__main__":
    sys.exit(main())
