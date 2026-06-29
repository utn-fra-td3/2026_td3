import sys
import serial
import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore

# ==========================================
# CONFIGURACIÓN DEL SISTEMA
# ==========================================
PUERTO = 'COM3' # <-- REVISAR Y CAMBIAR AL COM DE TU ESP32
BAUDIOS = 921600
PUNTOS_PANTALLA = 400
V_REF = 3.3 

try:
    ser = serial.Serial(PUERTO, BAUDIOS, timeout=0.05)
    print(f"Conectado exitosamente a {PUERTO}")
except Exception as e:
    print(f"Error abriendo el puerto {PUERTO}: {e}")
    sys.exit(1)

# Variables globales para almacenar la configuración del instrumento
flanco_trigger = 0
nivel_trigger = 2048
tiempo_div_ms = 1
amplitud_div_v = 1.0
modo_actual = 0 # 0=X1, 1=X10, 2=AC

# ==========================================
# CONFIGURACIÓN DE LA INTERFAZ GRÁFICA
# ==========================================
app = QtWidgets.QApplication([])
win = QtWidgets.QWidget()
win.setWindowTitle("Osciloscopio Digital ESP32 - Multi Modo")
win.resize(1100, 700)
win.setStyleSheet("background-color: #121212;")

layout = QtWidgets.QVBoxLayout()
win.setLayout(layout)

# Panel Superior Dinámico
panel_stats = QtWidgets.QLabel("Esperando señal...")
panel_stats.setAlignment(QtCore.Qt.AlignCenter)
panel_stats.setStyleSheet("""
    font-family: monospace; 
    font-size: 12pt; 
    font-weight: bold;
    background-color: #1e1e1e; 
    color: white; 
    padding: 15px; 
    border-radius: 5px;
    border: 1px solid #333333;
""")
layout.addWidget(panel_stats)

# Lienzo del Gráfico
plot = pg.PlotWidget(title="Adquisición en Tiempo Real")
plot.showGrid(x=True, y=True, alpha=0.5)

plot.setLabel('left', 'Tensión', units='V')
plot.setLabel('bottom', 'Tiempo', units='ms')
layout.addWidget(plot)

curve = plot.plot(pen=pg.mkPen('c', width=2))

# ==========================================
# CURSORES MÓVILES
# ==========================================
# 1. Cursor Horizontal (Nivel de Trigger)
cursor_trig = pg.InfiniteLine(
    angle=0, movable=True, pos=1.65, 
    pen=pg.mkPen('y', style=QtCore.Qt.DashLine),
    label='Trig: {value:.2f} V', 
    labelOpts={'color': 'y', 'position': 0.05, 'fill': pg.mkBrush(0, 0, 0, 200)}
)
plot.addItem(cursor_trig)

# 2. Cursor Vertical (Medición de Tiempo)
cursor_tiempo = pg.InfiniteLine(
    angle=90, movable=True, pos=2.0, 
    pen=pg.mkPen('g', style=QtCore.Qt.DashLine),
    label='T: {value:.2f} ms', 
    labelOpts={'color': 'g', 'position': 0.85, 'fill': pg.mkBrush(0, 0, 0, 200)}
)
plot.addItem(cursor_tiempo)

buffer_datos = []

# ==========================================
# FUNCIÓN DE LECTURA Y ACTUALIZACIÓN
# ==========================================
def actualizar_grafico():
    global buffer_datos, flanco_trigger, nivel_trigger, tiempo_div_ms, amplitud_div_v, modo_actual
    
    try:
        lineas_leidas = 0
        while ser.in_waiting > 0 and lineas_leidas < 1000:
            linea = ser.readline().decode('ascii').strip()
            lineas_leidas += 1
            
            if linea.startswith("SYNC"):
                partes = linea.split(',')
                # Ahora esperamos 6 partes en lugar de 5
                if len(partes) >= 6:
                    flanco_trigger = int(partes[1])
                    nivel_trigger = int(partes[2])
                    tiempo_div_ms = int(partes[3])
                    amplitud_div_v = float(partes[4])
                    modo_actual = int(partes[5])
                    
                    # Factor de multiplicación según el modo (X10 = *10)
                    factor_amp = 10.0 if modo_actual == 1 else 1.0
                    
                    # 1. Eje X: Grilla fija de 10 divisiones
                    rango_x = tiempo_div_ms * 10.0
                    plot.setXRange(0, rango_x, padding=0)
                    plot.getAxis('bottom').setTickSpacing(levels=[(tiempo_div_ms, 0)])

                    # 2. Eje Y: Dinámico según el Modo
                    centro_y = (V_REF / 2.0) * factor_amp  # 1.65V o 16.5V
                    if modo_actual == 2: 
                        centro_y = 0.0  # Si es modo AC, centramos en 0V la pantalla
                        
                    rango_y_mitad = amplitud_div_v * 5.0 * factor_amp
                    plot.setYRange(centro_y - rango_y_mitad, centro_y + rango_y_mitad, padding=0)
                    
                    amplitud_visual = amplitud_div_v * factor_amp
                    plot.getAxis('left').setTickSpacing(levels=[(amplitud_visual, 0)])
                    
                    # Ajuste visual del Cursor de Trigger
                    voltios_trigger = (nivel_trigger * (V_REF / 4095.0)) * factor_amp
                    if modo_actual == 2:
                        voltios_trigger -= (V_REF / 2.0) # Lo bajamos a la escala AC
                        
                    cursor_trig.setValue(voltios_trigger)
                    
            elif linea.isdigit():
                buffer_datos.append(int(linea))
                
    except (UnicodeDecodeError, ValueError):
        pass 
    except serial.SerialException:
        timer.stop()
        panel_stats.setText("<span style='color: #FF5555; font-size: 20pt;'>⚠️ CONEXIÓN PERDIDA (Cable desconectado) ⚠️</span>")
        return
        
    # Dibujado de la pantalla
    if len(buffer_datos) >= PUNTOS_PANTALLA:
        ventana_cruda = buffer_datos[-PUNTOS_PANTALLA:]
        buffer_datos.clear()
        
        # Eje X de Tiempo
        rango_x_total = tiempo_div_ms * 10.0
        x_tiempo_ms_dinamico = np.linspace(0, rango_x_total, PUNTOS_PANTALLA)
        
        # Eje Y de Voltaje
        y_volts = np.array(ventana_cruda) * (V_REF / 4095.0)
        
        # Procesamiento matemático según el modo físico de la placa
        if modo_actual == 1:
            y_volts = y_volts * 10.0  # MODO X10: Amplificamos la lectura
        elif modo_actual == 2:
            y_volts = y_volts - (V_REF / 2.0) # MODO AC: Eliminamos offset DC centrado
            
        curve.setData(x_tiempo_ms_dinamico, y_volts)
        
        v_max = np.max(y_volts)
        v_min = np.min(y_volts)
        v_pp = v_max - v_min
        
        txt_flanco = "SUBIDA" if flanco_trigger == 0 else "BAJADA"
        nombres_modo = ["CH5 (X1)", "CH6 (X10)", "CH2 (AC)"]
        txt_modo = nombres_modo[modo_actual]
        amp_real = amplitud_div_v * (10.0 if modo_actual == 1 else 1.0)
        
        stats_html = f"""
            <span style='color: #FF5555; margin-right: 15px;'>Vmax: {v_max:.2f}V</span>
            <span style='color: #5555FF; margin-right: 15px;'>Vmin: {v_min:.2f}V</span>
            <span style='color: #55FF55; margin-right: 25px;'>Vpp: {v_pp:.2f}V</span>
            <span style='color: #FFFF55; margin-right: 15px;'>| T: {tiempo_div_ms}ms/div</span>
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