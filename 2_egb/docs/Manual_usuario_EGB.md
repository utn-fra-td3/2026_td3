# Manual de usuario - EGB Control de posición

**Técnicas Digitales III - EGA / EGB**

Este manual explica cómo utilizar la interfaz web de la EGB desde una PC o un celular y cómo acceder a la aplicación de consola de la Raspberry Pi.

---

## 1. Acceso a la interfaz web

La Raspberry Pi crea su propia red Wi-Fi.

- **Red Wi-Fi (SSID):** `EGA-TD3`
- **Contraseña:** `ega2026td3`
- **Dirección de la Raspberry Pi:** `192.168.50.1`
- **Interfaz web:** `http://192.168.50.1:8080`

### Desde una PC

1. Abrir la lista de redes Wi-Fi.
2. Conectarse a **EGA-TD3**.
3. Ingresar la contraseña `ega2026td3`.
4. Si Windows indica que la red no tiene Internet, continuar igualmente: es una red local para controlar la EGA.
5. Abrir un navegador.
6. Ingresar a `http://192.168.50.1:8080`.

### Desde un celular

1. Abrir la configuración de Wi-Fi.
2. Conectarse a **EGA-TD3**.
3. Ingresar la contraseña `ega2026td3`.
4. Aceptar permanecer conectado aunque el teléfono indique que la red no tiene acceso a Internet.
5. Abrir el navegador e ingresar a `http://192.168.50.1:8080`.

---

## 2. Uso de la aplicación web

La pantalla principal permite supervisar y modificar el funcionamiento de la EGA.

### Posición del motor

Muestra la posición angular actual y el ángulo objetivo.

Para mover el motor:

1. Ingresar un ángulo entre **0° y 360°**.
2. Presionar **Mover**.
3. La nueva posición y la telemetría se actualizarán automáticamente.

### Perfil de movimiento

Permite elegir entre:

- **Escalón**
- **Rampa**

El perfil seleccionado queda indicado en la interfaz.

### Parámetros PID

Se pueden modificar:

- `Kp`
- `Ki`
- `Kd`

Los valores deben ser numéricos y mayores o iguales a cero. Para aplicar los cambios, presionar **Aplicar PID**.

### Telemetría

La interfaz muestra en tiempo real:

- posición actual;
- setpoint;
- error angular;
- PWM;
- dirección;
- estado del movimiento;
- parámetros PID;
- perfil de movimiento.

El indicador superior muestra si existe comunicación con la EGA.

---

## 3. Acceso a la aplicación de consola

La Raspberry Pi también dispone de una aplicación de consola llamada `ega_ctl`.

### Conectarse por SSH

Primero conectarse a la red Wi-Fi **EGA-TD3**. Luego, desde una terminal de la PC:

```bash
ssh lucas@192.168.50.1
```

Ingresar la contraseña del usuario `lucas` de la Raspberry Pi cuando sea solicitada.

### Antes de usar la consola

La interfaz web utiliza el mismo dispositivo `/dev/egalink`. Para evitar que la web y la consola intenten leer la misma respuesta al mismo tiempo, detener temporalmente el servicio web:

```bash
sudo systemctl stop ega-web
```

Luego ejecutar:

```bash
cd ~/ega_egb/console
./ega_ctl
```

No es necesario ejecutar `ega_ctl` con `sudo`.

### Comandos disponibles

Ejemplos:

```text
GET POSITION
GET SETPOINT
GET KP
GET KI
GET KD
GET PROFILE
GET STATUS

SET ANGLE 90
SET KP 6
SET KI 0
SET KD 0.5
SET PROFILE ESCALON
SET PROFILE RAMPA
```

Una operación correcta devuelve `OK` o una respuesta que comienza con `OK`, por ejemplo:

```text
OK POSITION=90.18
```

Los errores se informan como:

```text
ERROR 1 INVALID_COMMAND
ERROR 2 INVALID_VALUE
ERROR 3 DATA_UNAVAILABLE
```

Para salir de la aplicación:

```text
exit
```

Después de salir, volver a iniciar la interfaz web:

```bash
sudo systemctl start ega-web
```

---

## 4. Funcionamiento normal

En el uso habitual no es necesario iniciar manualmente el driver ni el servidor web.

Al encender la Raspberry Pi:

1. se carga el Device Tree Overlay;
2. se carga el driver `ega_link`;
3. aparece `/dev/egalink`;
4. se inicia el servidor web;
5. la Raspberry crea la red Wi-Fi **EGA-TD3**.

Una vez iniciada, el usuario solamente debe conectarse a la red Wi-Fi y abrir:

`http://192.168.50.1:8080`
