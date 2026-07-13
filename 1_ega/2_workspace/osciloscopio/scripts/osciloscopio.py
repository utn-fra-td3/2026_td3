import sys
import struct
import serial
import threading
import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore

# ==========================================
# CONFIGURACIÓN DEL SISTEMA
# ==========================================
PUERTO = 'COM11'  # <-- REVISAR Y CAMBIAR AL COM DE TU ESP32
BAUDIOS = 921600
PUNTOS_PANTALLA = 400
V_REF = 3.3

# ==========================================
# PROTOCOLO BINARIO (ESP32 -> PC)
# ==========================================
UART_SYNC = 0xAA55
SYNC_BYTES = bytes([UART_SYNC & 0xFF, UART_SYNC >> 8])
FORMATO_TRAMA = f'<HBHHfBH{PUNTOS_PANTALLA}HH'
TAMANO_TRAMA = struct.calcsize(FORMATO_TRAMA)

# ==========================================
# PROTOCOLO BINARIO (PC -> ESP32)
# ==========================================
SYNC_PC = 0xBB66
FORMATO_CMD = '<HBIIfBH'

def _checksum(payload_bytes):
    return sum(payload_bytes) & 0xFFFF

try:
    ser = serial.Serial(PUERTO, BAUDIOS, timeout=0.05)
    print(f"Conectado exitosamente a {PUERTO}")
except Exception as e:
    print(f"Error abriendo el puerto {PUERTO}: {e}")
    sys.exit(1)

buffer_bytes = bytearray()

# Estado actual para mantener sincronización si cambias un solo parámetro
estado_actual = {
    'flanco': 1,
    'nivel': 2000,
    'tiempo_idx': 1,   # 0=5, 1=10, 2=20, 3=50 (décimas)
    'amplitud_idx': 0, # 0=1.0V, 1=5.0V
    'modo': 0          # 0=X1, 1=X10, 2=AC
}

def leer_ultima_trama():
    global buffer_bytes
    disponibles = ser.in_waiting
    if disponibles:
        buffer_bytes.extend(ser.read(disponibles))

    ultima_trama = None
    while True:
        idx = buffer_bytes.find(SYNC_BYTES)
        if idx == -1:
            if len(buffer_bytes) > 1:
                del buffer_bytes[:-1]
            break
        if idx > 0:
            del buffer_bytes[:idx] 
        if len(buffer_bytes) < TAMANO_TRAMA:
            break 

        cruda = bytes(buffer_bytes[:TAMANO_TRAMA])
        del buffer_bytes[:TAMANO_TRAMA]

        campos = struct.unpack(FORMATO_TRAMA, cruda)
        if _checksum(cruda[:-2]) != campos[-1]:
            continue 

        _, flanco, nivel, tiempo_ms_decimas, amplitud, modo, num_puntos, *resto = campos
        ultima_trama = {
            'flanco': flanco,
            'nivel': nivel,
            'tiempo_ms': tiempo_ms_decimas / 10.0, 
            'amplitud': amplitud,
            'modo': modo,
            'datos': resto[:num_puntos],
        }
    return ultima_trama

# ==========================================
# CONSOLA Y ENVÍO DE COMANDOS
# ==========================================
def enviar_estado_actual():
    tiempos_decimas = [5, 10, 20, 50]
    amplitudes = [1.0, 5.0]

    flanco = estado_actual['flanco']
    modo = estado_actual['modo']
    nivel = estado_actual['nivel']
    tiempo_decimas = tiempos_decimas[estado_actual['tiempo_idx']]
    amplitud = amplitudes[estado_actual['amplitud_idx']]

    datos_sin_chk = struct.pack('<HBIIfB', SYNC_PC, flanco, nivel, tiempo_decimas, amplitud, modo)
    chk = _checksum(datos_sin_chk)
    
    paquete = struct.pack(FORMATO_CMD, SYNC_PC, flanco, nivel, tiempo_decimas, amplitud, modo, chk)
    ser.write(paquete)

def hilo_consola():
    print("\n" + "="*35)
    print(" 🛠️ CONSOLA DE CONTROL OSCILOSCOPIO")
    print("="*35)
    print(" Escribe el comando y presiona ENTER:")
    print(" M <0|1|2> : Modo (0=X1, 1=X10, 2=AC)")
    print(" F <0|1>   : Flanco (0=Subida, 1=Bajada)")
    print(" T <0|1|2|3>: Tiempo (0=0.5, 1=1.0, 2=2.0, 3=5.0 ms/div)")
    print(" A <0|1>   : Amplitud (0=1.0V, 1=5.0V)")
    print(" L <val>   : Nivel Trigger (0 a 4095)")
    print("="*35 + "\n")

    while True:
        try:
            linea = sys.stdin.readline().strip().upper()
            if not linea: continue

            partes = linea.split()
            if len(partes) != 2:
                print("⚠️ Formato incorrecto. Ejemplo: L 2500")
                continue

            cmd = partes[0]
            val = int(partes[1])
            actualizado = False

            if cmd == 'M' and val in [0, 1, 2]:
                estado_actual['modo'] = val; actualizado = True
            elif cmd == 'F' and val in [0, 1]:
                estado_actual['flanco'] = val; actualizado = True
            elif cmd == 'T' and val in [0, 1, 2, 3]:
                estado_actual['tiempo_idx'] = val; actualizado = True
            elif cmd == 'A' and val in [0, 1]:
                estado_actual['amplitud_idx'] = val; actualizado = True
            elif cmd == 'L' and 0 <= val <= 4095:
                estado_actual['nivel'] = val; actualizado = True
            else:
                print("⚠️ Comando o valor fuera de rango.")

            if actualizado:
                enviar_estado_actual()
                print(f"✅ Comando enviado -> {cmd}: {val}")

        except ValueError:
            print("⚠️ El valor debe ser un número entero.")
        except Exception as e:
            print(f"⚠️ Error: {e}")

# Iniciamos el hilo de la consola (daemon=True para que se cierre con el programa principal)
threading.Thread(target=hilo_consola, daemon=True).start()

# ==========================================
# CONFIGURACIÓN DE LA INTERFAZ GRÁFICA
# ==========================================
app = QtWidgets.QApplication([])
win = QtWidgets.QWidget()
win.setWindowTitle("Osciloscopio Digital ESP32 - Modo Consola")
win.resize(1100, 650)
win.setStyleSheet("background-color: #121212; color: white;")

layout = QtWidgets.QVBoxLayout()
win.setLayout(layout)

# --- PANEL DE ESTADÍSTICAS ---
panel_stats = QtWidgets.QLabel("Esperando señal...")
panel_stats.setAlignment(QtCore.Qt.AlignCenter)
panel_stats.setStyleSheet("""
    font-family: monospace; 
    font-size: 12pt; 
    font-weight: bold;
    background-color: #1e1e1e; 
    padding: 10px; 
    border-radius: 5px;
    border: 1px solid #333333;
""")
layout.addWidget(panel_stats)

# --- GRÁFICO ---
plot = pg.PlotWidget(title="Adquisición en Tiempo Real")
plot.showGrid(x=True, y=True, alpha=0.5)
plot.setLabel('left', 'Tensión', units='V')
plot.setLabel('bottom', 'Tiempo', units='ms')
layout.addWidget(plot)

curve = plot.plot(pen=pg.mkPen('c', width=2))

cursor_trig = pg.InfiniteLine(
    angle=0, movable=True, pos=1.65,
    pen=pg.mkPen('y', style=QtCore.Qt.DashLine),
    label='Trig: {value:.2f} V',
    labelOpts={'color': 'y', 'position': 0.05, 'fill': pg.mkBrush(0, 0, 0, 200)}
)
plot.addItem(cursor_trig)

cursor_tiempo = pg.InfiniteLine(
    angle=90, movable=True, pos=2.0,
    pen=pg.mkPen('g', style=QtCore.Qt.DashLine),
    label='T: {value:.2f} ms',
    labelOpts={'color': 'g', 'position': 0.85, 'fill': pg.mkBrush(0, 0, 0, 200)}
)
plot.addItem(cursor_tiempo)

# ==========================================
# ACTUALIZACIÓN PRINCIPAL
# ==========================================
def actualizar_grafico():
    try:
        trama = leer_ultima_trama()
    except serial.SerialException:
        timer.stop()
        panel_stats.setText("<span style='color: #FF5555; font-size: 16pt;'>⚠️ CONEXIÓN PERDIDA (Cable desconectado) ⚠️</span>")
        return

    if trama is None:
        return 

    # --- Sincronización Inversa Silenciosa ---
    # Si cambias algo con el encoder del ESP32, actualizamos el diccionario local
    # para que la consola no sobreescriba cambios con valores viejos.
    estado_actual['modo'] = trama['modo']
    estado_actual['flanco'] = trama['flanco']
    estado_actual['nivel'] = trama['nivel']
    
    tiempo_decimas = int(trama['tiempo_ms'] * 10)
    tiempos_idx = {5: 0, 10: 1, 20: 2, 50: 3}
    if tiempo_decimas in tiempos_idx:
        estado_actual['tiempo_idx'] = tiempos_idx[tiempo_decimas]
        
    estado_actual['amplitud_idx'] = 0 if trama['amplitud'] == 1.0 else 1
    # ----------------------------------------

    flanco_trigger = trama['flanco']
    nivel_trigger = trama['nivel']
    tiempo_div_ms = trama['tiempo_ms']
    amplitud_div_v = trama['amplitud']
    modo_actual = trama['modo']

    factor_amp = 10.0 if modo_actual == 1 else 1.0

    rango_x = tiempo_div_ms * 10.0
    plot.setXRange(0, rango_x, padding=0)
    plot.getAxis('bottom').setTickSpacing(levels=[(tiempo_div_ms, 0)])

    centro_y = (V_REF / 2.0) * factor_amp
    if modo_actual == 2:
        centro_y = 0.0

    rango_y_mitad = amplitud_div_v * 5.0 * factor_amp
    plot.setYRange(centro_y - rango_y_mitad, centro_y + rango_y_mitad, padding=0)

    amplitud_visual = amplitud_div_v * factor_amp
    plot.getAxis('left').setTickSpacing(levels=[(amplitud_visual, 0)])

    voltios_trigger = (nivel_trigger * (V_REF / 4095.0)) * factor_amp
    if modo_actual == 2:
        voltios_trigger -= (V_REF / 2.0)
    cursor_trig.setValue(voltios_trigger)

    x_tiempo_ms = np.linspace(0, rango_x, PUNTOS_PANTALLA)
    y_volts = np.array(trama['datos'], dtype=np.float64) * (V_REF / 4095.0)

    if modo_actual == 1:
        y_volts = y_volts * 10.0
    elif modo_actual == 2:
        y_volts = y_volts - (V_REF / 2.0)

    curve.setData(x_tiempo_ms, y_volts)

    v_max = np.max(y_volts)
    v_min = np.min(y_volts)
    v_pp = v_max - v_min

    txt_flanco = "SUBIDA" if flanco_trigger == 0 else "BAJADA"
    nombres_modo = ["CH5 (X1)", "CH6 (X10)", "CH2 (AC)"]
    txt_modo = nombres_modo[modo_actual] if modo_actual < len(nombres_modo) else "?"
    amp_real = amplitud_div_v * (10.0 if modo_actual == 1 else 1.0)

    stats_html = f"""
        <span style='color: #FF5555; margin-right: 15px;'>Vmax: {v_max:.2f}V</span>
        <span style='color: #5555FF; margin-right: 15px;'>Vmin: {v_min:.2f}V</span>
        <span style='color: #55FF55; margin-right: 25px;'>Vpp: {v_pp:.2f}V</span>
        <span style='color: #FFFF55; margin-right: 15px;'>| T: {tiempo_div_ms:.1f}ms/div</span>
        <span style='color: #00FFFF; margin-right: 15px;'>A: {amp_real:.1f}V/div</span>
        <span style='color: #FF00FF; margin-right: 15px;'>Trig: {txt_flanco}</span>
        <span style='color: #FFAA00;'>Modo: {txt_modo}</span>
    """
    panel_stats.setText(stats_html)

timer = QtCore.QTimer()
timer.timeout.connect(actualizar_grafico)
timer.start(10)

if __name__ == '__main__':
    win.show()
    QtWidgets.QApplication.instance().exec_()
    ser.close()