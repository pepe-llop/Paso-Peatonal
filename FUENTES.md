# Declaración de fuentes y uso de IA

Proyecto: **Paso Peatonal Inteligente** — TEI201 Taller de Diseño en Ingeniería (UAI, 2026-2)

---

## Librerías utilizadas

Todas vienen incluidas en el paquete oficial **esp8266 by ESP8266 Community** para Arduino. No se instalaron librerías externas adicionales.

| Librería | Versión | Uso en el proyecto | Fuente |
|---|---|---|---|
| ESP8266WiFi | Incluida en el core ESP8266 | Crear la red Wi-Fi propia del dispositivo (modo punto de acceso) | https://github.com/esp8266/Arduino |
| ESP8266WebServer | Incluida en el core ESP8266 | Servidor web que entrega el dashboard y los datos en JSON y CSV | https://github.com/esp8266/Arduino |
| FS (SPIFFS) | Incluida en el core ESP8266 | Guardar el historial de eventos y los contadores en la memoria flash | https://github.com/esp8266/Arduino |
| time.h | Librería estándar de C | Dar formato de fecha y hora a cada evento | Biblioteca estándar de C |

> Completar la columna "Versión" con la versión del paquete ESP8266 que muestra el Gestor de tarjetas de Arduino.

---

## Código externo adaptado

> Completar si se usó código de tutoriales, foros o ejemplos de GitHub. Indicar archivo, función, URL de la fuente y qué se modificó. Si no se usó, escribir: "No se utilizó código externo fuera de lo declarado en la sección de IA."

---

## Uso de Inteligencia Artificial

### Firmware del dispositivo (`firmware/paso_peatonal/paso_peatonal.ino`)
- **Herramienta:** Claude (Anthropic), noviembre de 2026.
- **Punto de partida:** una versión inicial del código con un LED, un botón peatonal (D5) y un botón que simulaba el paso del auto (D6). 
- **Lo que generó la IA a partir de nuestras indicaciones:**
  - Ampliación a dos LED RGB, uno por poste (pines D1/D2 y D0/D8), mostrando el mismo color.
  - Botones de ambos postes conectados en paralelo al mismo pin (D5).
  - Lectura de la barrera láser en D7 (función `nuevaActivacion()` con filtro antirrebote) y registro del paso del auto (función `registrarAuto()`).
  - Dashboard web con aviso visual, vibración y sonido cuando un auto cruza durante la advertencia.
  - Registro persistente de eventos en la memoria flash (funciones `registrarEvento()`, `guardarContadores()`, `cargarContadores()`), sincronización de la hora con el celular y descarga del registro en CSV.
- **Adaptaciones hechas por el equipo:**
  - Pruebas por etapas del hardware real y ajuste del código según los resultados.
  - Cambio de LittleFS a SPIFFS porque el editor web de Arduino no tenía LittleFS disponible.
  - Ajuste del nivel lógico del receptor (`NIVEL_HAZ_CORTADO`) según el funcionamiento del módulo.
  - Eliminación del módulo KY-004 tras comprobar que no respondía; se mantuvo D6 como respaldo manual.
  - Calibración del potenciómetro del receptor y verificación de la polaridad real del láser KY-008, cuyas marcas estaban invertidas.
- **Comprensión:**
  - `nuevaActivacion()` detecta solo el momento en que una entrada cambia a su nivel activo y se mantiene estable un tiempo mínimo, para evitar lecturas falsas por rebote.
  - `loop()` funciona como una máquina de estados: espera (verde), advertencia peatonal (amarillo, 15 s) y cruce de riesgo (rojo, 2 s), usando `millis()` en lugar de `delay()` para no bloquear el servidor web.
  - Cada evento se agrega como una línea al archivo `/eventos.csv` y los contadores se guardan en `/contadores.txt`, por lo que se conservan al apagar la placa.
  - La placa no tiene reloj propio: el celular envía su hora al abrir el dashboard y la placa calcula la hora de cada evento a partir de `millis()`.

### Documentación
- **Herramienta:** Claude (Anthropic), noviembre de 2026.
- **Uso:** borrador del `README.md`, guía de montaje, diagramas de conexiones y apoyo en la depuración del montaje (drivers USB, cableado y calibración). El equipo revisó y ajustó el contenido según el prototipo real.

### Diseño 3D
- **Herramienta:** Claude (Anthropic), noviembre de 2026.
- **Uso:** redacción de un prompt con medidas estimadas para modelar en Fusion 360 los postes y la caja. *Completar qué se modeló finalmente y qué medidas se ajustaron con el pie de metro.*
