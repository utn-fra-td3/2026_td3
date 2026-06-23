import sys
import serial
import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore

# ==========================================
# CONFIGURACIÓN DEL SISTEMA
# ==========================================
PUERTO = 'COM11'  # <-- Asegurate de que sea el puerto de tu ESP32
BAUDIOS = 921600
PUNTOS_PANTALLA = 400
V_REF = 3.3 
FS = 20000.0 # Frecuencia de muestreo en Hz (20 kHz)

try:
    ser = serial.Serial(PUERTO, BAUDIOS, timeout=0.05)
    print(f"Conectado exitosamente a {PUERTO}")
except Exception as e:
    print(f"Error abriendo el puerto {PUERTO}: {e}")
    sys.exit(1)

# ==========================================
# CÁLCULO DEL EJE X (TIEMPO)
# ==========================================
x_tiempo_ms = np.arange(PUNTOS_PANTALLA) * (1000.0 / FS)

# Variables globales para almacenar la configuración del instrumento
flanco_trigger = 1
nivel_trigger = 2000
tiempo_div_ms = 5
amplitud_div_v = 3.3

# ==========================================
# CONFIGURACIÓN DE LA INTERFAZ GRÁFICA
# ==========================================
app = QtWidgets.QApplication([])
win = QtWidgets.QWidget()
win.setWindowTitle("Osciloscopio Digital ESP32 - Control en Tiempo Real")
win.resize(1100, 700)
win.setStyleSheet("background-color: #121212;")

layout = QtWidgets.QVBoxLayout()
win.setLayout(layout)

# Panel Superior Dinámico
panel_stats = QtWidgets.QLabel("Esperando señal y sincronización...")
panel_stats.setAlignment(QtCore.Qt.AlignCenter)
panel_stats.setStyleSheet("""
    font-family: monospace; 
    font-size: 13pt; 
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
plot.setYRange(0, 3.5)
plot.setXRange(0, x_tiempo_ms[-1]) 
plot.showGrid(x=True, y=True, alpha=0.5)

plot.setLabel('left', 'Tensión', units='V')
plot.setLabel('bottom', 'Tiempo', units='ms')
layout.addWidget(plot)

curve = plot.plot(pen=pg.mkPen('c', width=2))

# ==========================================
# CURSORES MÓVILES
# ==========================================
cursor_v = pg.InfiniteLine(
    angle=0, movable=True, pos=1.65, 
    pen=pg.mkPen('y', style=QtCore.Qt.DashLine),
    label='Trig: {value:.2f} V', 
    labelOpts={'color': 'y', 'position': 0.05, 'fill': pg.mkBrush(0, 0, 0, 200)}
)
plot.addItem(cursor_v)

cursor_t = pg.InfiniteLine(
    angle=90, movable=True, pos=10.0, 
    pen=pg.mkPen('m', style=QtCore.Qt.DashLine),
    label='{value:.2f} ms', 
    labelOpts={'color': 'm', 'position': 0.95, 'fill': pg.mkBrush(0, 0, 0, 200)}
)
plot.addItem(cursor_t)

buffer_datos = []

# ==========================================
# FUNCIÓN DE LECTURA Y ACTUALIZACIÓN
# ==========================================
def actualizar_grafico():
    global buffer_datos, flanco_trigger, nivel_trigger, tiempo_div_ms, amplitud_div_v
    
    try:
        lineas_leidas = 0
        # Válvula de escape: Máximo 1000 líneas por ciclo para no asfixiar la GUI
        while ser.in_waiting > 0 and lineas_leidas < 1000:
            linea = ser.readline().decode('ascii').strip()
            lineas_leidas += 1
            
            # --- PARSEO DE LA CABECERA DE SINCRONIZACIÓN ---
            if linea.startswith("SYNC"):
                partes = linea.split(',')
                if len(partes) >= 5:
                    flanco_trigger = int(partes[1])
                    nivel_trigger = int(partes[2])
                    tiempo_div_ms = int(partes[3])
                    amplitud_div_v = float(partes[4])
                    
                    # Movemos el cursor horizontal de voltaje automáticamente
                    voltios_trigger = nivel_trigger * (V_REF / 4095.0)
                    cursor_v.setValue(voltios_trigger)
                    
            elif linea.isdigit():
                buffer_datos.append(int(linea))
                
    except (UnicodeDecodeError, ValueError):
        pass 
    except serial.SerialException:
        # Si desconectás el cable, detenemos el reloj y avisamos sin trabar el programa
        timer.stop()
        panel_stats.setText("<span style='color: #FF5555; font-size: 20pt;'>⚠️ CONEXIÓN PERDIDA (Cable desconectado o ESP32 Reiniciado) ⚠️</span>")
        return
            
    # Dibujado de la pantalla
    if len(buffer_datos) >= PUNTOS_PANTALLA:
        ventana_cruda = buffer_datos[-PUNTOS_PANTALLA:]
        
        # Limpiamos TODO el buffer para evitar que se acumule lag ("retraso" de la señal)
        buffer_datos.clear()
        
        y_volts = np.array(ventana_cruda) * (V_REF / 4095.0)
        curve.setData(x_tiempo_ms, y_volts)
        
        # Cálculos de picos de señal
        v_max = np.max(y_volts)
        v_min = np.min(y_volts)
        v_pp = v_max - v_min
        
        # Traducimos el flag del flanco a texto
        txt_flanco = "SUBIDA" if flanco_trigger == 0 else "BAJADA"
        
        # Display HTML con escalas dinámicas
        stats_html = f"""
            <span style='color: #FF5555; margin-right: 20px;'>Vmax: {v_max:.2f}V</span>
            <span style='color: #5555FF; margin-right: 20px;'>Vmin: {v_min:.2f}V</span>
            <span style='color: #55FF55; margin-right: 40px;'>Vpp: {v_pp:.2f}V</span>
            <span style='color: #FFFF55; margin-right: 20px;'>| T: {tiempo_div_ms}ms/div</span>
            <span style='color: #00FFFF; margin-right: 20px;'>A: {amplitud_div_v}V/div</span>
            <span style='color: #FF00FF;'>Flanco: {txt_flanco}</span>
        """
        panel_stats.setText(stats_html)

timer = QtCore.QTimer()
timer.timeout.connect(actualizar_grafico)
timer.start(10)

if __name__ == '__main__':
    print("Iniciando osciloscopio... (Cerrá la ventana para salir)")
    win.show() 
    QtWidgets.QApplication.instance().exec_()
    ser.close()
    print("Puerto serie cerrado.")