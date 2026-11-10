# IGNIS v4 Ground Control — Stardash UI

Interfaz Ground Control de Ignitia para LASC 2026. La GUI utiliza **PySide6**
(Qt 6) y las gráficas utilizan **pyqtgraph**. Funciona completamente offline:
no carga páginas web, CDNs, fuentes remotas ni servicios externos.

## Instalación en Windows

Abre PowerShell dentro de la carpeta del proyecto:

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
py -m pip install -r requirements.txt
py main.py
```

Dependencias: Python 3.10+, PySide6, pyqtgraph y pyserial.

## Diseño de una sola pantalla

- Tamaño mínimo: **1280 × 720 px**.
- No hay scroll en el dashboard principal.
- Receptor A y Receptor B aparecen como paneles horizontales estáticos.
- Los puertos se asignan pulsando el botón `COMx` de cada receptor. Se abre una
  cuadrícula de botones con todos los dispositivos detectados; no se utilizan
  dropdowns.
- La bitácora detallada vive en la ventana **EVENTOS**, dejando el panel de
  vuelo limpio.
- El logo se carga localmente desde `assets/estrella.svg`.
- La gráfica principal de la derecha muestra **altitud AGL contra tiempo de
  misión**, para leer claramente el perfil de ascenso, apogeo y descenso.
- La columna izquierda contiene RSSI y SNR apiladas verticalmente.
- La franja inferior incluye indicadores independientes para la SD de la
  computadora de vuelo y la SD de la Ground Station.
- El botón `☀/☾` alterna entre modo claro y oscuro. La preferencia queda
  guardada localmente para el siguiente inicio.
- La gráfica de altitud comienza en **0–100 m**, utiliza una ventana inicial de
  **60 s** y amplía automáticamente su escala con 15 % de margen. Ya no queda
  forzada a mostrar 1500 m cuando el cohete apenas está despegando.

## Instrumentos

`gauge.py` contiene dos widgets nativos de Qt:

- `ArcGauge`: semicírculo fino, sin aguja, con animación de 160 ms y lectura
  digital central. Se utiliza para temperatura, velocidad vertical y aceleración.
- `BatteryIndicator`: silueta física de batería, voltaje visible, porcentaje
  estimado y relleno verde/amarillo/naranja/rojo.

Los límites visuales se configuran en `config.py`. Si una lectura supera el
máximo del arco, solo se satura el dibujo: el valor original permanece intacto
en memoria y CSV.

## Paleta

- Fondo: `#090C10`
- Tarjetas: `#161B22`
- Bordes: `#30363D`
- Texto: `#F0F6FC`
- Altitud/trayectoria: `#00B0FF`, `#006374`
- Aceleración/velocidad: `#E15A00`, `#FF9100`
- Online/GPS/apogeo: `#00E676`
- Alertas: `#FF1744`, `#F50057`
- Advertencias: `#FFEA00`

## Nuevo vuelo sin perder datos

El botón **NUEVO VUELO** reinicia el cronómetro, los instrumentos, contadores y
el lienzo visible de las gráficas. No borra:

- `MainWindow.telemetry_history`;
- las series internas completas de `LiveMetricChart`;
- el archivo CSV de la sesión.

El logger vacía primero cualquier paquete pendiente y añade una fila
`NEW_FLIGHT`, de modo que los vuelos puedan separarse durante el análisis.

## Estados de enlace

- `ONLINE`: puerto abierto y telemetría reciente.
- `STALE`: puerto abierto, pero sin telemetría durante más de 5 segundos.
- `OFFLINE`: puerto ausente, cerrado o con error.

Cada tarjeta muestra COM, nombre reportado por el sistema, RSSI, SNR, paquetes,
pérdida y confirmación `IGNIS VERIFIED`. La app sigue trabajando si solo uno de
los receptores recibe el cohete.

## Estados de almacenamiento SD

El parser está sincronizado con la salida real de
`IGNIS_GroundStation_Heavy4_1.ino`. Heavy4 imprime ambos estados dentro del
bloque delimitado por `====`:

```text
SD      FC:LOGGING         GS:/GSLOG000.CSV
```

`FC:LOGGING` se muestra como `FLIGHT SD WRITING`; `FC:NOT logging`, como error.
Una ruta `GS:/GSLOGxxx.CSV` se muestra como `GROUND SD WRITING`; `GS:OFF`, como
error. También se conservó soporte para formatos etiquetados alternativos. Si
la línea no llega, la GUI muestra `NO DATA`; si deja de actualizarse durante
más de 5 segundos, muestra `STALE`. El CSV fusionado conserva ambos estados,
incluidos los reportes Ground SD de A/B.

La batería usa los mismos umbrales que Heavy12/Heavy4: menos de **7.80 V** es
`NO-GO` en plataforma y de **7.80 V a 8.00 V** es advertencia.

Además del subconjunto visible, el parser y el CSV conservan los ejes de
acelerómetro y giroscopio, temperatura/validez de IMU, inclinación, contador de
arranques, causa de reset y disparador de despliegue que imprime Heavy4.

Para asignar puertos hay tres opciones rápidas:

- `AUTO COM` selecciona los dos adaptadores más probables y evita perfiles de
  audio como AirPods;
- las flechas `‹` y `›` cambian de COM directamente desde cada tarjeta;
- pulsar el nombre `COMx` abre la cuadrícula visual completa.

## Estados de vuelo (firmware IGNIS v5 SINGLE-DEPLOY)

La app muestra siempre los nombres de estado de v5, aunque el sketch del
receptor todavía imprima los nombres anteriores (`config.STATE_ALIASES`):

| Código | v5 | Nombre anterior que también se acepta |
|---|---|---|
| 0 | `SAFE` (en rampa, sin continuidad) | `PAD_IDLE` |
| 1 | `ARMED` | |
| 2–4 | `BOOST`, `COAST`, `APOGEE` | |
| 5 | `DESCENT` (bajo paracaídas, despliegue único) | `DROGUE_DESCENT` |
| 6 | sin uso en v5 | `MAIN_DESCENT` |
| 7 | `LANDED` | |
| 8 | `RECOVERY` (el FC se reinició después de aterrizar o sin registro de vuelo; pirotecnia bloqueada) | `UNKNOWN(8)` |

`APOGEE CONFIRMED` se activa con `APOGEE`, `DESCENT`, `MAIN_DESCENT` o
`LANDED`. `RECOVERY` no lo activa, porque también puede ocurrir en rampa; en su
lugar registra el evento `FC_RECOVERY` una vez por vuelo.

## Archivos

```text
telemetry_app_redesign/
├── assets/estrella.svg
├── main.py                 arranque Qt
├── gui.py                  dashboard, receptores y eventos
├── gauge.py                arcos y batería animados
├── charts.py               gráficas e historial persistente
├── receiver.py             lectura serial en QThread
├── serial_manager.py       detección y asignación de puertos
├── telemetry_parser.py     parser tolerante del protocolo
├── logger.py               CSV fusionado A/B
├── styles.py               tema QSS
├── config.py               paleta, límites y regex
├── utilities.py            formato y QSettings
└── requirements.txt
```

La velocidad se calcula mediante diferencia finita de altitud AGL porque el
protocolo actual no transmite directamente ese campo. Para análisis posterior
conviene filtrar la señal almacenada en CSV.
