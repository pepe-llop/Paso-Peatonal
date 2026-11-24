[README.md](https://github.com/user-attachments/files/35306897/README.md)
# Paso Peatonal Inteligente

**Sistema preventivo de advertencia y detección de cruces de riesgo**

Proyecto del curso **TEI201 Taller de Diseño en Ingeniería** (UAI, 2026-2).
Prototipo de mesa (maqueta con autitos de juguete) basado en una placa **NodeMCU ESP8266**.

---

## Problema

En los pasos peatonales, la falta de atención o de respuesta de algunos conductores frente a la presencia de peatones puede generar situaciones de riesgo. La señalización convencional no siempre entrega información activa sobre la intención de cruzar ni permite registrar lo que ocurre durante una advertencia.

## Solución

Un paso peatonal con dos postes. El peatón presiona un botón para pedir el cruce y el sistema enciende una **advertencia luminosa** (amarillo intermitente). Una **barrera láser** detecta el paso de vehículos. Si un vehículo cruza durante la advertencia, el sistema:

- destella en **rojo** en ambos postes;
- **avisa al celular** con un mensaje, vibración y sonido;
- **registra el evento** con fecha y hora en la memoria de la placa.

Toda la información se ve en una **página web local** a la que se accede por Wi-Fi, sin cámaras y sin recopilar datos personales de los conductores.

---

## Funcionamiento

| Estado | Luz en los postes | Qué lo activa |
|---|---|---|
| Espera | Verde fijo | Estado normal |
| Advertencia peatonal | Amarillo intermitente (15 s) | Un peatón presiona el botón de cualquier poste |
| Cruce de riesgo | Rojo intermitente (2 s) | Un vehículo corta la barrera láser durante la advertencia |

Si un vehículo pasa con la luz en verde, se registra como "auto sin advertencia" y no se considera un riesgo.

---

## Componentes

| Componente | Cantidad | Uso |
|---|---|---|
| NodeMCU ESP8266 (ESP-12E) | 1 | Control del sistema y red Wi-Fi |
| Módulo LED RGB KY-016 | 2 | Semáforo de cada poste |
| Pulsador de 4 patas (12×12 mm) | 2 | Botón peatonal de cada poste |
| Módulo láser KY-008 | 1 | Emisor de la barrera |
| Módulo receptor con fotorresistencia (LM393) | 1 | Detecta cuando se corta el haz |
| Protoboard (principal) | 1 | Conexiones del NodeMCU |
| Mini protoboard | 2 | Montaje de los botones en cada poste |
| Cables jumper (macho-macho y macho-hembra) | — | Conexiones |
| Power bank USB | 1 | Alimentación portátil |

---

## Conexiones

### Pines del NodeMCU

| Pin | Conectado a |
|---|---|
| D0 | R del LED, poste 2 |
| D1 | R del LED, poste 1 |
| D2 | G del LED, poste 1 |
| D5 | Señal de los botones de ambos postes (en paralelo) |
| D6 | Respaldo manual de detección de vehículo (opcional) |
| D7 | Salida DO del receptor láser |
| D8 | G del LED, poste 2 |
| 3V3 | Riel "+" de la protoboard |
| GND | Riel "–" de la protoboard |

### Rieles de alimentación

| Riel | Conexiones |
|---|---|
| **+ (3,3 V)** | Pin 3V3 del NodeMCU, alimentación del láser, VCC del receptor |
| **– (GND)** | GND del NodeMCU, "–" de los dos LED, GND de los dos botones, GND del láser, GND del receptor |

### Notas de montaje

- El pin **B** de ambos KY-016 queda sin conectar.
- Cada botón se conecta usando **dos patas en diagonal**.
- En cada mini protoboard, el "–" del LED de ese poste comparte la columna GND del botón, para bajar un solo cable de GND por poste.
- En nuestro módulo KY-008, las marcas impresas estaban invertidas respecto al funcionamiento real. Hay que verificar la polaridad de cada módulo antes de conectarlo.
- No conectar **Vin** (5 V) al riel "+" ni usar **D8** para botones, porque D8 interviene en el arranque de la placa.

---

## Cómo cargar el código

1. Instalar el driver USB de la placa (**CP210x** de Silicon Labs, o **CH340** según el chip de la placa).
2. En Arduino IDE (o Arduino Cloud Editor), agregar el soporte para ESP8266:
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. Seleccionar la placa **NodeMCU 1.0 (ESP-12E Module)** y el puerto COM correspondiente.
4. En **Flash Size**, elegir una opción con sistema de archivos (por ejemplo **4MB (FS:2MB)**). Sin esto, el historial no se guarda.
5. Abrir `paso_peatonal.ino` y subirlo a la placa.
6. Abrir el Monitor serie a **115200 baudios**.

> Se necesita un cable USB **de datos**. Los cables que solo cargan no permiten que el computador detecte la placa.

---

## Uso

1. Alimentar la placa (USB del computador o power bank). El sistema arranca solo.
2. Conectar un celular a la red Wi-Fi **PasoPeatonal-Demo** (clave **CruceDemo26**).
3. Abrir en el navegador: **http://192.168.4.1**
4. Tocar **"Activar sonido y vibración de alertas"**.

La página muestra:

- el estado actual del paso peatonal;
- las solicitudes peatonales;
- los autos que cruzaron durante la advertencia;
- los autos detectados en total;
- el estado de la barrera láser (OK / CORTADO);
- los últimos eventos registrados, con fecha y hora;
- un botón para **descargar el registro** como planilla CSV (se abre en Excel);
- un botón para **borrar** el registro y los contadores.

---

## Registro de eventos

Cada evento se guarda en la memoria flash del NodeMCU y se conserva aunque la placa se apague. Formato del archivo descargado (separado por `;`):

```
fecha_hora;evento;fuente;solicitudes;cruces_en_advertencia;autos_total
2026-11-20 11:21:05;solicitud_peatonal;boton_poste;1;0;0
2026-11-20 11:21:09;cruce_en_advertencia;laser;1;1;1
```

La placa no tiene reloj propio: la hora se toma del celular al abrir la página. Los eventos ocurridos antes de que se conecte un celular quedan marcados como `sin_hora`.

---

## Calibración de la barrera láser

1. Con el láser apartado, girar el potenciómetro del receptor hasta que su LED DO se apague.
2. Apuntar el láser a la fotorresistencia: el LED DO debe encenderse.
3. Al tapar el haz, el LED DO se apaga y la página muestra **CORTADO**.

Si la luz ambiente interfiere, se puede poner un tubo corto alrededor de la fotorresistencia, apuntando hacia el láser.

---

## Limitaciones

- Es un **prototipo de maqueta** para autitos de juguete; no identifica infracciones de tránsito reales.
- La detección depende de la alineación del láser y de la luz ambiente.
- El aviso en el celular funciona mientras la página está abierta; no es una notificación push.
- La red Wi-Fi es local y no requiere internet.

---

## Estructura del repositorio

```
paso-peatonal-inteligente/
├── README.md
├── paso_peatonal/
│   └── paso_peatonal.ino
└── imagenes/          (fotos del montaje, esquema y maqueta)
```

---

## Integrantes

- Florencia Riveros
- Benjamin Rojas
- Joaquin Diaz
- Jose Maria Llop
