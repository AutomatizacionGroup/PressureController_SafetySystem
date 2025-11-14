# DEVELOPER GUIDE - Pressure Controller Safety System v3.1

Guía para desarrolladores que deseen extender, modificar o mantener el proyecto.

---

## 📋 TABLA DE CONTENIDOS

1. [Antes de Empezar](#antes-de-empezar)
2. [Estructura del Código](#estructura-del-código)
3. [Patrones de Diseño](#patrones-de-diseño)
4. [Agregar Nuevas Características](#agregar-nuevas-características)
5. [Testing y Validación](#testing-y-validación)
6. [Versioning y Releases](#versioning-y-releases)
7. [Mejores Prácticas](#mejores-prácticas)

---

## ANTES DE EMPEZAR

### Prerequisitos
- Arduino IDE 1.8.19+  OR  arduino-cli 0.33+
- Librerías instaladas:
  - Adafruit GFX Library
  - Adafruit SSD1306
  - Preferences (ESP32 built-in)
  - EEPROM (Arduino AVR built-in)

### Compilación Rápida
```bash
# ESP32 (primary target)
arduino-cli compile --fqbn esp32:esp32:esp32s3 PressureController_SafetySystem.ino

# Arduino Nano (INCOMPATIBLE en v3.1+)
arduino-cli compile --fqbn arduino:avr:nano PressureController_SafetySystem.ino
```

### Upload a Dispositivo
```bash
# ESP32
arduino-cli upload -p COM13 --fqbn esp32:esp32:esp32s3

# Serial Monitor
arduino-cli monitor -p COM13 --config baudrate=115200
```

---

## ESTRUCTURA DEL CÓDIGO

### Organización Lógica

```
PressureController_SafetySystem.ino (1,950+ líneas)
├── [1-40]       Comentarios y metadatos
├── [29-64]      #includes y configuración de display
├── [41-50]      Definiciones de pines
├── [53-57]      Resolución ADC por plataforma
├── [59-64]      Configuración OLED
├── [66-193]     Variables globales y structs
├── [195-228]    Constantes de timing y ADC
├── [234-374]    Sistema de sensores (checkSensor, hysteresis)
├── [376-467]    Validación de sensor - alarmas
├── [469-580]    Entrada digital (D7 válvula, D6 tanque)
├── [582-705]    Recuperación automática, animaciones
├── [707-776]    Setup
├── [778-1060]   Loop principal
├── [826-911]    Input (encoder, botones)
├── [915-1221]   Modo operación (displayOperation)
├── [1225-1562]  Sistema de menús (navegación)
├── [1321-1436]  Funciones genéricas de display (v3.2)
├── [1563-1748]  Manejo de menús (handleMenuNavigation)
├── [1804-1966]  Persistencia (loadConfig, saveConfig)
└── [FINAL]      Funciones de utilidad
```

### Módulos Conceptuales

| Módulo | Líneas | Propósito | Criticidad |
|--------|--------|----------|-----------|
| **Sensor Validation** | 285-374 | Hysteresis + ADC checks | CRÍTICA |
| **Digital Input** | 469-580 | D7/D6 monitoring | CRÍTICA |
| **Operation Loop** | 915-1060 | Main control logic | CRÍTICA |
| **Display/UI** | 1062-1436 | OLED rendering | MEDIA |
| **Menu System** | 1225-1748 | Config navigation | MEDIA |
| **Persistance** | 1804-1966 | EEPROM/Preferences | MEDIA |

---

## PATRONES DE DISEÑO

### 1. Non-Blocking Timing Loop

**Patrón utilizado:**
```cpp
static unsigned long lastUpdate = 0;
const unsigned long INTERVAL = 100;  // ms

if (millis() - lastUpdate < INTERVAL) {
  return;  // Skip this iteration
}
lastUpdate = millis();
// Realizar acción crítica
```

**Por qué:** Previene bloqueos que causarían timeouts del watchdog y menúes congelados.

**Aplicación:** Used en operationMode(), sensor checks, display updates.

---

### 2. Hysteresis con Confirmación

**Patrón utilizado:**
```cpp
// Detecta cambios erráticos (ruido)
if (change > ADC_MAX_CHANGE) {
  if (!waitingConfirmation) {
    waitingConfirmation = true;
    confirmationCount = 1;
  } else {
    confirmationCount++;
    if (confirmationCount >= 3) {
      // Confirmar como alarma después de 3 muestras
    }
  }
}
```

**Por qué:** Reduce falsas alarmas en ambiente ruidoso.

**Aplicación:** checkSensor() para validación ADC.

---

### 3. Multiple Independent Safety Interlocks

**Patrón utilizado:**
```cpp
if (sensorError || valveOpen || tankLow || recoveryInProgress) {
  digitalWrite(PUMP_PIN, LOW);  // BOMBA OFF - SIN EXCEPCIONES
}
```

**Por qué:** Redundancia de seguridad. Si UNA condición falla, bomba se apaga.

**Aplicación:** operationMode() línea 967-975.

---

### 4. Struct + Generic Function (Refactorización v3.2)

**Patrón utilizado:**
```cpp
MenuConfig menuConfigs[] = {
  {"SETPOINT", "Setpoint", "psi", &config_temp.setpoint, 10, 100, 1, 100, false, true},
  {"DEADBAND", "Deadband", "psi", &config_temp.deadband, 5, 40, 1, 40, false, false},
};

void displayMenuGeneric(int id) {
  MenuConfig cfg = menuConfigs[id];
  // Código común para TODOS los menús
}
```

**Por qué:** Elimina duplicación de código (era 362 líneas, ahora 98).

**Aplicación:** Menús 2-6 usan displayMenuGeneric().

---

### 5. Version Control para Config

**Patrón utilizado:**
```cpp
#define CONFIG_VERSION 2

if (loadedVersion != CONFIG_VERSION) {
  // Estructura cambió - regenerar defaults automáticamente
  loadDefaults();
  saveConfig();
}
```

**Por qué:** Manejo automático de upgrades sin corromper EEPROM.

**Aplicación:** loadConfig() línea 1839.

---

## AGREGAR NUEVAS CARACTERÍSTICAS

### Agregar Nuevo Sensor Digital (ej. humedad)

**Paso 1:** Definir pin
```cpp
#define HUMIDITY_PIN 5  // D5
```

**Paso 2:** En setup()
```cpp
pinMode(HUMIDITY_PIN, INPUT);
```

**Paso 3:** Crear función de chequeo con debounce
```cpp
bool humidityHigh = false;
const unsigned long HUMIDITY_DEBOUNCE = 100;
unsigned long lastHumidityChange = 0;

void checkHumidity() {
  bool reading = digitalRead(HUMIDITY_PIN);  // HIGH = humidity high

  if (reading != lastHumidityState) {
    if (millis() - lastHumidityChange > HUMIDITY_DEBOUNCE) {
      humidityHigh = reading;
      lastHumidityChange = millis();
    }
  }
}
```

**Paso 4:** Llamar en operationMode()
```cpp
if (millis() - sysState.lastSensorCheck < SENSOR_CHECK_INTERVAL) return;
checkInletValve();
checkTankLevel();
checkHumidity();  // ← NUEVO
sysState.lastSensorCheck = millis();
```

**Paso 5:** Agregar interlocks de seguridad
```cpp
if (pumpBlocked || inletValveOpen || tankLevelLow || humidityHigh) {
  digitalWrite(PUMP_PIN, LOW);
}
```

---

### Agregar Nuevo Menú de Configuración

**Paso 1:** Agregar a Config struct
```cpp
struct Config {
  // ... campos existentes ...
  int humidityThreshold;  // ← NUEVO CAMPO
};
```

**⚠️ CRÍTICO:** Incrementar CONFIG_VERSION
```cpp
#define CONFIG_VERSION 3  // era 2
```

**Paso 2:** Agregar default
```cpp
const int DEFAULT_HUMIDITY_THRESHOLD = 80;
```

**Paso 3:** Agregar a MenuConfig tabla
```cpp
MenuConfig menuConfigs[] = {
  // ... menus existentes ...
  // MENU 7: Humidity Threshold
  {"HUMEDAD MAXIMA", "Threshold", "%", &config_temp.humidityThreshold, 30, 100, 5, 100, false, true},
};
```

**Paso 4:** En menuConfigMode() switch
```cpp
switch (currentMenu) {
  case 1: displayMenu1(); break;
  case 2: displayMenuGeneric(2); break;
  // ...
  case 7: displayMenuGeneric(7); break;  // ← NUEVO
}
```

**Paso 5:** En handleShortPress()
```cpp
if (menuSelection > 7) menuSelection = 7;  // Actualizar límite
```

**Paso 6:** En loadConfig()
```cpp
#ifdef ESP32
  config.humidityThreshold = preferences.getInt("humidityThreshold", DEFAULT_HUMIDITY_THRESHOLD);
#endif
```

**Paso 7:** En saveConfig()
```cpp
preferences.putInt("humidityThreshold", config.humidityThreshold);
```

**Paso 8:** En loadDefaults()
```cpp
config.humidityThreshold = DEFAULT_HUMIDITY_THRESHOLD;
```

---

### Agregar Más Parámetros al Sensor Existente

**Scenario:** Quieres agregar "minOffTime" (tiempo mínimo OFF)

**Paso 1:** Agregar a Config
```cpp
int minOffTime;  // Tiempo mínimo que bomba debe estar OFF
```

**Paso 2:** Incrementar CONFIG_VERSION
```cpp
#define CONFIG_VERSION 3
```

**Paso 3:** Seguir los mismos pasos que "Agregar Nuevo Menú"

**Nota:** El sistema detectará automáticamente el cambio de versión y regenerará defaults.

---

## TESTING Y VALIDACIÓN

### Test de Persistencia (Manual)

```
1. Compilar y subir código
2. Abrir Serial Monitor (115200 baud)
3. En menú: cambiar Menu 5 (Valvula) SI → NO
4. Serial mostrará: "Configuracion guardada en Preferences (v2)"
5. DESENCHUFAR completamente
6. ENCHUFAR nuevamente
7. Serial mostrará: "Configuracion cargada desde Preferences (v2)"
8. VERIFICAR: Valve=NO (no volvió a YES)
```

**✅ ÉXITO:** Si Valve persiste, persistencia funciona.

---

### Test de Sensor Fault Detection

```
1. Compilar y subir
2. En OLED: Verificar "V:OK" (válvula detectada)
3. Desconectar cable D7 de válvula
4. Esperar 10 segundos
5. OLED debe mostrar "V:ERR" y bomba debe bloquearse
6. Reconectar D7
7. OLED debe volver a "V:OK"
```

---

### Test de Safety Interlocks

```
1. Verificar MANUAL que bomba se bloquea en cada condición:
   a. Sensor error
   b. Válvula entrada abierta (D7 LOW)
   c. Tanque bajo (D6 HIGH)
   d. Recuperación en progreso

2. Simulación: Tocar sensor (crear error ADC)
3. Verificar: Serial muestra alarma, bomba OFF
```

---

## VERSIONING Y RELEASES

### Convención de Versiones

```
v3.1 - Release actual (ESP32 primary, Arduino Nano incompatible)
v3.0 - Previous (ambas plataformas soportadas)
v2.x - Legacy
```

### Changelog Template (próximo release)

```markdown
# v3.2 - [Descripción]

## Features
- [ ] Nueva característica 1
- [ ] Nueva característica 2

## Bug Fixes
- [ ] Bug fix 1
- [ ] Bug fix 2

## Breaking Changes
- [ ] Cambio de API 1 (describe cómo migrar)

## Technical
- CONFIG_VERSION: 2 → 3
- Memory usage: XX% → YY%
- Compatible: ESP32 only
```

---

## MEJORES PRÁCTICAS

### Code Organization Rules

1. **Non-blocking siempre**
   - ✅ `if (millis() - lastTime < INTERVAL) return;`
   - ❌ `delay(100);`

2. **No accents en strings**
   - ✅ `"Valvula"` (ASCII only)
   - ❌ `"Válvula"` (UTF-8 causes display corruption)

3. **Validar antes de guardar**
   - ✅ `if (validateConfig(cfg)) saveConfig();`
   - ❌ Guardar sin validar

4. **Versioning para cambios de struct**
   - ✅ Incrementar CONFIG_VERSION
   - ❌ Cambiar Config struct sin incrementar

5. **Comentarios en español pero variables en inglés**
   - ✅ `// Chequear si tanque está bajo` + `if (tankLevelLow)`
   - ❌ `if (tanque_bajo)` (mezclar idiomas)

6. **Magic numbers → Constantes**
   - ✅ `const unsigned long INTERVAL = 100;`
   - ❌ `if (millis() - last < 100)`

7. **Nombres descriptivos**
   - ✅ `valveImplemented`, `tankLevelLow`
   - ❌ `flag1`, `val2`

---

### Debugging Tips

1. **Serial Output para timing issues**
```cpp
Serial.print("lastCheck=");
Serial.print(millis() - sysState.lastSensorCheck);
Serial.println("ms");
```

2. **Stack overflow detection**
```cpp
// En setup()
Serial.print("Free RAM: ");
Serial.println(freeRam());  // Útil función

int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ?
          (int) &__heap_start : (int) __brkval);
}
```

3. **EEPROM Dump (Arduino Nano)**
```cpp
void dumpEEPROM() {
  for (int i = 0; i < 64; i++) {
    Serial.print(EEPROM.read(i), HEX);
    Serial.print(" ");
    if ((i+1) % 16 == 0) Serial.println();
  }
}
```

---

### Performance Optimization

| Optimización | Ganancia | Dificultad |
|---|---|---|
| Usar PROGMEM para strings | ~100 bytes | 🟢 Baja |
| Refactorizar menús (HECHO) | ~260 líneas | 🟡 Media |
| Reducir buffer alarmas | ~50 bytes | 🟢 Baja |
| Inline funciones pequeñas | ~30 bytes | 🟠 Alta |
| Comprimir display buffer | ~1 KB | 🔴 Muy Alta |

---

### Testing Checklist

- [ ] Compilación exitosa (ESP32)
- [ ] Serial output clean (sin caracteres raros)
- [ ] Persistencia funciona (test manual)
- [ ] Sensor validation funciona
- [ ] Display muestra correctamente
- [ ] Menús navegables sin saltos
- [ ] Encoder responde sin lag
- [ ] Interlocks de seguridad funcionan

---

**Última actualización:** 2024-11-13
**Versión:** 3.1
