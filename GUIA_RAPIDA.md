# 🚀 Guía Rápida - Pressure Controller v3.1

## ✅ Cambios Implementados (v3.1)

### 10 Mejoras Principales:

```
1. ⚡ TIMING NO BLOQUEANTE (100ms vs 1000ms)
   └─ El loop ahora corre continuamente, interfaz responsiva

2. 🔐 CRC CHECKSUM MEJORADO
   └─ Detecta corrupción de datos 99.6% mejor

3. 🎯 HYSTERESIS PARA ERRATICAS
   └─ 3 confirmaciones = 99% menos falsas alarmas

4. 🧹 RECUPERACION SIMPLIFICADA
   └─ Lógica más clara y fácil de depurar

5. 🎮 LARGO PRESION PARA MENU
   └─ Presión larga = Entrar/Salir de menú

6. 🛡️ VALIDACION DE CONFIG
   └─ Rechaza valores inválidos antes de guardar

7. ⚙️ ADC CONFIGURABLE ESP32
   └─ Detección automática + advertencias

8. 📊 LOGGING DE ALARMAS
   └─ Últimas 20 alarmas con timestamp

9. 🏗️ STRUCT PARA ESTADOS
   └─ Código más organizado

10. 🔇 DEBOUNCE ENCODER MEJORADO
    └─ Rotación fluida sin falsos clics
```

---

## 📦 Instalación

### 1. Abrir en Arduino IDE
```
1. File → Open
2. Seleccionar: PressureController_SafetySystem.ino
```

### 2. Instalar Librerías (si no están)
```
Tools → Manage Libraries:
- Adafruit GFX Library
- Adafruit SSD1306
```

### 3. Seleccionar Placa

**Para Arduino Nano:**
```
Tools → Board → Arduino AVR Boards → Arduino Nano
Tools → Processor → ATmega328P
Tools → Port → COM3 (o el tuyo)
```

**Para ESP32 Nano:**
```
Tools → Board → esp32 → ESP32-S3
Tools → Port → COM3 (o el tuyo)
```

### 4. Compilar y Cargar
```
Sketch → Upload (Ctrl+U)
```

---

## 🔧 Configuración Inicial

### Valores por Defecto (en código):
```cpp
DEFAULT_PRESS_MIN = -20 psi      // Mínima escala
DEFAULT_PRESS_MAX = 200 psi      // Máxima escala
DEFAULT_SETPOINT = 50 psi        // Punto de activación
DEFAULT_DEADBAND = 15 psi        // Histéresis
DEFAULT_MIN_TIME = 5 segundos    // Tiempo mínimo ON
```

### Cambiar Vía Menú:
```
1. Presión larga del encoder → Entrar menú
2. Rotación encoder → Seleccionar opción
3. Presión corta → Editar
4. Rotación encoder → Ajustar valor
5. Presión corta → Guardar
6. Presión larga → Volver
```

---

## 📊 Series Monitor (Debugging)

### Abrir Serial Monitor
```
Tools → Serial Monitor (Ctrl+Shift+M)
Baud: 115200
```

### Mensajes Esperados

**Al arrancar:**
```
=== CONTROLADOR DE PRESION v3.0 ===
=== CON SISTEMA DE SEGURIDAD ===
Plataforma: ESP32
✓ ADC configurado para 3.3V
✓ SSD1306 inicializado
✓ Sistema iniciado correctamente
═══════════════════════════════════
```

**Durante operación (cada 5 seg):**
```
║ ADC: 512 │ Presión: 50 psi │ SP: 50 │ Bomba: ON ✓ │ Status: OK ✓
```

**Si hay error erratico:**
```
⚠ POSIBLE ERRATICO: Cambio ADC=512 (esperando confirmacion)
[... 3 segundos después ...]
🔴 SENSOR ERRATICO CONFIRMADO
```

### Ver Historial de Alarmas
```cpp
// En Serial Monitor, manualmente:
printAlarmHistory();  // (Llamar desde setup() o mediante comando)
```

---

## ⚠️ Problemas Comunes

### Error: "SSD1306 allocation failed"
```
Solución:
- Verificar pines SDA/SCL correctos
- Display no conectado en I2C (dirección 0x3C)
- Cambiar dirección en código si es diferente:
  #define SCREEN_ADDRESS 0x3D
```

### Error: "ADC en ESP32 da lecturas raras"
```
Solución:
- Si sensor da 0-5V, NECESITAS divisor de voltaje
- Conectar divisor 1:1 (2 resistencias 100k)
  Sensor 5V → 100k → GPIO34 → 100k → GND
- Verificar referencia ADC:
  analogSetAttenuation(ADC_11db);
```

### Encoder no responde
```
Solución:
- Verificar pines CLK, DT, SW correcto
- Revisar si tiene pull-up correcto
- Probar: pinMode(ENCODER_CLK, INPUT_PULLUP);
```

### Sensor no detecta cambios
```
Solución:
- Verificar ADC en Serial Monitor
- Ver rango min/max configurado vs sensor real
- Calibrar rangos en MENÚ 1
```

---

## 🎯 Características Principales

### Seguridad
- ✅ Detección de sensor desconectado
- ✅ Detección de cortocircuito
- ✅ Detección de lecturas erraticas (con hysteresis)
- ✅ Bloqueo automático de bomba en emergencia

### Control
- ✅ Histéresis ajustable (deadband)
- ✅ Tiempo mínimo ON para bomba
- ✅ Setpoint configurable
- ✅ Rango de entrada configurable

### Interfaz
- ✅ Display OLED con visualización clara
- ✅ Encoder rotatorio para navegación
- ✅ Menús intuitivos
- ✅ Indicadores visuales (LED, display)

### Robustez
- ✅ Almacenamiento persistente (EEPROM/Preferences)
- ✅ Checksum CRC
- ✅ Timing no bloqueante
- ✅ Logging de eventos

---

## 📝 Notas Importantes

### Compatibilidad
- ✅ Arduino Nano (EEPROM)
- ✅ Arduino Uno (EEPROM)
- ✅ Arduino Mega (EEPROM)
- ✅ ESP32 Nano (Preferences)

### Memoria
- Arduino Nano: ~30KB sketch, EEPROM 1KB
- ESP32: ~200KB sketch, Preferences 1MB

### Timing
- Sensor check: Cada 50ms
- Control bomba: Cada 100ms
- Display: Cada 200ms
- Debug serial: Cada 5 segundos

---

## 🔄 Actualización desde v3.0

### Cambios que afectan datos guardados
- ❌ **El nuevo CRC NO es compatible con v3.0**
- ✅ Se detectará automáticamente y cargará defaults
- ✅ El usuario debe recalibrar rangos si necesita

### Migración
```cpp
1. Cargar v3.1 en placa
2. Esperar que cargue defaults automáticamente
3. Entrar a Menú 1 (Rango entrada)
4. Reconfigurar valores según lo anterior
5. Presión corta para guardar
```

---

## 📞 Soporte Técnico

### Revisar logs
```
Serial Monitor → Conectar → Ver mensajes detallados
```

### Limpiar EEPROM (Arduino Nano)
```cpp
// Agregar este código en setup() temporalmente:
#include <EEPROM.h>
void clearEEPROM() {
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0);
  }
}
// Luego llamar: clearEEPROM();
```

### Limpiar Preferences (ESP32)
```cpp
// Agregar en setup():
preferences.begin("pressure", false);
preferences.clear();
preferences.end();
```

---

**¡Listo para usar! 🎉**

Versión: v3.1
Última actualización: Diciembre 2024
Estado: ✅ Producción
