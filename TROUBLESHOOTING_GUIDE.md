# TROUBLESHOOTING GUIDE - Pressure Controller Safety System v3.1

Diagnóstico y solución de problemas comunes.

---

## 🔴 PROBLEMAS CRÍTICOS

### 1. "Bomba no enciende" (aunque setpoint no alcanzado)

**Síntomas:**
- Sensor OK, setpoint bajo presión actual
- Pero bomba siempre OFF
- Serial muestra "SENSOR BLOQUEADO" o "VALVULA ERROR"

**Diagnóstico:**

Abrir Serial Monitor (115200 baud) y buscar:

| Mensaje | Significado | Acción |
|---------|-----------|--------|
| `V:ERR` / `VALVULA ERROR` | D7 desconectado | Verificar conexión D7 |
| `T:BAJO` / `TANQUE BAJO` | D6 HIGH | Revisar sensor tanque |
| `SENSOR ERROR` | ADC inválido | Recalibrar sensor |

**Solución:**
```cpp
// En Serial Monitor, ver:
║ Valvula:     NO IMPLEMENTADA     ← Si no hay válvula, debe ser NO
║ Tank Sensor: NO IMPLEMENTADO     ← Si no hay sensor, debe ser NO

// Si muestra IMPLEMENTADA pero no está conectada:
// 1. Ir a Menú 5 → Cambiar a NO
// 2. Guardar
// 3. Bomba debería encender ahora
```

---

### 2. "Bomba enciende pero sensor fluctúa"

**Síntomas:**
- Bomba alterna ON/OFF sin razón
- Serial muestra `ERRATICO ⚠` periódicamente
- Presión no sube steadily

**Diagnóstico:**

**Causa probable:** Ruido eléctrico en línea ADC

**Verificación:**
```
1. Abrir Serial Monitor
2. Cada 5 segundos ves:
   ║ ADC: 1023  ║ Presión: 50 psi
   ║ ADC: 1024  ║ Presión: 50 psi
   ║ ADC: 512   ║ Presión: 25 psi ← SALTO ERRATICO
   ║ ADC: 1020  ║ Presión: 50 psi

3. Si ve saltos >1024 ADC: RUIDO en línea
```

**Soluciones (en orden):**
1. **Cortocircuitar capacitor de desacoplamiento** (100nF cercano a sensor)
2. **Usar cable blindado** para señal ADC
3. **Separar cables de potencia** de cables de señal
4. **Aumentar separación física** del motor/bomba

**Código workaround temporal:**
```cpp
// En checkSensor(), línea 321
#define ADC_MAX_CHANGE 512  // Aumentar de 256 (AVR) o 1024 (ESP32)
// Esto requiere MÁS muestras erráticas para confirmar error
```

---

### 3. "Configuración se pierde después de apagar"

**Síntomas:**
- Cambio Menu 5 (Válvula) SI → NO
- Guardo (Serial muestra "guardado")
- APAGO/ENCIENDO
- ¡Vuelve a aparecer SI!

**Diagnóstico:**

En Serial Monitor al arrancar:
```
✓ Configuracion cargada desde Preferences (v2)  ← OK, está cargando
║ Valvula:     SI ← pero no debería ser SI!
```

**Problema probable:** Configuración guardada pero NO persistió

**Soluciones:**

**A. ESP32 - Preferences issue:**
```cpp
// En saveConfig(), línea 1887:
preferences.begin("pressure", false);  // false = Read+Write
preferences.putUChar("version", CONFIG_VERSION);
preferences.putInt("pressMin", config.pressMin);
// ... más puts ...
preferences.end();  // ← ¡CRÍTICO! Sin esto no guarda
```

**B. Arduino AVR - EEPROM issue:**
```cpp
// En saveConfig(), línea 1903:
config.version = CONFIG_VERSION;
config.checksum = calculateChecksum();
EEPROM.put(0, config);  // ← EEPROM.commit() NO existe en AVR
```

**Test de persistencia:**
```bash
1. Serial Monitor > 115200
2. Cambiar Menu 5: SI → NO
3. Ver: "Configuracion guardada en Preferences (v2)"
4. DESENCHUFAR completamente 5 segundos
5. ENCHUFAR
6. Ver: "Configuracion cargada desde Preferences (v2)"
7. ¿Dice Valve=NO? ✅ OK
   ¿Dice Valve=SI? ❌ No persistió
```

**Si sigue fallando:**
```cpp
// Opción nuclear: Factory reset
// En setup(), antes de loadConfig():

// Descomenta temporalmente:
// clearStoredConfig();  // Borra todo y reinicia defaults
```

---

### 4. "Display no muestra nada" o "Caracteres raros"

**Síntomas:**
- Pantalla en blanco, negra o con caracteres aleatorios
- Serial Monitor funciona bien
- No hay inicialización OLED

**Diagnóstico:**

**Verificar conexión I2C:**
```
D20 (SDA) → Display SDA
D21 (SCL) → Display SCL
GND       → Display GND
3.3V      → Display VCC
```

**Test de comunicación I2C (Arduino Nano):**
```cpp
// Agregar al setup() temporalmente:
Wire.begin();
byte error, address;
Serial.println("I2C Scanner:");
for(address = 1; address < 127; address++ ) {
  Wire.beginTransmission(address);
  error = Wire.endTransmission();
  if (error == 0) {
    Serial.print("Device at 0x");
    Serial.println(address, HEX);
  }
}

// Debe mostrar: Device at 0x3C (si display conectado)
```

**Soluciones:**
1. Verificar VCC display = 3.3V (NO 5V!)
2. Probar con pull-up internos de Arduino
3. Si aún no funciona: reemplazar cable I2C
4. Probar con sketchwch simple de Adafruit

---

## 🟡 PROBLEMAS MODERADOS

### 5. "Encoder rota pero menú no responde"

**Síntomas:**
- Giro encoder pero menuSelection no cambia
- O salta de 0 a 6 sin valores intermedios

**Diagnóstico:**

**Verificar conexión físicamente:**
```
D2 (ENCODER_CLK) → Encoder CLK
D3 (ENCODER_DT)  → Encoder DT
D4 (ENCODER_SW)  → Encoder SW
GND              → Encoder GND
```

**Test en Serial:**
```cpp
// Agregar a menuListMode():
Serial.print("encoderPos=");
Serial.print(encoderPos);
Serial.print(" menuSelection=");
Serial.println(menuSelection);

// Girar encoder y ver si encoderPos cambia
```

**Problema probable:** Rebotes en CLK/DT

**Solución:** Aumentar ENCODER_DEBOUNCE_TIME
```cpp
const unsigned long ENCODER_DEBOUNCE_TIME = 10;  // era 5
```

---

### 6. "Menú 1 (Rango) se congela"

**Síntomas:**
- Entro a Menu 1
- Cambio pressMin
- Pero no puedo salir o avanzar

**Diagnóstico:**

Menu 1 es especial (2 parámetros). En handleMenuNavigation():
```cpp
if (currentMenu == 1) {
  if (editingParam == 1) {
    editingParam = 2;  // Avanzar a Max
    encoderPos = 0;
  } else if (editingParam == 2) {
    // Guardar
    editingParam = 0;
  }
}
```

**Problema probable:** Validación rechazando configuración

**Verificar Serial:**
```
Si ves: "❌ ERROR: Min debe ser < Max - 10"
Significa: pressMin >= (pressMax - 10)

Solución: Hacer pressMax más alto o pressMin más bajo
```

---

### 7. "Serial output muestra caracteres raros"

**Síntomas:**
```
╔════════════════════════════════════════╗
║ ▓▓▓▓▓▓ ERRATICO ⚠ ░░░░░░░░░░░░░░░░░║  ← Caracteres raros
```

**Causa:** Encoding issue en Serial

**Solución:**
```cpp
// Verificar en Serial Monitor:
// Baud Rate DEBE ser: 115200
// Character set: UTF-8 o Default

// En Arduino IDE:
// Tools > Serial Monitor > 115200 baud
```

**Workaround temporal:** Desabilitar caracteres especiales
```cpp
// En displaySensorError(), comentar líneas con emojis:
// Serial.println("✗ DESCONECTADO");  // ← Causa problemas en AVR
Serial.println("DESCONECTADO");
```

---

## 🟢 PROBLEMAS MENORES

### 8. "Bomba enciende/apaga constantemente (ON/OFF/ON/OFF)"

**Síntomas:**
- Presión oscila alrededor de setpoint
- Bomba no llega a presión estable

**Diagnóstico:**

Probably **deadband muy pequeño**. Si deadband=1 psi:
```
Setpoint = 50 psi
deadband = 1 psi
Range: 49-51 psi

Bomba ON a 49 psi
Bomba OFF a 51 psi
← Oscilación constante
```

**Solución:** Aumentar deadband
```
Ir a Menú 3 (Banda Muerta)
Cambiar de 1 → 5 psi mínimo
```

---

### 9. "Tank low alarm suena pero tanque no está bajo"

**Síntomas:**
- OLED muestra "T:BAJO"
- Pero tanque está completamente lleno
- Serial muestra "D6 HIGH"

**Diagnóstico:**

D6 invertida (lógica opuesta) o falla de sensor

**Verificar:**
1. ¿D6 está conectada correctamente?
2. ¿Sensor activo HIGH o LOW?

**Solución:**

Si sensor está **invertido lógicamente**:
```cpp
// En checkTankLevel(), línea 556:
bool reading = digitalRead(TANK_LEVEL_PIN);

// Si inversión: cambiar a:
bool reading = !digitalRead(TANK_LEVEL_PIN);  // ← Invertir
```

---

### 10. "Luz LED no parpadea en alarma"

**Síntomas:**
- LED en pin D12 siempre OFF
- O siempre ON, no parpadea

**Diagnóstico:**

LED parpadea cuando `errorRecoveryCount > 0` o `pumpBlocked`:

```cpp
// En operationMode(), búscar:
if (pumpBlocked || errorRecoveryCount > 0) {
  // LED parpadea
  if ((millis() / ALARM_BLINK_INTERVAL) % 2) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}
```

**Verificar:**
1. ¿LED conectado a D12?
2. ¿GND conectado?
3. ¿LED tiene resistencia limitadora (220Ω)?

---

## 📊 TABLA RÁPIDA DE DIAGNÓSTICO

| Síntoma | Probable Causa | Verificar |
|---------|---|---|
| No enciende | pumpBlocked activo | Serial: "BLOQUEADO" |
| Fluctúa presión | Ruido ADC | Serial: "ERRATICO" |
| Config no persiste | EEPROM corrupta | Serial: "guardada"? |
| Display muestra raros | Encoding I2C | Baud 115200? |
| Menú no responde | Encoder roto | encoderPos cambia? |
| Oscilación ON/OFF | deadband pequeño | Menú 3: aumentar |
| Falsa alarma tanque | D6 invertida | Invertir lógica |
| Sem LED | LED desconectado | Revisar D12 |

---

## 🔧 PROCEDIMIENTO DIAGNOSTIC COMPLETO

Si nada funciona, ejecutar este test:

```
PASO 1: Compilación
└─ ✓ Compila sin errores? → PASO 2
└─ ✗ Error de compilación → Revisar mensajes

PASO 2: Serial Output
└─ ✓ Serial Monitor 115200 muestra mensajes limpios? → PASO 3
└─ ✗ Caracteres raros → Cambiar encoding

PASO 3: Display
└─ ✓ OLED muestra "CONTROLADOR DE PRESION"? → PASO 4
└─ ✗ Pantalla en blanco → Revisar I2C (scan address 0x3C)

PASO 4: Sensores
└─ ✓ Serial muestra "Config: Min=-20 Max=200..." → PASO 5
└─ ✗ Defaults aparecen → EEPROM corrupta (factory reset)

PASO 5: Entrada
└─ ✓ Girar encoder cambia menú? → PASO 6
└─ ✗ No responde → Revisar D2/D3 o debounce

PASO 6: Persistencia
└─ ✓ Apagar/encender: config persiste? → ¡OK!
└─ ✗ Se resetea → Issue Preferences/EEPROM

PASO 7: Seguridad
└─ ✓ Abrir válvula (D7 LOW) → bomba bloquea? → ¡ÉXITO!
└─ ✗ No bloquea → Código de seguridad roto
```

---

## 📞 CONTACTO Y REPORTES

**Para reportar bugs:**
1. Ejecutar test diagnóstico completo
2. Capturar Serial Monitor output
3. Describir el problema exactamente
4. Incluir temperatura ambiente si es relevante
5. Presionar durante cuánto tiempo ocurre

**GitHub Issues:** https://github.com/AutomatizacionGroup/PressureController_SafetySystem/issues

---

**Última actualización:** 2024-11-13
**Versión:** 3.1
