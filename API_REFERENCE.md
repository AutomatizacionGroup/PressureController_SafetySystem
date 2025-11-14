# API REFERENCE - Pressure Controller Safety System v3.1

Documentación completa de todas las funciones, estructuras y constantes del proyecto.

---

## TABLA DE CONTENIDOS

1. [Estructuras de Datos](#estructuras-de-datos)
2. [Enumeraciones](#enumeraciones)
3. [Constantes](#constantes)
4. [Funciones de Sensor](#funciones-de-sensor)
5. [Funciones de Entrada](#funciones-de-entrada)
6. [Funciones de Display](#funciones-de-display)
7. [Funciones de Menú](#funciones-de-menú)
8. [Funciones de Almacenamiento](#funciones-de-almacenamiento)
9. [Funciones de Control](#funciones-de-control)

---

## ESTRUCTURAS DE DATOS

### `struct SystemState`
**Ubicación:** Línea 187
**Propósito:** Almacena timestamps de últimas actualizaciones para timing no-bloqueante

**Miembros:**
```cpp
struct SystemState {
  unsigned long lastOperationUpdate;    // Último update de operationMode() [ms]
  unsigned long lastSerialDebug;        // Último debug por serial [ms]
  unsigned long lastSensorCheck;        // Último chequeo de sensor [ms]
  unsigned long lastDisplayUpdate;      // Último update de display [ms]
};
```

**Uso:**
```cpp
if (millis() - sysState.lastSensorCheck < SENSOR_CHECK_INTERVAL) return;
sysState.lastSensorCheck = millis();  // Marcar tiempo de última ejecución
```

---

### `struct Config`
**Ubicación:** Línea 88
**Propósito:** Almacena configuración persistente en EEPROM/Preferences

**Miembros:**
```cpp
struct Config {
  byte version;                   // Version de estructura (para upgrades)
  int pressMin;                   // Presión mínima permitida [psi]
  int pressMax;                   // Presión máxima permitida [psi]
  int setpoint;                   // Punto de ajuste deseado [psi]
  int deadband;                   // Banda muerta para hysteresis [psi]
  int minOnTime;                  // Tiempo mínimo ON de bomba [segundos]
  bool valveImplemented;          // ¿Válvula de entrada implementada?
  bool tankSensorImplemented;     // ¿Sensor de tanque bajo implementado?
  byte checksum;                  // CRC-8 para validación
};
```

**Rango de valores válidos:**
| Campo | Min | Max | Unidad | Default |
|-------|-----|-----|--------|---------|
| pressMin | -100 | 490 | psi | -20 |
| pressMax | 10 | 500 | psi | 200 |
| setpoint | 10 | 100 | psi | 50 |
| deadband | 5 | 40 | psi | 10 |
| minOnTime | 1 | 10 | seg | 5 |

**Persistencia:**
- **ESP32**: Guardado en `Preferences` con namespace "pressure"
- **Arduino AVR**: Guardado en `EEPROM` dirección 0

---

### `struct MenuConfig`
**Ubicación:** Línea 1323
**Propósito:** Define parámetros de cada menú para función genérica (refactorización v3.2)

**Miembros:**
```cpp
typedef struct {
  const char* title;        // Título mostrado en header
  const char* label;        // Label del parámetro
  const char* unit;         // Unidad (psi, seg, etc)
  int* value;               // Puntero al valor en config_temp
  int minVal;               // Valor mínimo permitido
  int maxVal;               // Valor máximo permitido
  int step;                 // Paso del encoder (1 o 5)
  int displayMax;           // Máximo para barra de progreso
  bool isBoolean;           // Si es booleano (SI/NO) o numérico
  bool showProgressBar;     // Si mostrar barra visual
} MenuConfig;
```

---

### `struct AlarmLog`
**Ubicación:** Línea 204
**Propósito:** Registro circular de últimas 10 alarmas para debugging

**Miembros:**
```cpp
struct AlarmLog {
  unsigned long timestamp;  // Tiempo de alarma [ms desde startup]
  SensorStatus status;      // Estado del sensor (ver enum)
  int adcValue;             // Lectura ADC en el momento
};
```

**Array global:**
```cpp
AlarmLog alarmHistory[MAX_ALARM_LOG];  // Buffer circular de 10 eventos
int alarmHistoryIndex = 0;             // Índice para inserción circular
```

---

## ENUMERACIONES

### `enum SystemMode`
**Ubicación:** Línea 79
**Propósito:** Estados principales de la máquina de estados

```cpp
enum SystemMode {
  MODE_OPERATION,     // Operación normal
  MODE_MENU_LIST,     // Seleccionar menú
  MODE_MENU_CONFIG    // Configurar parámetro
};
```

**Transiciones:**
```
MODE_OPERATION → (long press) → MODE_MENU_LIST → (short press) → MODE_MENU_CONFIG
MODE_MENU_CONFIG → (HOLD) → MODE_MENU_LIST → (HOLD) → MODE_OPERATION
```

---

### `enum SensorStatus`
**Ubicación:** Línea 148
**Propósito:** Estados de validación del sensor de presión

```cpp
enum SensorStatus {
  SENSOR_OK,             // Lectura válida 0.5-4.5V
  SENSOR_DISCONNECTED,   // Desconectado (<0.3V)
  SENSOR_LOW_SIGNAL,     // Señal baja (0.3-0.5V)
  SENSOR_HIGH_SIGNAL,    // Señal alta (4.5-4.8V)
  SENSOR_SHORT,          // Cortocircuito (>4.8V)
  SENSOR_ERRATIC         // Cambios erráticos (>1024 ADC en 50ms)
};
```

**Acciones por estado:**
| Estado | Acción |
|--------|--------|
| OK | Operación normal |
| DISCONNECTED | Bloquea bomba, alarma |
| LOW_SIGNAL | Bloquea bomba, alarma |
| HIGH_SIGNAL | Bloquea bomba, alarma |
| SHORT | Bloquea bomba, alarma |
| ERRATIC | 3 confirmaciones en 3s, luego bloquea |

---

## CONSTANTES

### Constantes de Timing [ms]

```cpp
const unsigned long OPERATION_UPDATE_INTERVAL = 100;    // Loop de operación
const unsigned long SENSOR_CHECK_INTERVAL = 50;         // Chequeo ADC
const unsigned long SERIAL_DEBUG_INTERVAL = 5000;       // Output serial
const unsigned long DISPLAY_UPDATE_INTERVAL = 200;      // Actualizar OLED
```

### Constantes de Digitales Debounce [ms]

```cpp
const unsigned long INLET_DEBOUNCE_TIME = 100;          // Válvula entrada (D7)
const unsigned long TANK_DEBOUNCE_TIME = 100;           // Tanque bajo (D6)
const unsigned long ENCODER_DEBOUNCE_TIME = 5;          // Encoder rotativo
```

### Constantes de Sensor Fault Detection [ms]

```cpp
const unsigned long VALVE_FAULT_THRESHOLD = 10000;      // 10 segundos D7 HIGH
const unsigned long ERRATIC_CONFIRMATION_TIME = 3000;   // 3 segundos hysteresis
const int ERRATIC_CONFIRMATION_THRESHOLD = 3;           // 3 muestras para confirmar
```

### Constantes ADC Platform-Specific

**ESP32 (12-bit, 3.3V ref):**
```cpp
#define ADC_DISCONNECTED  250    // <0.3V
#define ADC_LOW_WARNING   410    // 0.5V
#define ADC_HIGH_WARNING  3686   // 4.5V
#define ADC_SHORT_CIRCUIT 3932   // >4.8V
#define ADC_MAX_CHANGE    1024   // Máximo cambio permitido
```

**Arduino AVR (10-bit, 5V ref):**
```cpp
#define ADC_DISCONNECTED  61     // <0.3V
#define ADC_LOW_WARNING   102    // 0.5V
#define ADC_HIGH_WARNING  921    // 4.5V
#define ADC_SHORT_CIRCUIT 983    // >4.8V
#define ADC_MAX_CHANGE    256    // Máximo cambio permitido
```

### Constantes de Recuperación

```cpp
const int RECOVERY_CYCLES = 10;  // Ciclos buenos para recuperar de error
```

---

## FUNCIONES DE SENSOR

### `SensorStatus checkSensor(int adcValue)`
**Ubicación:** Línea 293
**Propósito:** Validar lectura ADC con hysteresis y detección de cambios erráticos

**Parámetros:**
- `adcValue` (int): Lectura del ADC (0-4095 ESP32, 0-1023 AVR)

**Retorna:**
- (SensorStatus): Estado del sensor (ver enum)

**Lógica:**
1. Chequea límites de voltaje (desconectado, bajo, alto, cortocircuito)
2. Si está OK, verifica cambios erráticos vs lectura anterior
3. Si cambio > ADC_MAX_CHANGE: inicia confirmación (3 muestras en 3s)
4. Si 3 confirmaciones: retorna SENSOR_ERRATIC

**Ejemplo:**
```cpp
int adcReading = analogRead(ANALOG_PIN);
SensorStatus status = checkSensor(adcReading);
if (status != SENSOR_OK) {
  pumpBlocked = true;  // Bloquear bomba si hay error
}
```

---

### `void checkInletValve()`
**Ubicación:** Línea 469
**Propósito:** Monitorear D7 (válvula de entrada) y detectar desconexión

**Funcionamiento:**
- Lee D7: LOW = válvula abierta, HIGH = cerrada
- Si valveImplemented = true:
  - Detecta si D7 HIGH por >10 segundos = sensor probablemente desconectado
  - Setea `valveFaultDetected = true` y `pumpBlocked = true`
- Auto-recupera cuando D7 va LOW (sensor responde)

**Estado Global:**
```cpp
bool inletValveOpen;        // D7 actualmente abierta
bool valveFaultDetected;    // Sensor desconectado detectado
unsigned long valveHighStartTime;  // Timestamp cuando D7 fue HIGH
bool hasValveStateChanged;  // Flag de cambio de estado
```

**Seguridad:**
- Si implementada pero desconectada → BOMBA BLOQUEADA
- Múltiples interlocks previenen cavitación

---

### `void checkTankLevel()`
**Ubicación:** Línea 546
**Propósito:** Monitorear D6 (sensor nivel bajo de tanque)

**Funcionamiento:**
- Lee D6: LOW = tanque OK, HIGH = tanque vacío/bajo
- Si tankSensorImplemented = true:
  - Detecta transición LOW→HIGH (tanque bajando)
  - Setea `tankLevelLow = true`
  - Bloquea bomba para prevenir cavitación
- Debounce de 100ms previene falsos positivos

**Estado Global:**
```cpp
bool tankLevelLow;              // Tanque bajo detectado
bool lastTankLevelState;        // Estado anterior
unsigned long lastTankLevelChange;  // Timestamp último cambio
```

---

### `void handleSensorRecovery()`
**Ubicación:** Línea 582
**Propósito:** Sistema de recuperación automática después de error de sensor

**Lógica:**
1. Si estado OK: incrementar `errorRecoveryCount`
2. Si `errorRecoveryCount >= RECOVERY_CYCLES` (10):
   - Setear `pumpBlocked = false`
   - `sensorStatus = SENSOR_OK`
   - Reset counters
3. Si error detectado: reset a 0

**Tiempo de recuperación:**
```
SENSOR_CHECK_INTERVAL=50ms × RECOVERY_CYCLES=10 = ~500ms mínimo
```

---

### `void logAlarm(SensorStatus status, int adcValue)`
**Ubicación:** Línea 237
**Propósito:** Registrar alarma en buffer circular

**Parámetros:**
- `status`: Estado del sensor que causó la alarma
- `adcValue`: Lectura ADC en el momento

**Buffer:**
- MAX_ALARM_LOG=10 eventos
- Overwrite automático cuando lleno (circular)
- Útil para debugging en Serial Monitor

---

## FUNCIONES DE ENTRADA

### `void readEncoder()`
**Ubicación:** Línea 826
**Propósito:** ISR (Interrupt Service Routine) para encoder rotativo

**Funcionamiento:**
- Ejecutada en cambio de D2 (ENCODER_CLK)
- Detecta dirección rotativa (CW/CCW)
- Incrementa/decrementa `encoderPos`

**Debounce:**
- ENCODER_DEBOUNCE_TIME = 5ms previene rebotes

---

### `void checkButton()`
**Ubicación:** Línea 828
**Propósito:** Detectar presiones corta y larga del encoder

**Estados:**
```
SHORT_PRESS: <1 segundo  → Seleccionar/Editar
LONG_PRESS:  >=1 segundo → Cambiar modo menú
```

**Global:**
```cpp
unsigned long lastButtonPress;  // Timestamp del último press
const unsigned long longPressTime = 1000;  // 1 segundo
```

---

### `void handleShortPress()`
**Ubicación:** Línea 882
**Propósito:** Router de acciones en presión corta

**Lógica de Estados:**
```
MODE_OPERATION:
  → Iniciar navegación de menú

MODE_MENU_LIST:
  → Entrar a edición del menú seleccionado

MODE_MENU_CONFIG:
  → Avanzar a siguiente parámetro o guardar
```

---

## FUNCIONES DE DISPLAY

### `void drawHeader(const char* text)`
**Ubicación:** Línea 645
**Propósito:** Dibujar encabezado con fondo invertido

**Parámetros:**
- `text`: String a mostrar en header (máx. 21 caracteres)

**Estilo:**
- Fondo blanco, texto negro, textSize=1
- Línea separadora en y=10

---

### `void drawFooter(const char* text)`
**Ubicación:** Línea 655
**Propósito:** Dibujar pie de página con instrucciones

**Parámetros:**
- `text`: Instrucciones (ej. "[OK] Editar  [HOLD] Menu")

**Ubicación:** y=58 (parte inferior de display 64px)

---

### `void drawProgressBar(int x, int y, int w, int h, int val, int minVal, int maxVal)`
**Ubicación:** Línea 663
**Propósito:** Barra de progreso visual

**Parámetros:**
- `x, y`: Posición superior-izquierda
- `w, h`: Ancho y alto
- `val`: Valor actual
- `minVal, maxVal`: Rango para escala

---

### `void displayOperation()`
**Ubicación:** Línea 1062
**Propósito:** Pantalla principal de operación

**Componentes:**
- Header con estado actual
- Valor de presión grande (textSize=2)
- Iconos gráficos: válvula, tanque, bomba
- Barra de progreso de presión
- Configuración (SP, DB, MIN)

**Update:** Cada 200ms si ha cambiado algo

---

### `void displayMenuGeneric(int menuId)`
**Ubicación:** Línea 1358
**Propósito:** Función genérica para mostrar menús (refactorización v3.2)

**Parámetros:**
- `menuId`: 2-6 (Menu 1 es especial con 2 parámetros)

**Modo Visualización:**
- Muestra valor actual grande
- Instrucciones: "[OK] Editar  [HOLD] Menu"

**Modo Edición:**
- Recuadro blanco con valor invertido
- Instrucciones: "[OK] Guardar"
- Ajuste con encoder (step en tabla)

---

### `void displayMenu1()`
**Ubicación:** Línea 1438
**Propósito:** Menú especial para rango de presión (2 parámetros)

**Parámetros editables:**
1. pressMin: -100 a (pressMax-10), step=5
2. pressMax: (pressMin+10) a 500, step=5

**Visualización:**
- Barra de progreso mostrando rango

---

## FUNCIONES DE MENÚ

### `void menuListMode()`
**Ubicación:** Línea 1225
**Propósito:** Pantalla de selección de menú

**Menús disponibles:**
1. RANGO DE ENTRADA (pressMin/Max)
2. PUNTO DE AJUSTE (setpoint)
3. BANDA MUERTA (deadband)
4. TIEMPO MINIMO (minOnTime)
5. VALVULA (implementada SI/NO)
6. SENSOR TANQUE (implementado SI/NO)
7. SALIR

**Scroll:**
- Muestra máx. 5 items
- Auto-scrollea cuando menuSelection > 2
- Indicadores ^ (arriba) v (abajo)

---

### `void menuConfigMode()`
**Ubicación:** Línea 1306
**Propósito:** Router a función correcta según menú

**Switch case:**
```cpp
case 1: displayMenu1(); break;  // Especial
case 2: displayMenuGeneric(2); break;
case 3: displayMenuGeneric(3); break;
case 4: displayMenuGeneric(4); break;
case 5: displayMenuGeneric(5); break;
case 6: displayMenuGeneric(6); break;
```

---

### `void handleMenuNavigation()`
**Ubicación:** Línea 1563
**Propósito:** Guardar configuración cuando usuario presiona OK

**Lógica:**
1. Validar con `validateConfig()`
2. Si válida: copiar config_temp → config
3. Guardar con `saveConfig()`
4. Mostrar animación de guardado
5. Volver a MODE_MENU_LIST

---

## FUNCIONES DE ALMACENAMIENTO

### `bool validateConfig(Config& cfg)`
**Ubicación:** Línea 1532
**Propósito:** Validar rango de configuración antes de guardar

**Validaciones:**
```cpp
pressMin < pressMax - 10
setpoint 10-100 psi
deadband 5-40 psi
minOnTime 1-10 segundos
```

**Retorna:**
- `true` si válida
- `false` si inválida (logs error a Serial)

---

### `void loadConfig()`
**Ubicación:** Línea 1804
**Propósito:** Cargar configuración desde almacenamiento persistente

**Flujo:**
1. ESP32: Lee `Preferences` namespace "pressure"
2. AVR: Lee `EEPROM` dirección 0
3. Verifica version y checksum
4. Si inválida: regenera defaults automáticamente

**Serial output:**
```
✓ Configuracion cargada desde Preferences (v2)
╔════════════════════════════════════════╗
║     CONFIGURACION ACTUAL CARGADA      ║
...
```

---

### `void saveConfig()`
**Ubicación:** Línea 1884
**Propósito:** Guardar configuración a almacenamiento persistente

**Flujo:**
1. Setear version = CONFIG_VERSION (2)
2. Calcular checksum CRC-8
3. ESP32: putBool/putInt a Preferences
4. AVR: EEPROM.put() con checksum

**Serial output:**
```
✓ Configuracion guardada en Preferences (v2)
```

---

### `void loadDefaults()`
**Ubicación:** Línea 1908
**Propósito:** Cargar valores por defecto

**Valores por defecto:**
```cpp
pressMin:  -20 psi
pressMax:  200 psi
setpoint:  50 psi
deadband:  10 psi
minOnTime: 5 seg
valveImplemented:   true
tankSensorImplemented: true
```

---

### `byte calculateChecksum()`
**Ubicación:** Línea 1920
**Propósito:** Calcular CRC-8 para validación de datos

**Algoritmo:**
```
CRC-8 con polinomio 0x07
Valor inicial: 0xAB
Procesa todos los bytes del struct Config excepto checksum
```

**Precisión:**
- CRC-8 detecta errores con ~99.6% confiabilidad
- Mejor que simple suma de bytes

---

### `void clearStoredConfig()`
**Ubicación:** Línea 1940
**Propósito:** Factory reset - limpiar almacenamiento persistente

**Funcionamiento:**
1. ESP32: `preferences.clear()`
2. AVR: Llena EEPROM con 0xFF
3. Carga defaults automáticamente

**Uso:** Presionar HOLD varias veces en startup

---

## FUNCIONES DE CONTROL

### `void operationMode()`
**Ubicación:** Línea 915
**Propósito:** Loop principal de operación - control no-bloqueante

**Timing gates:**
- Sensor check: cada 50ms
- Control de operación: cada 100ms
- Display update: cada 200ms
- Serial debug: cada 5s

**Seguridad triple-check:**
```cpp
if (pumpBlocked || inletValveOpen || tankLevelLow) {
  digitalWrite(PUMP_PIN, LOW);  // BOMBA OFF (OBLIGATORIO)
}
```

**Estados monitoreados:**
- Válvula abierta (D7)
- Tanque bajo (D6)
- Sensor error
- Recuperación en progreso

---

### `void resetValveTracking()`
**Ubicación:** Línea 1874
**Propósito:** Resetear tracking de válvula cuando se carga config

**Resetea:**
```cpp
valveFaultDetected = false
valveHighStartTime = 0
hasValveStateChanged = false
inletValveOpen = false
```

---

### `void showSaveAnimation()`
**Ubicación:** Línea 687
**Propósito:** Animación visual de guardado (3 círculos)

**Duración:** ~1 segundo

---

## MAPA DE MEMORIA

### ESP32 Nano S3
```
Flash:  388 KB / 1310 KB (29%)  ← Espacio suficiente
SRAM:   23.8 KB / 327 KB (7%)   ← Espacio suficiente
```

### Arduino Nano (INCOMPATIBLE v3.1+)
```
Flash:  Overflow (>30.7 KB disponibles)
SRAM:   Overflow (>2 KB disponibles)
```

---

## NOTAS DE IMPLEMENTACIÓN

### Non-Blocking Timing
- Usa `millis()` para comparaciones, nunca `delay()`
- Permite responsividad de menú mientras monitorea sensores
- 4 intervalos independientes para diferentes tareas

### Hysteresis Sensor
- 3 confirmaciones en 3 segundos previenen falsas alarmas
- Ideal para ambientes con ruido eléctrico
- CRC-8 checksum valida integridad

### Persistencia
- Version control automático detecta cambios de estructura
- Factory reset si mismatch de versión
- Ambas plataformas (ESP32 + AVR) soportadas

---

**Documento generado:** v3.1
**Última actualización:** 2024-11-13
