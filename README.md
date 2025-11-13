# Controlador de Presión con Sistema de Seguridad v3.0

## 📋 Descripción
Sistema avanzado de control de presión con detección de fallas del sensor, protección de bomba y alarmas visuales. Compatible con Arduino AVR y ESP32 Nano.

## ✨ Nuevas Características v3.0
- **Detección de fallas del sensor** (desconexión, cortocircuito, señal fuera de rango)
- **Bloqueo automático de bomba** en caso de anomalías
- **Alarmas visuales específicas** para cada tipo de falla
- **Sistema de recuperación inteligente** con histéresis
- **Compatibilidad dual** ESP32/Arduino AVR

## 🔧 Hardware Requerido

### Componentes Principales
- **Arduino Nano** o **ESP32 Nano**
- **Display OLED** 128x64 I2C (SSD1306)
- **Encoder Rotativo** KY-040 o similar
- **Sensor de Presión** 0-150 PSI (0.5-4.5V salida)
- **Relé o MOSFET** para control de bomba
- **LED indicador**

## 📍 Conexiones

```
Display OLED (I2C):
  VCC → 3.3V (¡IMPORTANTE!)
  GND → GND
  SCL → A5 (Arduino) / GPIO22 (ESP32)
  SDA → A4 (Arduino) / GPIO21 (ESP32)

Encoder Rotativo:
  CLK → D2 (interrupción)
  DT  → D3
  SW  → D4
  VCC → 5V/3.3V
  GND → GND

Entradas/Salidas:
  Sensor Presión → A0
  Bomba (Relé)   → D10
  LED Indicador  → D12
```

## 🚨 Sistema de Seguridad

### Estados del Sensor
El sistema detecta automáticamente:

| Estado | Voltaje | Condición | Acción |
|--------|---------|-----------|--------|
| **DESCONECTADO** | < 0.3V | Cable roto | Bomba bloqueada |
| **SEÑAL BAJA** | 0.3-0.5V | Fuera de rango | Advertencia |
| **NORMAL** | 0.5-4.5V | Operación OK | Control normal |
| **SEÑAL ALTA** | 4.5-4.8V | Fuera de rango | Advertencia |
| **CORTOCIRCUITO** | > 4.8V | Falla crítica | Bomba bloqueada |

### Comportamiento de Protección
- **Bloqueo inmediato** de bomba en fallas críticas
- **LED parpadeante** durante alarmas
- **Mensajes específicos** para cada tipo de falla
- **Recuperación segura** después de 10 ciclos buenos

## 🎮 Uso del Sistema

### Navegación Básica
- **Presión larga** (1s): Entrar/salir menú
- **Girar encoder**: Navegar/ajustar valores
- **Presión corta**: Seleccionar/confirmar

### Menús de Configuración
1. **Rango de entrada**: Calibración del sensor
2. **Punto de ajuste**: Presión objetivo
3. **Banda muerta**: Histéresis del control
4. **Tiempo mínimo ON**: Protección anti-ciclos

## ⚙️ Parámetros del Sistema

### Configuración Sensor (0-150 PSI, 0.5-4.5V)
```
Presión Mínima: 0 PSI   → 0.5V → ADC: 102 (AVR) / 410 (ESP32)
Presión Máxima: 150 PSI → 4.5V → ADC: 921 (AVR) / 3686 (ESP32)
```

### Valores Recomendados
- **Setpoint**: 40-60 PSI (doméstico)
- **Deadband**: 10-15 PSI
- **Tiempo mínimo**: 3-5 segundos

## 📊 Pantallas de Alarma

### Sensor Desconectado
```
┌────────────────────┐
│███ !!! ALARMA !!! ███│
│                      │
│      SENSOR          │
│   DESCONECTADO       │
│                      │
│  REVISAR CABLES      │
└────────────────────┘
```

### Señal Fuera de Rango
```
┌────────────────────┐
│███ !!! ALARMA !!! ███│
│                      │
│    SEÑAL BAJA        │
│      < 0.5V          │
│                      │
│  VERIFICAR SENSOR    │
└────────────────────┘
```

## 🔄 Lógica de Control

### Condiciones Normales
```
ENCENDER BOMBA:
  SI presión <= (SP - DB/2)

APAGAR BOMBA:
  SI presión >= (SP + DB/2) 
  Y tiempo_on >= T_mínimo
```

### Con Sistema de Seguridad
```
BLOQUEAR BOMBA:
  SI sensor_status != NORMAL
  
PERMITIR OPERACIÓN:
  SI sensor_status == NORMAL
  Y error_count == 0
```

## 📈 Mejoras Futuras Planificadas
- [ ] Registro de eventos de falla
- [ ] Comunicación WiFi/Bluetooth
- [ ] Múltiples sensores redundantes
- [ ] Auto-calibración del sensor
- [ ] Notificaciones remotas de alarmas

## 🐛 Resolución de Problemas

### Alarma constante "SENSOR DESCONECTADO"
1. Verificar cables del sensor
2. Medir voltaje en pin A0 (debe ser > 0.3V)
3. Revisar alimentación del sensor

### Bomba no enciende con presión baja
1. Verificar estado del sensor en pantalla
2. Revisar si hay alarma activa
3. Esperar recuperación del sistema (10 ciclos)

### Encoder salta de 2 en 2
- Problema resuelto en v3.0 con filtro de contador

## 📚 Bibliotecas Requeridas
```cpp
#include <Wire.h>              // I2C
#include <Adafruit_GFX.h>      // Gráficos
#include <Adafruit_SSD1306.h>  // Display OLED
#include <EEPROM.h>            // Almacenamiento (AVR)
#include <Preferences.h>       // Almacenamiento (ESP32)
```

## 🔐 Seguridad Industrial
- ⚠️ Sistema diseñado para aplicaciones no críticas
- ⚠️ Incluir válvula de alivio mecánica
- ⚠️ Verificar sensor regularmente
- ⚠️ Mantener respaldo manual de control

## 📝 Versiones
- **v1.0**: Sistema básico
- **v2.0**: Menús mejorados, interfaz gráfica
- **v3.0**: Sistema de seguridad, detección de fallas

## 👥 Autor
Actualizado: Diciembre 2024
Versión: 3.0 - Safety System

---
*Sistema de control con protección avanzada para aplicaciones industriales y domésticas.*