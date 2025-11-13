# 🔍 PROCEDIMIENTO DE VALIDACIÓN: PERSISTENCIA DE MEMORIA

## Objetivo
Verificar que **valveImplemented** y **tankSensorImplemented** persisten en memoria después de apagar/encender el dispositivo.

---

## PASO 1: Cargar y Compilar

```bash
# Navegar al directorio
cd "c:\Users\amont\Documents\Arduino\PressureController_SafetySystem"

# Compilar
arduino-cli compile --fqbn esp32:esp32:esp32s3 PressureController_SafetySystem.ino
```

## PASO 2: Subir al Dispositivo

```bash
# Subir a ESP32 Nano
arduino-cli upload -p COM13 --fqbn esp32:esp32:esp32s3

# Para Arduino AVR (si usas Nano)
arduino-cli upload -p COM13 --fqbn arduino:avr:nano
```

## PASO 3: Abrir Monitor Serial

```bash
# Monitorear salida a 115200 baud
arduino-cli monitor -p COM13 --config baudrate=115200
```

---

## VERIFICACIÓN: PRIMERA EJECUCIÓN

**Deberías ver en el serial:**

```
=== CONTROLADOR DE PRESION v3.0 ===
=== CON SISTEMA DE SEGURIDAD ===
Plataforma: ESP32
✓ ADC configurado para 3.3V
...
Primera ejecucion, usando valores por defecto
Config: Min=-20 Max=200 SP=50 DB=10 MinTime=5s | Valve=SI | Tank=SI
```

**Nota:** Dice "Primera ejecucion" porque es primera vez (memoria vacía).

---

## PASO 4: CAMBIAR CONFIGURACIÓN

1. **Presionar HOLD (botón encoder)** → Entra a menú
2. **Girar encoder** → Seleccionar "Valvula" (Menu 5)
3. **Presionar corto** → Entra a edición
4. **Girar encoder** → Cambiar SI → NO
5. **Presionar corto** → Guardar
6. **Presionar HOLD** → Volver a operación

**En el serial verás:**
```
✓ Configuracion valvula guardada correctamente
✓ Valvula de entrada DESHABILITADA
✓ Configuracion guardada en Preferences (v2)
```

---

## PASO 5: APAGAR Y ENCENDER (CRÍTICA)

1. **DESENCHUFAR el dispositivo completamente** (esperar 5 segundos)
2. **VOLVER A ENCHUFAR**
3. **Abrir monitor serial nuevamente**

**Deberías ver en el serial:**

```
=== CONTROLADOR DE PRESION v3.0 ===
=== CON SISTEMA DE SEGURIDAD ===
Plataforma: ESP32
...
✓ Configuracion cargada desde Preferences (v2)
Config: Min=-20 Max=200 SP=50 DB=10 MinTime=5s | Valve=NO | Tank=SI
        ↑ ESTO ES CRÍTICO: Valve=NO (NO Valve=SI como antes)
```

**Si ves:**
- ✅ `✓ Configuracion cargada desde Preferences (v2)` → **PERSISTENCIA FUNCIONA**
- ✅ `Valve=NO` en la configuración → **VALORES PERSISTEN CORRECTAMENTE**

---

## VERIFICACIÓN EN PANTALLA

Después de encender, en la pantalla OLED deberías ver:

- **Si Valve=NO:** El ícono de válvula mostrará `-` (no implementada)
- **Si Tank=SI:** El ícono de tanque mostrará estado normal

Girar encoder para verificar que el menú refleja la configuración guardada.

---

## TEST ADICIONAL: TANK SENSOR

Repetir PASOS 4-5 pero con Menu 6 (Sensor Tanque):

1. Cambiar Tank: SI → NO
2. Guardar (verás "Configuracion guardada en Preferences")
3. Apagar/encender
4. Verificar que Tank=NO persiste

---

## TROUBLESHOOTING

### Problema: "Primera ejecucion" cada vez que enciendo

**Causa posible:**
- Memoria Preferences no se está guardando correctamente
- Dispositivo no cierra la sesión de Preferences correctamente

**Solución:**
```cpp
// Verificar en el código:
// Línea 1897: preferences.end();  ← Debe estar aquí

// Si falta, agregar.
```

### Problema: "Checksum invalido" (Arduino AVR)

**Causa posible:**
- EEPROM corrompida o vacía

**Solución:**
1. Presionar en menú → SALIR → HOLD (varias veces seguidas)
2. Debería limpiar EEPROM e inicializar
3. O cargar el código `clearStoredConfig()` manualmente

---

## MÉTRICAS DE ÉXITO

| Métrica | Esperado | Señal |
|---------|----------|--------|
| **Serial al arrancar** | "Configuracion cargada desde Preferences (v2)" | ✅ ÉXITO |
| **Valve después de cambiar** | Dice Valve=NO después de apagar | ✅ ÉXITO |
| **Tank después de cambiar** | Dice Tank=SI después de apagar | ✅ ÉXITO |
| **Pantalla OLED** | Iconos reflejan configuración | ✅ ÉXITO |

---

## DATOS GUARDADOS EN MEMORIA

### ESP32 (Preferences):
```
Clave              Tipo    Valor por defecto
saved              bool    false (se setea a true al guardar)
version            uchar   2
pressMin           int     -20
pressMax           int     200
setpoint           int     50
deadband           int     10
minOnTime          int     5
valveImplemented   bool    true
tankSensorImplemented bool  true
```

**Tamaño total:** ~20 bytes en NVS (flash)

### Arduino AVR (EEPROM):
```
Offset   Campo                    Tipo    Bytes
0        version                  byte    1
1        pressMin                 int     2
3        pressMax                 int     2
5        setpoint                 int     2
7        deadband                 int     2
9        minOnTime                int     2
11       valveImplemented         bool    1
12       tankSensorImplemented    bool    1
13       checksum                 byte    1
```

**Total:** 14 bytes en EEPROM (1% de 1KB)

---

## CONCLUSIÓN

Si sigues estos pasos y ves `Valve=NO` después de apagar/encender, **LA PERSISTENCIA FUNCIONA PERFECTAMENTE**.

Si hay problemas, verifica:
1. Serial muestra "Configuracion cargada" (no "Primera ejecucion")
2. Valores después de "Config:" coinciden con lo que cambiaste
3. Memory persists después de múltiples ciclos apagar/encender
