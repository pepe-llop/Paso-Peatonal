#include "arduino_secrets.h"

/*
  PASO PEATONAL INTELIGENTE - VERSION MAQUETA CON BARRERA LASER Y REGISTRO
  NodeMCU ESP8266 + 2 KY-016 + 2 pulsadores peatonales + KY-008 + modulo LDR

  FUNCION:
  - Botones de los postes (D5, en paralelo): solicitud peatonal; alerta amarilla 15 s.
  - Barrera laser (receptor con fotorresistencia en D7): detecta el paso de un autito.
  - D6: respaldo manual (boton o cable a GND) si la barrera falla.
  - Si un autito pasa durante la alerta, se cuenta y ambos LED destellan rojo 2 s.
  - Pagina web local por Wi-Fi: estado, contadores, alerta en el celular,
    historial de eventos y descarga del registro como planilla (CSV).
  - Los contadores y el historial se guardan en la memoria flash del NodeMCU
    y se conservan aunque la placa se apague.
  - La hora de cada evento se toma del celular al abrir la pagina
    (el NodeMCU no tiene reloj propio).

  CABLEADO:
  D1 -> R LED poste 1        D2 -> G LED poste 1
  D0 -> R LED poste 2        D8 -> G LED poste 2   (B de ambos LED sin conectar)
  D5 -> puente -> senal de los botones de ambos postes
  D6 -> respaldo manual (opcional)
  D7 -> DO del modulo con fotorresistencia
  3V3 -> riel +  (laser, VCC del receptor)
  GND -> riel -  (GND de cada poste, laser, GND del receptor)
  No conectar 5 V (Vin) al riel + ni usar D8 para botones.
  USO EXCLUSIVO PARA MAQUETA CON AUTITOS DE JUGUETE.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <time.h>

const uint8_t PIN_ROJO_1 = D1;
const uint8_t PIN_VERDE_1 = D2;
const uint8_t PIN_ROJO_2 = D0;
const uint8_t PIN_VERDE_2 = D8;
const uint8_t PIN_BOTON_PEATON = D5;
const uint8_t PIN_BOTON_AUTO = D6;
const uint8_t PIN_RECEPTOR_LASER = D7;

// Modulo con fotorresistencia: entrega HIGH cuando queda a oscuras (haz cortado).
// Si el contador sube al VOLVER el haz y no al cortarlo, cambiar a LOW.
const bool NIVEL_HAZ_CORTADO = HIGH;

const unsigned long DURACION_ALERTA_MS = 15000UL;
const unsigned long DURACION_RIESGO_MS = 2000UL;
const unsigned long ANTIRREBOTE_MS = 40UL;
const unsigned long FILTRO_LASER_MS = 10UL;
const unsigned long PERIODO_PARPADEO_MS = 250UL;

const char* NOMBRE_RED = "PasoPeatonal-Demo";
const char* CLAVE_RED = "CruceDemo26";
ESP8266WebServer servidor(80);

// Archivos en la memoria flash. Separador ";" para que Excel en espanol lo abra en columnas.
const char* ARCHIVO_EVENTOS = "/eventos.csv";
const char* ARCHIVO_CONTADORES = "/contadores.txt";
const char* ENCABEZADO_CSV = "fecha_hora;evento;fuente;solicitudes;cruces_en_advertencia;autos_total";

struct Entrada {
  uint8_t pin;
  bool nivelActivo;          // nivel que significa "boton presionado" o "haz cortado"
  unsigned long filtroMs;    // tiempo que la lectura debe mantenerse estable
  bool lecturaAnterior;
  bool estadoEstable;
  unsigned long ultimoCambio;
};

Entrada botonPeaton = {PIN_BOTON_PEATON, LOW, ANTIRREBOTE_MS, HIGH, HIGH, 0};
Entrada botonAuto = {PIN_BOTON_AUTO, LOW, ANTIRREBOTE_MS, HIGH, HIGH, 0};
Entrada receptorLaser = {PIN_RECEPTOR_LASER, NIVEL_HAZ_CORTADO, FILTRO_LASER_MS,
                         !NIVEL_HAZ_CORTADO, !NIVEL_HAZ_CORTADO, 0};

bool alertaActiva = false;
unsigned long inicioAlerta = 0;
unsigned long inicioRiesgo = 0;
bool riesgoIntermitente = false;
unsigned long solicitudes = 0;
unsigned long crucesEnAlerta = 0;
unsigned long autosDetectados = 0;

bool memoriaLista = false;
bool horaSincronizada = false;
uint32_t baseEpochLocal = 0;   // hora local (en segundos) que corresponde a millis() = 0

// ---------- Entradas ----------

// Devuelve true solo en el instante en que la entrada pasa a su nivel activo
// y se mantiene estable durante el filtro.
bool nuevaActivacion(Entrada &e, unsigned long ahora) {
  bool lectura = digitalRead(e.pin);
  if (lectura != e.lecturaAnterior) {
    e.ultimoCambio = ahora;
    e.lecturaAnterior = lectura;
  }
  if ((ahora - e.ultimoCambio >= e.filtroMs) && lectura != e.estadoEstable) {
    e.estadoEstable = lectura;
    return (lectura == e.nivelActivo);
  }
  return false;
}

// ---------- LED ----------

// Ambos LED muestran siempre el mismo color. KY-016 de catodo comun: HIGH enciende.
void color(bool rojo, bool verde) {
  digitalWrite(PIN_ROJO_1, rojo ? HIGH : LOW);
  digitalWrite(PIN_VERDE_1, verde ? HIGH : LOW);
  digitalWrite(PIN_ROJO_2, rojo ? HIGH : LOW);
  digitalWrite(PIN_VERDE_2, verde ? HIGH : LOW);
}

String estadoTexto() {
  if (!alertaActiva) return "ESPERA";
  if (riesgoIntermitente) return "AUTO_EN_ADVERTENCIA";
  return "ADVERTENCIA_PEATONAL";
}

String hazTexto() {
  return (receptorLaser.estadoEstable == NIVEL_HAZ_CORTADO) ? "CORTADO" : "OK";
}

// ---------- Hora y memoria ----------

String fechaHora() {
  unsigned long seg = millis() / 1000UL;
  if (!horaSincronizada) return "sin_hora_T+" + String(seg) + "s";
  time_t t = (time_t)(baseEpochLocal + seg);
  struct tm* tm = gmtime(&t);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
  return String(buf);
}

void guardarContadores() {
  if (!memoriaLista) return;
  File f = SPIFFS.open(ARCHIVO_CONTADORES, "w");
  if (!f) return;
  f.print(solicitudes); f.print(' ');
  f.print(crucesEnAlerta); f.print(' ');
  f.println(autosDetectados);
  f.close();
}

void cargarContadores() {
  if (!memoriaLista || !SPIFFS.exists(ARCHIVO_CONTADORES)) return;
  File f = SPIFFS.open(ARCHIVO_CONTADORES, "r");
  if (!f) return;
  solicitudes = f.parseInt();
  crucesEnAlerta = f.parseInt();
  autosDetectados = f.parseInt();
  f.close();
}

void registrarEvento(const char* evento, const char* fuente) {
  guardarContadores();
  if (!memoriaLista) return;
  bool nuevo = !SPIFFS.exists(ARCHIVO_EVENTOS);
  File f = SPIFFS.open(ARCHIVO_EVENTOS, "a");
  if (!f) return;
  if (nuevo) f.println(ENCABEZADO_CSV);
  f.print(fechaHora()); f.print(';');
  f.print(evento); f.print(';');
  f.print(fuente); f.print(';');
  f.print(solicitudes); f.print(';');
  f.print(crucesEnAlerta); f.print(';');
  f.println(autosDetectados);
  f.close();
}

void registrarAuto(const char* fuente, unsigned long ahora) {
  autosDetectados++;
  if (alertaActiva) {
    crucesEnAlerta++;
    riesgoIntermitente = true;
    inicioRiesgo = ahora;
    Serial.print("Autito cruzo durante advertencia (");
    registrarEvento("cruce_en_advertencia", fuente);
  } else {
    Serial.print("Autito paso sin advertencia, no se cuenta como riesgo (");
    registrarEvento("auto_sin_advertencia", fuente);
  }
  Serial.print(fuente);
  Serial.println(")");
}

// ---------- Pagina web ----------

const char PAGINA[] PROGMEM = R"HTML(
<!doctype html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Paso peatonal IoT - Demo</title>
<style>
body{font-family:system-ui,Arial,sans-serif;margin:30px auto;max-width:600px;padding:0 18px;background:#f3f6fa;color:#152238}
section{background:white;padding:18px;border-radius:14px;margin:12px 0;border:1px solid #dae3ee}
h1{font-size:25px}.numero{font-size:30px;font-weight:700}.estado{font-size:20px;font-weight:700}
small{color:#596579}
#alerta{display:none;background:#c62828;color:white;padding:18px;border-radius:14px;margin:12px 0;font-size:20px;font-weight:700;text-align:center}
.boton{display:block;width:100%;box-sizing:border-box;padding:14px;margin:8px 0;font-size:17px;border:0;border-radius:12px;color:white;text-align:center;text-decoration:none;font-family:inherit}
#activar{background:#1565c0}#descargar{background:#2e7d32}#borrar{background:#757575}
#registro li{margin:6px 0}.riesgo{color:#c62828;font-weight:700}
</style></head><body>
<h1>Paso peatonal inteligente</h1><p>Prototipo con autitos de juguete</p>
<button id="activar" class="boton" onclick="activarAlertas()">Activar sonido y vibracion de alertas</button>
<div id="alerta">AUTO CRUZO DURANTE LA ADVERTENCIA</div>
<section><small>Estado actual</small><div class="estado" id="estado">Conectando...</div></section>
<section><small>Solicitudes peatonales</small><div class="numero" id="solicitudes">0</div></section>
<section><small>Autos que cruzaron durante la advertencia</small><div class="numero" id="cruces">0</div></section>
<section><small>Autos detectados en total</small><div class="numero" id="autos">0</div></section>
<section><small>Barrera laser</small><div class="estado" id="haz">-</div></section>
<section><small>Ultimos eventos guardados</small><ul id="registro"><li>Sin eventos</li></ul>
<a id="descargar" class="boton" href="/registro.csv" download="registro_paso_peatonal.csv">Descargar registro (planilla CSV)</a>
<button id="borrar" class="boton" onclick="borrarRegistro()">Borrar registro y contadores</button></section>
<p><small>Botones de los postes: solicitud peatonal. Barrera laser: detecta el paso del autito.
Los datos se guardan en el NodeMCU aunque se apague. La hora se toma de este celular.
Este prototipo no identifica infracciones de transito reales.</small></p>
<script>
let ultimosCruces=null;let audio=null;let ocultar=null;
const nombres={solicitud_peatonal:'Solicitud peatonal',cruce_en_advertencia:'AUTO CRUZO DURANTE LA ADVERTENCIA',auto_sin_advertencia:'Auto paso sin advertencia'};
function sincronizarHora(){let local=Math.floor(Date.now()/1000)-new Date().getTimezoneOffset()*60;
fetch('/hora?t='+local).catch(()=>{});}
function activarAlertas(){
try{audio=new (window.AudioContext||window.webkitAudioContext)();}catch(e){}
if(navigator.vibrate)navigator.vibrate(100);
let b=document.getElementById('activar');b.textContent='Alertas activadas';b.style.background='#2e7d32';}
function pitido(){if(!audio)return;
for(let i=0;i<3;i++){let o=audio.createOscillator();let g=audio.createGain();
o.frequency.value=880;o.connect(g);g.connect(audio.destination);
let t=audio.currentTime+i*0.3;g.gain.setValueAtTime(0.3,t);o.start(t);o.stop(t+0.2);}}
function avisar(){
let a=document.getElementById('alerta');a.style.display='block';
clearTimeout(ocultar);ocultar=setTimeout(()=>{a.style.display='none';},5000);
if(navigator.vibrate)navigator.vibrate([400,150,400,150,400]);
pitido();}
async function actualizarRegistro(){try{let r=await fetch('/ultimos');let txt=await r.text();
let lineas=txt.split('\n').map(l=>l.trim()).filter(l=>l.split(';').length===6&&!l.startsWith('fecha'));
let lista=document.getElementById('registro');lista.innerHTML='';
if(lineas.length===0){lista.innerHTML='<li>Sin eventos</li>';return;}
lineas.slice(-10).reverse().forEach(l=>{let c=l.split(';');let li=document.createElement('li');
li.textContent=c[0].replace('sin_hora_','(sin hora) ')+' - '+(nombres[c[1]]||c[1])+' ('+c[2]+')';
if(c[1]==='cruce_en_advertencia')li.className='riesgo';lista.appendChild(li);});
}catch(e){}}
async function borrarRegistro(){if(!confirm('Se borrara todo el historial y los contadores. Continuar?'))return;
await fetch('/borrar');ultimosCruces=0;actualizar();actualizarRegistro();}
async function actualizar(){try{let r=await fetch('/estado');let d=await r.json();
document.getElementById('estado').textContent=d.estado;
document.getElementById('solicitudes').textContent=d.solicitudes;
document.getElementById('cruces').textContent=d.cruces;
document.getElementById('autos').textContent=d.autos;
document.getElementById('haz').textContent=d.haz;
if(ultimosCruces!==null&&d.cruces>ultimosCruces)avisar();
if(ultimosCruces!==d.cruces)actualizarRegistro();
ultimosCruces=d.cruces;
}catch(e){document.getElementById('estado').textContent='Sin conexion';}}
sincronizarHora();actualizar();actualizarRegistro();
setInterval(actualizar,700);setInterval(actualizarRegistro,3000);
</script></body></html>
)HTML";

void prepararWeb() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(NOMBRE_RED, CLAVE_RED)) {
    Serial.println("Error al iniciar Wi-Fi AP");
  }

  servidor.on("/", HTTP_GET, []() {
    servidor.send_P(200, "text/html; charset=utf-8", PAGINA);
  });

  servidor.on("/estado", HTTP_GET, []() {
    String json = "{\"estado\":\"" + estadoTexto() + "\"";
    json += ",\"solicitudes\":" + String(solicitudes);
    json += ",\"cruces\":" + String(crucesEnAlerta);
    json += ",\"autos\":" + String(autosDetectados);
    json += ",\"haz\":\"" + hazTexto() + "\"}";
    servidor.send(200, "application/json", json);
  });

  // El celular envia su hora local (segundos) al abrir la pagina.
  servidor.on("/hora", HTTP_GET, []() {
    if (servidor.hasArg("t")) {
      uint32_t t = strtoul(servidor.arg("t").c_str(), nullptr, 10);
      if (t > 1700000000UL) {
        baseEpochLocal = t - millis() / 1000UL;
        if (!horaSincronizada) Serial.println("Hora tomada del celular: " + fechaHora());
        horaSincronizada = true;
      }
    }
    servidor.send(200, "text/plain", horaSincronizada ? "OK" : "SIN_HORA");
  });

  // Descarga del historial completo como planilla.
  servidor.on("/registro.csv", HTTP_GET, []() {
    if (!memoriaLista || !SPIFFS.exists(ARCHIVO_EVENTOS)) {
      servidor.send(200, "text/csv", String(ENCABEZADO_CSV) + "\n");
      return;
    }
    File f = SPIFFS.open(ARCHIVO_EVENTOS, "r");
    servidor.sendHeader("Content-Disposition", "attachment; filename=registro_paso_peatonal.csv");
    servidor.streamFile(f, "text/csv");
    f.close();
  });

  // Ultimos eventos para mostrar en la pagina (final del archivo).
  servidor.on("/ultimos", HTTP_GET, []() {
    String salida = "";
    if (memoriaLista && SPIFFS.exists(ARCHIVO_EVENTOS)) {
      File f = SPIFFS.open(ARCHIVO_EVENTOS, "r");
      size_t tam = f.size();
      if (tam > 1500) f.seek(tam - 1500, SeekSet);
      salida = f.readString();
      f.close();
    }
    servidor.send(200, "text/plain; charset=utf-8", salida);
  });

  servidor.on("/borrar", HTTP_GET, []() {
    if (memoriaLista) SPIFFS.remove(ARCHIVO_EVENTOS);
    solicitudes = 0;
    crucesEnAlerta = 0;
    autosDetectados = 0;
    guardarContadores();
    Serial.println("Registro y contadores borrados desde la pagina");
    servidor.send(200, "text/plain", "OK");
  });

  servidor.begin();
  Serial.print("Panel Wi-Fi: http://");
  Serial.println(WiFi.softAPIP());
}

// ---------- Programa principal ----------

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ROJO_1, OUTPUT);
  pinMode(PIN_VERDE_1, OUTPUT);
  pinMode(PIN_ROJO_2, OUTPUT);
  pinMode(PIN_VERDE_2, OUTPUT);
  pinMode(PIN_BOTON_PEATON, INPUT_PULLUP);
  pinMode(PIN_BOTON_AUTO, INPUT_PULLUP);
  pinMode(PIN_RECEPTOR_LASER, INPUT_PULLUP);
  color(false, true); // verde: en espera

  // Parte desde el estado real del haz para no contar un auto falso al encender.
  bool hazInicial = digitalRead(PIN_RECEPTOR_LASER);
  receptorLaser.lecturaAnterior = hazInicial;
  receptorLaser.estadoEstable = hazInicial;

  // Memoria flash para el historial (la primera vez se formatea sola).
  memoriaLista = SPIFFS.begin();
  if (!memoriaLista) {
    Serial.println("Preparando memoria por primera vez...");
    SPIFFS.format();
    memoriaLista = SPIFFS.begin();
  }
  Serial.println(memoriaLista ? "Memoria lista" : "Memoria no disponible: el historial no se guardara");
  cargarContadores();

  prepararWeb();
  Serial.println("Prototipo listo. Botones de postes: solicitud. Laser D7 y D6: autito.");
  Serial.print("Contadores guardados -> solicitudes: ");
  Serial.print(solicitudes);
  Serial.print(", cruces en advertencia: ");
  Serial.print(crucesEnAlerta);
  Serial.print(", autos: ");
  Serial.println(autosDetectados);
  Serial.print("Estado inicial del haz: ");
  Serial.println(hazTexto());
}

void loop() {
  servidor.handleClient();
  unsigned long ahora = millis();

  if (nuevaActivacion(botonPeaton, ahora)) {
    if (!alertaActiva) {
      alertaActiva = true;
      inicioAlerta = ahora;
      riesgoIntermitente = false;
      solicitudes++;
      Serial.println("Solicitud peatonal: advertencia activa");
      registrarEvento("solicitud_peatonal", "boton_poste");
    }
  }

  if (nuevaActivacion(botonAuto, ahora)) registrarAuto("boton_D6", ahora);
  if (nuevaActivacion(receptorLaser, ahora)) registrarAuto("laser", ahora);

  if (alertaActiva && (ahora - inicioAlerta >= DURACION_ALERTA_MS)) {
    alertaActiva = false;
    riesgoIntermitente = false;
    Serial.println("Advertencia terminada");
  }
  if (riesgoIntermitente && (ahora - inicioRiesgo >= DURACION_RIESGO_MS)) {
    riesgoIntermitente = false;
  }

  if (!alertaActiva) {
    color(false, true); // verde fijo
  } else if (riesgoIntermitente) {
    bool encendido = ((ahora / PERIODO_PARPADEO_MS) % 2) == 0;
    color(encendido, false); // rojo parpadeante
  } else {
    bool encendido = ((ahora / 500UL) % 2) == 0;
    color(encendido, encendido); // rojo + verde = amarillo parpadeante
  }
}