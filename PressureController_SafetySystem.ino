//Pressure controller with Safety System v3.1
//Compatible with Arduino AVR (Nano, Uno, Mega) and ESP32 (Nano ESP32)
//Enhanced with sensor fault detection, hysteresis filtering, and improved safety
//Modified: December 2024
//
// CAMBIOS IMPLEMENTADOS v3.1:
// ✓ Timing no bloqueante (reemplazo delay de 1s)
// ✓ Sistema CRC mejorado para checksum
// ✓ Filtro erratico con hysteresis (3 confirmaciones)
// ✓ Recuperacion simplificada y mas clara
// ✓ Largo presion para entrar/salir de menu
// ✓ Validacion de configuracion antes de guardar
// ✓ Verificacion ADC para ESP32
// ✓ Logging de alarmas (ultimas 20)
// ✓ Debounce mejorado del encoder
// ✓ Struct para estados del sistema
// ✓ Serial con emojis y mejor formato
//
// REQUERIMIENTOS HARDWARE:
// - Arduino Nano / Uno / Mega con EEPROM
// - ESP32 Nano con Preferences
// - Display OLED SSD1306 (128x64, direccion 0x3C)
// - Sensor de presion 0-150 PSI (salida 0.5-4.5V)
// - Encoder rotatorio 3-pin
// - Bomba/valvula PWM
//
// NOTA ESP32: Si el sensor da 0-5V, usar divisor de voltaje 1:1 en ANALOG_PIN

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Deteccion de plataforma y configuracion de almacenamiento
#ifdef ESP32
  #include <Preferences.h>
  Preferences preferences;
#else
  #include <EEPROM.h>
#endif

// Definiciones de pines - Arduino Nano ESP32 / AVR
// Los pines son compatibles entre ambas plataformas (D2, D3, D4, etc.)
#define ENCODER_CLK 2    // D2 - Pin CLK del encoder rotatorio
#define ENCODER_DT  3    // D3 - Pin DT del encoder rotatorio  
#define ENCODER_SW  4    // D4 - Pin SW (boton) del encoder rotatorio
#define PUMP_PIN    10   // D10 - Control de bomba/valvula
#define LED_PIN     12   // D12 - LED indicador
#define INPUT_PIN   7    // D7 - Valvula de entrada (LOW=abierta, HIGH=cerrada)
#define TANK_LEVEL_PIN 6 // D6 - Nivel bajo de tanque (HIGH=bajo nivel)
#define ANALOG_PIN  A0   // A0 - Sensor de presion (señal acondicionada PT-Sig)

// Resolucion ADC segun plataforma
#ifdef ESP32
  #define ANALOG_MAX  4095 // ESP32 Nano tiene ADC de 12 bits (0-4095)
#else
  #define ANALOG_MAX  1023 // Arduino AVR tiene ADC de 10 bits (0-1023)
#endif

// Display OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Variables del encoder mejoradas
volatile int encoderPos = 0;
int lastEncoderPos = 0;
int lastCLK = HIGH;
unsigned long lastButtonPress = 0;
const unsigned long longPressTime = 1000; // 1 segundo para presion larga
const unsigned long debounceDelay = 50;

// Filtro para encoder (debounce)
volatile unsigned long lastEncoderChange = 0;
const unsigned long ENCODER_DEBOUNCE_TIME = 5;  // 5ms para debounce

// Estados del sistema
enum SystemMode {
  MODE_OPERATION,
  MODE_MENU_LIST,
  MODE_MENU_CONFIG
};
SystemMode currentMode = MODE_OPERATION;

// Variables de configuracion (guardadas en memoria persistente)
#define CONFIG_VERSION 2  // Incrementar cuando cambia la estructura Config
struct Config {
  byte version;           // Version de estructura (para deteccion de cambios)
  int pressMin;           // Minimo del rango (Menu 1)
  int pressMax;           // Maximo del rango (Menu 1)
  int setpoint;           // Punto de ajuste (Menu 2)
  int deadband;           // Banda muerta (Menu 3)
  int minOnTime;          // Tiempo minimo ON (Menu 4)
  bool valveImplemented;  // ¿Valvula de entrada implementada? (Menu 5)
  bool tankSensorImplemented; // ¿Sensor de bajo nivel implementado? (Menu 6)
  byte checksum;          // Para validar datos
};
Config config;

// Valores por defecto
const int DEFAULT_PRESS_MIN = -20;
const int DEFAULT_PRESS_MAX = 200;
const int DEFAULT_SETPOINT = 50;
const int DEFAULT_DEADBAND = 15;
const int DEFAULT_MIN_TIME = 5;
const bool DEFAULT_VALVE_IMPLEMENTED = true;  // Asumir que la valvula si existe por defecto
const bool DEFAULT_TANK_SENSOR_IMPLEMENTED = true;  // Asumir que sensor de tanque existe

// Variables del menu
int currentMenu = 0;        // 0=Lista menus, 1-4=Menus individuales
int menuSelection = 0;      // Seleccion en lista de menus
bool editingValue = false;  // Si esta editando un valor
int editingParam = 0;       // Que parametro esta editando (0=ninguno, 1=min, 2=max)

// Variables operativas
int voltage = 0;
int press = 50;
int sp_value = 50;
int db_value = 10;
int volt_An = 0;
unsigned long T_syst;
unsigned long T_zero = 0;
float T_on = 0;
int T_set = 2;
int pump = 0;
bool time_1 = 0;

// Variables para valvula de entrada de agua
bool inletValveOpen = false;
bool lastInletValveState = false;
const unsigned long INLET_DEBOUNCE_TIME = 100;
unsigned long lastInletChange = 0;

// Variables para deteccion de falla de sensor D7 (valvula no conectada)
bool valveFaultDetected = false;
unsigned long valveHighStartTime = 0;
const unsigned long VALVE_FAULT_THRESHOLD = 10000;
bool hasValveStateChanged = false;

// Variables para detectar bajo nivel de tanque en D6
bool tankLevelLow = false;           // D6 HIGH = tanque bajo/vacio
bool lastTankLevelState = false;     // Estado anterior de D6
const unsigned long TANK_DEBOUNCE_TIME = 100;  // Debounce 100ms
unsigned long lastTankLevelChange = 0;  // Timestamp del ultimo cambio

// ========== SISTEMA DE SEGURIDAD ==========

// Estados del sensor
enum SensorStatus {
  SENSOR_OK,           // Operacion normal (0.5-4.5V)
  SENSOR_DISCONNECTED, // < 0.3V - Cable roto/desconectado
  SENSOR_LOW_SIGNAL,   // 0.3-0.5V - Fuera de rango bajo
  SENSOR_HIGH_SIGNAL,  // 4.5-4.8V - Fuera de rango alto  
  SENSOR_SHORT,        // > 4.8V - Cortocircuito
  SENSOR_ERRATIC       // Cambios bruscos imposibles
};

// Variables de seguridad
SensorStatus sensorStatus = SENSOR_OK;
SensorStatus lastSensorStatus = SENSOR_OK;
bool pumpBlocked = false;         // Bloqueo de bomba por seguridad
int errorRecoveryCount = 0;       // Contador para recuperacion
int lastValidReading = 0;         // Ultima lectura valida
unsigned long lastAlarmBlink = 0; // Para parpadeo de LED en alarma
bool alarmLedState = false;       // Estado del LED en alarma

// Variables para filtro temporal de erratico con hysteresis
unsigned long erraticDetectedTime = 0;
bool waitingErraticConfirmation = false;
int lastErraticReading = 0;
const unsigned long ERRATIC_CONFIRMATION_TIME = 3000; // 3 segundos
int erraticConfirmationCount = 0;                      // Contador para hysteresis
const int ERRATIC_CONFIRMATION_THRESHOLD = 3;         // Requiere 3 muestras erraticas

// Variables para sistema de recuperacion mejorado
int recoveryAttempts = 0;
const int MAX_RECOVERY_ATTEMPTS = 3;
unsigned long lastRecoveryAttempt = 0;
const unsigned long RECOVERY_WAIT_TIME = 30000; // 30 segundos

// Variable temporal para edicion en menu
Config config_temp;
bool usingTempConfig = false;

// ========== STRUCT PARA ESTADOS DEL SISTEMA ==========
struct SystemState {
  unsigned long lastOperationUpdate;    // Ultimo update de operationMode()
  unsigned long lastSerialDebug;        // Ultimo debug por serial
  unsigned long lastSensorCheck;        // Ultimo chequeo de sensor
  unsigned long lastDisplayUpdate;      // Ultimo update de display
};
SystemState sysState = {0, 0, 0, 0};

// Constantes de timing (ms)
const unsigned long OPERATION_UPDATE_INTERVAL = 100;   // 100ms para control rapido
const unsigned long SENSOR_CHECK_INTERVAL = 50;        // 50ms para sensor
const unsigned long SERIAL_DEBUG_INTERVAL = 5000;      // 5 segundos para debug
const unsigned long DISPLAY_UPDATE_INTERVAL = 200;     // 200ms para display

// ========== SISTEMA DE LOGGING DE ALARMAS ==========
#define MAX_ALARM_LOG 20  // Ultimas 20 alarmas

struct AlarmLog {
  unsigned long timestamp;
  SensorStatus status;
  int adcValue;
};

AlarmLog alarmHistory[MAX_ALARM_LOG];
int alarmHistoryIndex = 0;

// Constantes de limites ADC segun voltaje del sensor
// Sensor 0-150 PSI con salida 0.5-4.5V en sistema de 5V
#ifdef ESP32
  // ESP32: ADC 12-bit (0-4095) con 3.3V ref, pero sensor da 0-5V
  // Necesita divisor de voltaje o usar atenuacion
  #define ADC_DISCONNECTED  250   // < 0.3V
  #define ADC_LOW_WARNING   410   // 0.5V
  #define ADC_HIGH_WARNING  3686  // 4.5V  
  #define ADC_SHORT_CIRCUIT 3932  // > 4.8V
  #define ADC_MAX_CHANGE    1024  // Maximo cambio permitido entre lecturas
#else
  // Arduino AVR: ADC 10-bit (0-1023) con 5V ref
  #define ADC_DISCONNECTED  61    // < 0.3V
  #define ADC_LOW_WARNING   102   // 0.5V
  #define ADC_HIGH_WARNING  921   // 4.5V
  #define ADC_SHORT_CIRCUIT 983   // > 4.8V
  #define ADC_MAX_CHANGE    256   // Maximo cambio permitido entre lecturas
#endif

// Constantes de recuperacion
const int RECOVERY_CYCLES = 10;  // Ciclos buenos necesarios para recuperar
const unsigned long ALARM_BLINK_INTERVAL = 500; // ms para parpadeo

// ========== FUNCIONES DE SEGURIDAD ==========

void logAlarm(SensorStatus status, int adcValue) {
  // Registrar alarma en el historial
  alarmHistory[alarmHistoryIndex].timestamp = millis();
  alarmHistory[alarmHistoryIndex].status = status;
  alarmHistory[alarmHistoryIndex].adcValue = adcValue;
  alarmHistoryIndex = (alarmHistoryIndex + 1) % MAX_ALARM_LOG;
}

void printAlarmHistory() {
  // Mostrar historial de ultimas alarmas
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     HISTORIAL DE ULTIMAS ALARMAS      ║");
  Serial.println("╠════════════════════════════════════════╣");

  for (int i = 0; i < MAX_ALARM_LOG; i++) {
    int idx = (alarmHistoryIndex - i - 1 + MAX_ALARM_LOG) % MAX_ALARM_LOG;
    if (alarmHistory[idx].timestamp == 0) continue;

    Serial.print("║ T:");
    Serial.print(alarmHistory[idx].timestamp / 1000);
    Serial.print("s │ ADC:");
    Serial.print(alarmHistory[idx].adcValue);
    Serial.print(" │ Estado:");

    switch (alarmHistory[idx].status) {
      case SENSOR_DISCONNECTED:
        Serial.print("DESCONECTADO");
        break;
      case SENSOR_LOW_SIGNAL:
        Serial.print("SEÑAL_BAJA");
        break;
      case SENSOR_HIGH_SIGNAL:
        Serial.print("SEÑAL_ALTA");
        break;
      case SENSOR_SHORT:
        Serial.print("CORTOCIRCUITO");
        break;
      case SENSOR_ERRATIC:
        Serial.print("ERRATICO");
        break;
      default:
        Serial.print("OK");
        break;
    }
    Serial.println(" ║");
  }
  Serial.println("╚════════════════════════════════════════╝\n");
}

SensorStatus checkSensor(int adcValue) {
  // Deteccion de sensor mejorada con hysteresis y logging
  static int lastReading = 0;
  static bool firstReading = true;
  SensorStatus detectedStatus = SENSOR_OK;

  // ========== CHEQUEO POR VOLTAJE ==========
  if (adcValue < ADC_DISCONNECTED) {
    detectedStatus = SENSOR_DISCONNECTED;
  }
  else if (adcValue < ADC_LOW_WARNING) {
    detectedStatus = SENSOR_LOW_SIGNAL;
  }
  else if (adcValue > ADC_SHORT_CIRCUIT) {
    detectedStatus = SENSOR_SHORT;
  }
  else if (adcValue > ADC_HIGH_WARNING) {
    detectedStatus = SENSOR_HIGH_SIGNAL;
  }
  else {
    detectedStatus = SENSOR_OK;
  }

  // ========== DETECCION DE CAMBIOS ERRATICOS ==========
  if (!firstReading && detectedStatus == SENSOR_OK) {
    int change = abs(adcValue - lastReading);

    if (change > ADC_MAX_CHANGE) {
      // Cambio grande detectado - usar hysteresis
      if (!waitingErraticConfirmation) {
        // Primera deteccion
        waitingErraticConfirmation = true;
        erraticDetectedTime = millis();
        erraticConfirmationCount = 1;
        lastErraticReading = adcValue;
        Serial.print("⚠ POSIBLE ERRATICO: Cambio ADC=");
        Serial.print(change);
        Serial.println(" (esperando confirmacion)");
      } else {
        // Ya estamos en periodo de confirmacion
        erraticConfirmationCount++;

        if (millis() - erraticDetectedTime >= ERRATIC_CONFIRMATION_TIME) {
          // Periodo de espera expirado, evaluar confirmaciones
          if (erraticConfirmationCount >= ERRATIC_CONFIRMATION_THRESHOLD) {
            Serial.println("🔴 SENSOR ERRATICO CONFIRMADO");
            logAlarm(SENSOR_ERRATIC, adcValue);
            waitingErraticConfirmation = false;
            detectedStatus = SENSOR_ERRATIC;
          } else {
            // Falsa alarma - se normalizo
            Serial.println("✓ Falsa alarma erratica normalizada");
            waitingErraticConfirmation = false;
          }
        }

        if (waitingErraticConfirmation) {
          detectedStatus = SENSOR_OK;  // Aun no confirmar
        }
      }
    } else {
      // Cambio normal - resetear deteccion
      if (waitingErraticConfirmation) {
        Serial.println("✓ Cambio erratico se normalizo");
        waitingErraticConfirmation = false;
        erraticConfirmationCount = 0;
      }
    }
  }

  lastReading = adcValue;
  if (firstReading) {
    firstReading = false;
  }

  // ========== LOGGING Y SERIAL ==========
  if (detectedStatus != SENSOR_OK && detectedStatus != SENSOR_LOW_SIGNAL &&
      detectedStatus != SENSOR_HIGH_SIGNAL) {
    static SensorStatus lastLoggedStatus = SENSOR_OK;
    if (detectedStatus != lastLoggedStatus) {
      // Solo loguear cambios de estado critico
      logAlarm(detectedStatus, adcValue);
      lastLoggedStatus = detectedStatus;
    }
  }

  return detectedStatus;
}

void displaySensorError(SensorStatus status) {
  display.clearDisplay();
  
  // Fondo invertido para alarma critica
  if (status == SENSOR_DISCONNECTED || status == SENSOR_SHORT) {
    display.fillRect(0, 0, 128, 64, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  
  // Titulo de alarma
  display.setCursor(25, 2);
  display.setTextSize(1);
  display.print("!!! ALARMA !!!");
  
  // Linea separadora
  if (status == SENSOR_DISCONNECTED || status == SENSOR_SHORT) {
    display.drawLine(0, 12, 128, 12, SSD1306_BLACK);
  } else {
    display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  }
  
  // Mensaje especifico segun el tipo de falla
  display.setTextSize(1);
  switch(status) {
    case SENSOR_DISCONNECTED:
      display.setCursor(35, 20);
      display.print("SENSOR");
      display.setCursor(25, 30);
      display.print("DESCONECTADO");
      display.setCursor(20, 45);
      display.print("REVISAR CABLES");
      break;
      
    case SENSOR_LOW_SIGNAL:
      display.setCursor(25, 20);
      display.print("SENAL BAJA");
      display.setCursor(35, 30);
      display.print("< 0.5V");
      display.setCursor(15, 45);
      display.print("VERIFICAR SENSOR");
      break;
      
    case SENSOR_HIGH_SIGNAL:
      display.setCursor(25, 20);
      display.print("SENAL ALTA");
      display.setCursor(35, 30);
      display.print("> 4.5V");
      display.setCursor(15, 45);
      display.print("VERIFICAR SENSOR");
      break;
      
    case SENSOR_SHORT:
      display.setCursor(20, 20);
      display.print("CORTOCIRCUITO");
      display.setCursor(25, 30);
      display.print("DETECTADO");
      display.setCursor(20, 45);
      display.print("REVISAR CABLES");
      break;
      
    case SENSOR_ERRATIC:
      display.setCursor(30, 20);
      display.print("LECTURA");
      display.setCursor(30, 30);
      display.print("ERRATICA");
      display.setCursor(25, 45);
      display.print("RUIDO/FALLA");
      break;
  }
  
  // Indicador de bomba bloqueada
  display.setTextSize(1);
  if (status == SENSOR_DISCONNECTED || status == SENSOR_SHORT) {
    display.setCursor(15, 55);
    display.print("[BOMBA BLOQUEADA]");
  } else {
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 55);
    display.print("[BOMBA LIMITADA]");
  }
  
  display.display();
  
  // Parpadeo del LED para alarma
  if (millis() - lastAlarmBlink > ALARM_BLINK_INTERVAL) {
    lastAlarmBlink = millis();
    alarmLedState = !alarmLedState;
    digitalWrite(LED_PIN, alarmLedState);
  }
}

void checkInletValve() {
  // Leer estado de valvula de entrada en D7 (INPUT_PIN)
  // Solo si esta implementada
  // Señal BAJA = Valvula abierta (entra agua)
  // Señal ALTA = Valvula cerrada
  // DETECCION DE FALLA: Si valveImplemented=true pero D7 nunca cambia (HIGH continuo),
  //                     significa sensor desconectado → BLOQUEAR BOMBA

  // Si la valvula no esta implementada, mantener como cerrada
  if (!config.valveImplemented) {
    inletValveOpen = false;
    valveFaultDetected = false;  // Resetear deteccion de falla
    return;
  }

  unsigned long now = millis();

  // Debounce: ignorar cambios rapidos
  if (now - lastInletChange < INLET_DEBOUNCE_TIME) {
    return;
  }

  bool currentState = (digitalRead(INPUT_PIN) == LOW);  // LOW = valvula abierta
  bool currentStateHigh = (digitalRead(INPUT_PIN) == HIGH);  // HIGH = valvula cerrada (o desconectada)

  // ===== DETECCION DE FALLA: D7 SIEMPRE HIGH (sensor desconectado) =====
  if (config.valveImplemented && currentStateHigh) {
    // D7 esta HIGH (valvula cerrada o sensor desconectado)
    if (valveHighStartTime == 0) {
      // Primera vez que detectamos HIGH
      valveHighStartTime = now;
    }

    // Verificar si ha estado HIGH por demasiado tiempo
    unsigned long highDuration = now - valveHighStartTime;

    // Si estuvo HIGH por mas de VALVE_FAULT_THRESHOLD y NUNCA ha habido cambio = sensor desconectado
    if (highDuration >= VALVE_FAULT_THRESHOLD && !hasValveStateChanged) {
      // FALTA DETECTADA: Sensor de valvula desconectado
      if (!valveFaultDetected) {
        valveFaultDetected = true;
        pumpBlocked = true;  // BLOQUEAR BOMBA
        Serial.println("🔴 ERROR CRITICO: Sensor de valvula D7 DESCONECTADO");
        Serial.println("   Config indica valveImplemented=SI pero D7 no responde");
        Serial.println("   BOMBA BLOQUEADA - Revisar conexion de sensor o desabilitar valvula en Menu 5");
        logAlarm(SENSOR_DISCONNECTED, 0);  // Loguear como error
      }
    }
  } else if (config.valveImplemented && !currentStateHigh) {
    // D7 esta LOW (valvula abierta) = sensor esta activo/conectado
    valveHighStartTime = 0;  // Resetear contador HIGH
    hasValveStateChanged = true;  // Marcar que sensor respondio

    // Si antes detectamos falla, ahora se recupero
    if (valveFaultDetected) {
      valveFaultDetected = false;
      pumpBlocked = false;
      Serial.println("✓ Sensor de valvula D7 RECUPERADO");
      logAlarm(SENSOR_OK, 0);
    }
  }

  if (currentState != lastInletValveState) {
    // Cambio detectado
    lastInletChange = now;
    inletValveOpen = currentState;
    lastInletValveState = currentState;
    hasValveStateChanged = true;  // Marcar cambio de estado

    if (inletValveOpen) {
      Serial.println("💧 VALVULA DE ENTRADA ABIERTA");
    } else {
      Serial.println("✓ Valvula de entrada cerrada");
    }
  }
}

void checkTankLevel() {
  // Detectar bajo nivel de tanque en D6
  // D6 HIGH = tanque bajo/vacio = BLOQUEAR BOMBA
  // D6 LOW = tanque con agua = bomba puede funcionar
  // Solo si tankSensorImplemented = true

  // Si sensor no esta implementado, mantener como OK
  if (!config.tankSensorImplemented) {
    tankLevelLow = false;
    return;
  }

  unsigned long now = millis();

  // Debounce: ignorar cambios rapidos
  if (now - lastTankLevelChange < TANK_DEBOUNCE_TIME) {
    return;
  }

  bool currentState = (digitalRead(TANK_LEVEL_PIN) == HIGH);  // HIGH = tanque bajo

  if (currentState != lastTankLevelState) {
    // Cambio detectado
    lastTankLevelChange = now;
    tankLevelLow = currentState;
    lastTankLevelState = currentState;

    if (tankLevelLow) {
      Serial.println("⚠️  TANQUE BAJO - Bomba deshabilitada por seguridad");
      logAlarm(SENSOR_DISCONNECTED, 0);  // Usar log de alarma para registro
    } else {
      Serial.println("✓ Nivel de tanque OK - Bomba disponible");
    }
  }
}

void handleSensorRecovery() {
  // Sistema de recuperacion simplificado y mejorado
  if (sensorStatus == SENSOR_OK) {
    // Sensor OK - contar ciclos de recuperacion
    if (errorRecoveryCount > 0) {
      errorRecoveryCount--;
      if (errorRecoveryCount == 0) {
        Serial.println("✓ Sistema recuperado - Operacion normal");
        pumpBlocked = false;
        recoveryAttempts = 0;
      } else if (errorRecoveryCount % 5 == 0) {
        // Log cada 5 ciclos para no saturar serial
        Serial.print("  Recuperando: ");
        Serial.print(errorRecoveryCount);
        Serial.print("/");
        Serial.println(RECOVERY_CYCLES);
      }
    }
  } else {
    // Errores detectados
    if (sensorStatus == SENSOR_DISCONNECTED || sensorStatus == SENSOR_SHORT) {
      // Errores criticos - bloquear inmediatamente
      if (!pumpBlocked) {
        pumpBlocked = true;
        errorRecoveryCount = RECOVERY_CYCLES;
        recoveryAttempts = 0;
        Serial.println("🔴 ERROR CRITICO - BOMBA BLOQUEADA");
        logAlarm(sensorStatus, 0);
      }
    } else if (sensorStatus == SENSOR_ERRATIC) {
      // Error erratico - sistema de intentos limitados
      if (recoveryAttempts < MAX_RECOVERY_ATTEMPTS) {
        // Primer bloqueo del error erratico
        if (!pumpBlocked) {
          recoveryAttempts++;
          pumpBlocked = true;
          errorRecoveryCount = RECOVERY_CYCLES;
          lastRecoveryAttempt = millis();
          Serial.print("⚠ ERROR ERRATICO - Intento ");
          Serial.print(recoveryAttempts);
          Serial.print("/");
          Serial.println(MAX_RECOVERY_ATTEMPTS);
        }
      } else {
        // Agotados intentos - esperar RECOVERY_WAIT_TIME
        if (millis() - lastRecoveryAttempt >= RECOVERY_WAIT_TIME) {
          Serial.println("⏱ Reintentando recuperacion tras espera...");
          recoveryAttempts = 0;
          pumpBlocked = true;
          errorRecoveryCount = RECOVERY_CYCLES;
          lastRecoveryAttempt = millis();
        } else if (!pumpBlocked) {
          // Primera vez en espera
          pumpBlocked = true;
          Serial.println("⏱ Esperando 30s antes de reintentar...");
        }
      }
    }
  }
}

// ========== FUNCIONES DE UTILIDAD VISUAL ==========

void drawHeader(const char* title) {
  // Dibuja un header consistente para todos los menus
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.setTextSize(1);
  display.print(title);
  display.setTextColor(SSD1306_WHITE);
}

void drawFooter(const char* text) {
  // Dibuja un footer con instrucciones
  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setCursor(2, 56);
  display.setTextSize(1);
  display.print(text);
}

void drawProgressBar(int x, int y, int width, int height, int value, int minVal, int maxVal) {
  // Dibuja una barra de progreso
  display.drawRect(x, y, width, height, SSD1306_WHITE);
  int fillWidth = map(value, minVal, maxVal, 0, width - 2);
  if (fillWidth < 0) fillWidth = 0;
  if (fillWidth > width - 2) fillWidth = width - 2;
  display.fillRect(x + 1, y + 1, fillWidth, height - 2, SSD1306_WHITE);
}

void drawValueBox(int x, int y, int value, bool selected) {
  // Dibuja una caja con valor numerico
  if (selected) {
    display.fillRect(x - 2, y - 2, 40, 14, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRect(x - 2, y - 2, 40, 14, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(x + 2, y);
  display.setTextSize(1);
  display.print(value);
  display.setTextColor(SSD1306_WHITE);
}

void showSaveAnimation() {
  // Animacion de guardado
  display.clearDisplay();
  for(int i = 0; i < 3; i++) {
    display.clearDisplay();
    display.drawCircle(64, 32, 10 + i*5, SSD1306_WHITE);
    display.setCursor(42, 28);
    display.setTextSize(1);
    display.print("GUARDANDO");
    display.display();
    delay(150);
  }
  display.clearDisplay();
  display.setCursor(38, 28);
  display.setTextSize(2);
  display.print("LISTO!");
  display.display();
  delay(500);
}

// ========== SETUP ==========

void setup() {
  Serial.begin(115200);

  // Mensaje de inicio
  Serial.println("\n=== CONTROLADOR DE PRESION v3.0 ===");
  Serial.println("=== CON SISTEMA DE SEGURIDAD ===");
  #ifdef ESP32
    Serial.println("Plataforma: ESP32");
    // Configuracion ADC para ESP32
    analogSetAttenuation(ADC_11db);  // Permitir 0-3.3V + margen
    Serial.println("✓ ADC configurado para 3.3V");
    Serial.println("⚠️ NOTA: Se requiere divisor de voltaje 1:1 si el sensor da 0-5V");
  #else
    Serial.println("Plataforma: Arduino AVR (5V)");
    Serial.println("✓ ADC configurado para 5V");
  #endif

  // Configurar pines
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(INPUT_PIN, INPUT);       // D7 - Valvula de entrada
  pinMode(TANK_LEVEL_PIN, INPUT);  // D6 - Nivel bajo de tanque

  // Estado inicial seguro
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // Inicializar display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("❌ SSD1306 allocation failed"));
    for (;;);
  }

  // Cargar configuracion desde memoria persistente
  loadConfig();

  // Validar configuracion cargada
  if (!validateConfig(config)) {
    Serial.println("⚠️ Configuracion invalida al arrancar - cargando defaults");
    loadDefaults();
    saveConfig();
  }

  // Aplicar configuracion
  sp_value = config.setpoint;
  db_value = config.deadband;
  T_set = config.minOnTime;

  // Pantalla de inicio mejorada
  display.clearDisplay();
  display.drawRect(10, 10, 108, 44, SSD1306_WHITE);
  display.setCursor(25, 22);
  display.setTextSize(1);
  display.println("CONTROLADOR DE");
  display.setCursor(38, 32);
  display.println("PRESION v3.0");
  display.setCursor(25, 42);
  display.setTextSize(1);
  display.println("Safety System");
  display.display();
  delay(2000);

  // Configurar interrupciones para el encoder
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), readEncoder, CHANGE);

  Serial.println("✓ Sistema iniciado correctamente");
  Serial.println("═══════════════════════════════════");
}

// ========== LOOP PRINCIPAL ==========

void loop() {
  // Leer boton del encoder
  checkButton();
  
  // SIEMPRE ejecutar control de bomba (incluso en menu)
  operationMode();
  
  // Sobrescribir display si esta en menu
  if (currentMode == MODE_MENU_LIST) {
    menuListMode();
  } else if (currentMode == MODE_MENU_CONFIG) {
    menuConfigMode();
  }
  // Si esta en MODE_OPERATION, operationMode() ya dibujo su pantalla
}

// ========== FUNCIONES DEL ENCODER ==========

void readEncoder() {
  // Debounce: ignorar cambios rapidos (ruido)
  unsigned long now = millis();
  if (now - lastEncoderChange < ENCODER_DEBOUNCE_TIME) {
    return;
  }

  static int counter = 0;
  int clkValue = digitalRead(ENCODER_CLK);
  int dtValue = digitalRead(ENCODER_DT);

  if (clkValue != lastCLK) {
    lastEncoderChange = now;
    counter++;

    // Solo contar cada 2 cambios (1 click fisico = 2 transiciones)
    if (counter % 2 == 0) {
      if (dtValue != clkValue) {
        encoderPos++;
      } else {
        encoderPos--;
      }
    }
  }
  lastCLK = clkValue;
}

void checkButton() {
  static bool buttonPressed = false;
  static unsigned long pressStartTime = 0;
  static bool longPressHandled = false;

  if (digitalRead(ENCODER_SW) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      pressStartTime = millis();
    }

    // Detectar presion larga (1 segundo)
    if (millis() - pressStartTime >= longPressTime && !longPressHandled) {
      // Presion larga - alternar entre menu y operacion
      if (currentMode == MODE_OPERATION) {
        // Entrar a menu desde operacion
        currentMode = MODE_MENU_LIST;
        menuSelection = 0;
        encoderPos = 0;
        lastEncoderPos = 0;
        Serial.println("Entrando a MENU");
      } else {
        // Volver a operacion desde cualquier menu
        currentMode = MODE_OPERATION;
        usingTempConfig = false;  // Descartar cambios sin guardar
        editingValue = false;
        editingParam = 0;
        Serial.println("Volviendo a OPERACION");
      }

      longPressHandled = true;
      buttonPressed = false;
      delay(300);  // Debounce
    }
  } else {
    if (buttonPressed) {
      unsigned long pressDuration = millis() - pressStartTime;

      // Si se acaba de hacer una presion larga, consumir este evento
      if (longPressHandled) {
        longPressHandled = false;
        buttonPressed = false;
        return;
      }

      // Presion corta (< 1 segundo)
      if (pressDuration < longPressTime) {
        handleShortPress();
      }
      buttonPressed = false;
    }
  }
}

void handleShortPress() {
  if (currentMode == MODE_MENU_LIST) {
    // Entrar al menu seleccionado
    if (menuSelection == 6) {
      // Opcion de salir sin guardar (opcion 6 es SALIR)
      currentMode = MODE_OPERATION;
      usingTempConfig = false; // Descartar cambios temporales
    } else {
      // Copiar config actual a config_temp al entrar a edicion
      config_temp = config;
      usingTempConfig = true;
      currentMenu = menuSelection + 1;
      currentMode = MODE_MENU_CONFIG;
      encoderPos = 0;
      editingValue = false;
      editingParam = 0;
    }
  } 
  else if (currentMode == MODE_MENU_CONFIG) {
    if (!editingValue) {
      // Empezar a editar
      editingValue = true;
      encoderPos = 0;
    } else {
      // Cambiar al siguiente parametro o guardar
      handleMenuNavigation();
    }
  }
  delay(200);
}

// ========== MODO OPERACION ==========

void operationMode() {
  // Timing no bloqueante - ejecutar operacion cada 100ms
  if (millis() - sysState.lastOperationUpdate < OPERATION_UPDATE_INTERVAL) {
    return;  // No es tiempo aun
  }
  sysState.lastOperationUpdate = millis();

  // Leer sensor analogico
  int InVolt_1 = analogRead(ANALOG_PIN);

  // ========== VALIDACION DE SENSOR ==========
  sensorStatus = checkSensor(InVolt_1);

  // Manejar recuperacion del sistema
  handleSensorRecovery();

  // ========== VERIFICAR VALVULA DE ENTRADA ==========
  checkInletValve();

  // ========== VERIFICAR NIVEL DE TANQUE ==========
  checkTankLevel();

  // Si el sensor esta OK o en warning, calcular presion
  if (sensorStatus == SENSOR_OK ||
      sensorStatus == SENSOR_LOW_SIGNAL ||
      sensorStatus == SENSOR_HIGH_SIGNAL) {

    // Guardar ultima lectura valida
    lastValidReading = InVolt_1;

    // Calcular presion normalmente
    voltage = map(InVolt_1, 0, ANALOG_MAX, config.pressMin, config.pressMax);
    press = voltage;
    volt_An = map(InVolt_1, 0, ANALOG_MAX, 0, 80);
  } else {
    // Usar ultima lectura valida si hay error
    if (lastValidReading > 0) {
      voltage = map(lastValidReading, 0, ANALOG_MAX, config.pressMin, config.pressMax);
      press = voltage;
    } else {
      press = 0; // Sin lectura valida previa
    }
  }

  // Usar valores de configuracion
  sp_value = config.setpoint;
  db_value = config.deadband;
  T_set = config.minOnTime;

  // Calculos de tiempo
  T_syst = millis() / 1000;

  // ========== CONTROL DE BOMBA CON SEGURIDAD ==========

  // Si hay bloqueo O valvula abierta O tanque bajo, apagar bomba inmediatamente
  if (pumpBlocked || inletValveOpen || tankLevelLow) {
    digitalWrite(PUMP_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    pump = 0;
    T_zero = 0;
    T_on = 0;
  }
  // Control normal solo si no hay bloqueo, valvula cerrada y tanque con agua
  else {
    // Control de bomba con histeresis
    if (press >= (sp_value + db_value/2) && (T_on >= T_set)) {
      digitalWrite(PUMP_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
    }
    else if (press <= (sp_value - db_value/2)) {
      // Solo encender si el sensor esta OK
      if (sensorStatus == SENSOR_OK ||
          (sensorStatus == SENSOR_LOW_SIGNAL && press >= -5) ||
          (sensorStatus == SENSOR_HIGH_SIGNAL && press < 140)) {
        digitalWrite(PUMP_PIN, HIGH);
        digitalWrite(LED_PIN, HIGH);
      }
    }

    // Calcular tiempo ON
    pump = digitalRead(PUMP_PIN);
    if (pump == 1 && T_zero == 0) {
      T_zero = T_syst;
    }
    else if (pump == 0) {
      T_zero = 0;
      T_on = 0;
    }
    if (pump == 1) {
      T_on = T_syst - T_zero;
    }
    else {
      T_on = 0;
    }
  }

  // ========== ACTUALIZAR DISPLAY (cada 200ms) ==========
  if (millis() - sysState.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    sysState.lastDisplayUpdate = millis();

    // Solo mostrar pantalla si NO esta en menu
    if (currentMode == MODE_OPERATION) {
      // Si hay error critico, mostrar pantalla de error
      if (sensorStatus != SENSOR_OK && sensorStatus != lastSensorStatus) {
        Serial.print("🔔 ALARMA: Sensor status = ");
        Serial.println(sensorStatus);
      }
      lastSensorStatus = sensorStatus;

      if (sensorStatus != SENSOR_OK &&
          sensorStatus != SENSOR_LOW_SIGNAL &&
          sensorStatus != SENSOR_HIGH_SIGNAL) {
        displaySensorError(sensorStatus);
      } else {
        // Mostrar pantalla de operacion normal
        displayOperation();
      }
    }
  }

  // ========== DEBUG SERIAL (cada 5 segundos) ==========
  if (millis() - sysState.lastSerialDebug >= SERIAL_DEBUG_INTERVAL) {
    sysState.lastSerialDebug = millis();

    Serial.print("║ ADC: ");
    Serial.print(InVolt_1);
    Serial.print(" │ Presion: ");
    Serial.print(press);
    Serial.print(" psi │ sp_value: ");
    Serial.print(sp_value);
    Serial.print(" │ Bomba: ");
    Serial.print(pump ? "ON ✓" : "OFF ✗");
    Serial.print(" │ Valvula: ");
    Serial.print(inletValveOpen ? "ABIERTA 💧" : "CERRADA ✓");
    Serial.print(" │ Status: ");

    switch (sensorStatus) {
      case SENSOR_OK: Serial.println("OK ✓"); break;
      case SENSOR_DISCONNECTED: Serial.println("DESCONECTADO ✗"); break;
      case SENSOR_LOW_SIGNAL: Serial.println("SEÑAL_BAJA ⚠"); break;
      case SENSOR_HIGH_SIGNAL: Serial.println("SEÑAL_ALTA ⚠"); break;
      case SENSOR_SHORT: Serial.println("CORTOCIRCUITO ✗"); break;
      case SENSOR_ERRATIC: Serial.println("ERRATICO ⚠"); break;
    }
  }
}

void displayOperation() {
  display.clearDisplay();

  // Header mejorado con estados de seguridad
  if (tankLevelLow) {
    drawHeader("⚠️  TANQUE BAJO!");
  }
  else if (valveFaultDetected) {
    drawHeader("! SENSOR VALVULA ERROR !");
  }
  else if (inletValveOpen) {
    drawHeader("💧 VALVULA ENTRADA ABIERTA");
  }
  else if (pumpBlocked) {
    drawHeader("! SISTEMA BLOQUEADO !");
  }
  else if (errorRecoveryCount > 0) {
    drawHeader("RECUPERANDO...");
  }
  else if (sensorStatus == SENSOR_LOW_SIGNAL) {
    drawHeader("! SEÑAL BAJA !");
  }
  else if (sensorStatus == SENSOR_HIGH_SIGNAL) {
    drawHeader("! SEÑAL ALTA !");
  }
  else if(press < config.pressMin) {
    drawHeader("! CHECK TRANSMITTER !");
  }
  else if(press > (sp_value + db_value/2 + 5)) {
    drawHeader("!! ALARMA PRESION !!");
  }
  else {
    drawHeader("SISTEMA OPERANDO OK");
  }
  
  // Panel principal con diseño mejorado
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.print("PRESION:");
  
  // Valor de presion grande
  display.setTextSize(2);
  display.setCursor(50, 12);
  display.print(press);
  display.setTextSize(1);
  display.print(" psi");
  
  // Indicador de estado compacto - ICONOS GRAFICOS PROFESIONALES
  display.setTextSize(1);
  display.setCursor(0, 30);

  // Valvula: dibuja pequeño cilindro con estado
  if (config.valveImplemented) {
    display.drawRect(1, 25, 4, 8, SSD1306_WHITE);  // cilindro valvula
    if (valveFaultDetected) {
      display.drawLine(1, 25, 5, 33, SSD1306_WHITE);  // X = error
      display.drawLine(5, 25, 1, 33, SSD1306_WHITE);
    } else if (inletValveOpen) {
      display.fillRect(1, 25, 4, 8, SSD1306_WHITE);  // lleno = abierta
    } else {
      display.drawRect(1, 25, 4, 8, SSD1306_WHITE);  // vacio = cerrada
    }
  } else {
    display.drawLine(0, 29, 5, 29, SSD1306_WHITE);  // - = no implementada
  }

  // Tanque: dibuja pequeño rectangulo con nivel
  if (config.tankSensorImplemented) {
    display.drawRect(11, 25, 5, 8, SSD1306_WHITE);  // tanque
    if (tankLevelLow) {
      display.drawLine(11, 25, 16, 33, SSD1306_WHITE);  // X = bajo
      display.drawLine(16, 25, 11, 33, SSD1306_WHITE);
    } else {
      display.fillRect(13, 30, 3, 3, SSD1306_WHITE);  // puntito = ok
    }
  } else {
    display.drawLine(10, 29, 15, 29, SSD1306_WHITE);  // - = no implementado
  }

  // Bomba: dibuja pequeño circulo con estado dinamico
  display.drawCircle(25, 29, 3, SSD1306_WHITE);  // circulo bomba
  if (inletValveOpen || pumpBlocked) {
    display.drawLine(22, 26, 28, 32, SSD1306_WHITE);  // X = bloqueada/inhibida
    display.drawLine(28, 26, 22, 32, SSD1306_WHITE);
  } else if (pump == 1) {
    display.fillCircle(25, 29, 3, SSD1306_WHITE);  // lleno = encendida
  } else {
    display.drawCircle(25, 29, 3, SSD1306_WHITE);  // vacio = apagada
  }

  // Texto compacto de estado (derecha)
  display.setCursor(35, 30);
  if (valveFaultDetected) {
    display.print("V:ERR");
  } else if (inletValveOpen) {
    display.print("INHIB");
  } else if (pumpBlocked) {
    display.print("BLK");
  } else if (pump == 1) {
    display.print("ON");
  } else {
    display.print("OFF");
  }

  // Indicador complementario (derecha)
  if (errorRecoveryCount > 0) {
    // Durante recuperacion, mostrar icono de ciclos buenos
    display.setCursor(100, 30);
    display.print("*");
    display.print(errorRecoveryCount);
  } else if (pump == 1 && !pumpBlocked) {
    // Mostrar icono de flujo cuando la bomba esta activa
    display.setCursor(100, 30);
    display.drawTriangle(100, 26, 100, 34, 105, 30, SSD1306_WHITE);
    display.setCursor(110, 30);
    display.print("act");
  } else {
    // Mostrar voltaje del sensor en reposo
    display.setCursor(100, 30);
    display.print("V:");
    display.print(volt_An);
  }
  
  // Panel de informacion - DISEÑO COMPACTO Y GRAFICO
  display.drawLine(0, 40, 128, 40, SSD1306_WHITE);

  // Informacion en columnas con estado del sensor (diseño mejorado)
  if (errorRecoveryCount > 0) {
    display.setCursor(0, 44);
    display.print("RECOVER:");
    display.print(errorRecoveryCount);
    display.print("/");
    display.print(RECOVERY_CYCLES);
  } else {
    // Abreviaturas ultra-compactas con iconos visuales
    display.setCursor(0, 44);
    display.print("SP:");
    display.print(sp_value);
    display.print("p");

    display.setCursor(40, 44);
    display.print("DB:");
    display.print(db_value);
    display.print("p");

    display.setCursor(80, 44);
    display.print("MIN:");
    display.print(T_set);
    display.print("s");
  }
  
  // Barra de presion mejorada
  drawProgressBar(0, 54, 128, 10, press, 0, 100);
  
  // Indicador del setpoint en la barra
  int spPosition = map(sp_value, 0, 100, 0, 128);
  display.drawLine(spPosition, 52, spPosition, 64, SSD1306_WHITE);
  
  display.display();
}

// ========== MODO LISTA DE MENUS ==========

void menuListMode() {
  // Actualizar seleccion con encoder
  if (encoderPos != lastEncoderPos) {
    menuSelection += (encoderPos > lastEncoderPos) ? 1 : -1;
    if (menuSelection < 0) menuSelection = 0;
    if (menuSelection > 6) menuSelection = 6; // 7 opciones (0-6), incluyendo SALIR
    lastEncoderPos = encoderPos;
  }

  display.clearDisplay();
  drawHeader("MENU CONFIGURACION");

  // Items del menu con iconos
  const char* menuItems[] = {
    "Rango entrada",
    "Punto ajuste",
    "Banda muerta",
    "Tiempo min ON",
    "Valvula entrada",
    "Sensor tanque",
    "< SALIR"
  };

  const char* menuIcons[] = {
    "[~]", // Rango
    "[o]", // Setpoint
    "[=]", // Deadband
    "[T]", // Tiempo
    "[V]", // Valvula
    "[S]", // Sensor tanque
    "[X]"  // Salir
  };

  // Mostrar opciones con scroll (max 5 items visibles)
  int scrollOffset = 0;
  if (menuSelection > 2) {
    scrollOffset = menuSelection - 2;  // Mantener 2 items antes de la seleccion
  }
  if (scrollOffset > 7 - 5) {
    scrollOffset = 7 - 5;  // No scrollear mas alla del final
  }

  for (int i = 0; i < 5; i++) {
    int itemIndex = scrollOffset + i;
    if (itemIndex >= 7) break;

    int yPos = 14 + (i * 10);

    if (itemIndex == menuSelection) {
      // Opcion seleccionada
      display.fillRect(0, yPos - 1, 128, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(4, yPos);
    display.print(menuIcons[itemIndex]);
    display.setCursor(25, yPos);
    display.print(menuItems[itemIndex]);

    display.setTextColor(SSD1306_WHITE);
  }

  // Indicador de scroll si hay mas items
  if (scrollOffset > 0 || scrollOffset + 5 < 7) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(120, 14);
    if (scrollOffset > 0) display.print("^");
    if (scrollOffset + 5 < 7) {
      display.setCursor(120, 54);
      display.print("v");
    }
  }

  display.display();
}

// ========== MODO CONFIGURACION DE MENU ==========

void menuConfigMode() {
  display.clearDisplay();

  switch(currentMenu) {
    case 1: displayMenu1(); break;
    case 2: displayMenu2(); break;
    case 3: displayMenu3(); break;
    case 4: displayMenu4(); break;
    case 5: displayMenu5(); break;
    case 6: displayMenu6(); break;
  }

  display.display();
}

// ========== MENU CONFIGURATION STRUCTURE (REFACTORIZACIÓN v3.2) ==========

typedef struct {
  const char* title;        // Titulo del menu
  const char* label;        // Label del parametro
  const char* unit;         // Unidad (psi, seg, etc)
  int* value;               // Puntero al valor en config_temp
  int minVal;               // Minimo valor permitido
  int maxVal;               // Maximo valor permitido
  int step;                 // Paso del encoder (1 o 5)
  int displayMax;           // Maximo para barra de progreso (si aplica)
  bool isBoolean;           // Si es bool (SI/NO) en lugar de número
  bool showProgressBar;     // Si mostrar barra de progreso
} MenuConfig;

// Tabla de configuracion de TODOS los menus (elimina 362 lineas de duplicacion)
MenuConfig menuConfigs[] = {
  // MENU 1 es especial: 2 parametros, se maneja en displayMenu1()
  {NULL, NULL, NULL, NULL, 0, 0, 0, 0, false, false},

  // MENU 2: Setpoint
  {"PUNTO DE AJUSTE", "Setpoint", "psi", &config_temp.setpoint, 10, 100, 1, 100, false, true},

  // MENU 3: Deadband
  {"BANDA MUERTA", "Deadband", "psi", &config_temp.deadband, 5, 40, 1, 40, false, false},

  // MENU 4: Min On Time
  {"TIEMPO MINIMO", "Tiempo Min", "seg", &config_temp.minOnTime, 1, 10, 1, 10, false, false},

  // MENU 5: Valve Implemented (Boolean)
  {"VALVULA DE ENTRADA", "Implementada", "", &(int&)config_temp.valveImplemented, 0, 1, 1, 1, true, false},

  // MENU 6: Tank Sensor (Boolean)
  {"SENSOR TANQUE", "Implementado", "", &(int&)config_temp.tankSensorImplemented, 0, 1, 1, 1, true, false},
};

// Funcion generica para menus con 1 parametro numerico o boolean
void displayMenuGeneric(int menuId) {
  MenuConfig cfg = menuConfigs[menuId];

  drawHeader(cfg.title);

  if (!editingValue) {
    // ===== MODO VIS UALIZACION =====
    display.setCursor(20, 20);
    display.print(cfg.label);
    display.print(":");

    // Mostrar valor
    display.setTextSize(2);
    display.setCursor(40, 32);

    if (cfg.isBoolean) {
      display.print(*cfg.value ? "SI" : "NO");
    } else {
      display.print(*cfg.value);
    }

    display.setTextSize(1);
    if (strlen(cfg.unit) > 0) {
      display.print(" ");
      display.print(cfg.unit);
    }

    // Barra visual si aplica
    if (cfg.showProgressBar && !cfg.isBoolean) {
      drawProgressBar(10, 48, 108, 6, *cfg.value, cfg.minVal, cfg.displayMax);
    }

    drawFooter("[OK] Editar  [HOLD] Menu");
  } else {
    // ===== MODO EDICION =====
    display.setCursor(20, 16);
    display.print("Nuevo ");
    display.println(cfg.label);

    // Recuadro con valor editandose
    display.fillRect(30, 28, 68, 18, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(2);
    display.setCursor(38, 30);

    if (cfg.isBoolean) {
      display.print(*cfg.value ? "SI" : "NO");
    } else {
      display.print(*cfg.value);
    }

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    if (strlen(cfg.unit) > 0) {
      display.setCursor(92, 34);
      display.print(cfg.unit);
    }

    // Ajustar con encoder
    if (encoderPos != lastEncoderPos) {
      int delta = (encoderPos > lastEncoderPos) ? cfg.step : -cfg.step;
      *cfg.value += delta;

      // Clampear al rango valido
      if (*cfg.value < cfg.minVal) *cfg.value = cfg.minVal;
      if (*cfg.value > cfg.maxVal) *cfg.value = cfg.maxVal;

      lastEncoderPos = encoderPos;
    }

    // Barra visual si aplica
    if (cfg.showProgressBar && !cfg.isBoolean) {
      drawProgressBar(10, 48, 108, 6, *cfg.value, cfg.minVal, cfg.displayMax);
    }

    drawFooter("[OK] Guardar");
  }
}

void displayMenu1() {
  // Menu 1: Rango de entrada con diseño mejorado
  drawHeader("RANGO DE ENTRADA");
  
  if (!editingValue) {
    // Mostrar valores actuales con visualizacion mejorada
    display.setCursor(10, 18);
    display.print("Minimo:");
    drawValueBox(60, 16, config_temp.pressMin, false);
    display.setCursor(100, 18);
    display.print("psi");
    
    display.setCursor(10, 32);
    display.print("Maximo:");
    drawValueBox(60, 30, config_temp.pressMax, false);
    display.setCursor(100, 32);
    display.print("psi");
    
    // Visualizacion grafica del rango
    drawProgressBar(10, 46, 108, 6, 50, config_temp.pressMin, config_temp.pressMax);
    
    drawFooter("[OK] Editar  [HOLD] Menu");
  } else {
    if (editingParam == 0) editingParam = 1;
    
    // Editar minimo
    if (editingParam == 1) {
      display.setCursor(10, 18);
      display.print("Minimo:");
      drawValueBox(60, 16, config_temp.pressMin, true);
      display.setCursor(100, 18);
      display.print("psi");
      
      display.setCursor(10, 32);
      display.print("Maximo:");
      drawValueBox(60, 30, config_temp.pressMax, false);
      display.setCursor(100, 32);
      display.print("psi");
      
      // Ajustar con encoder
      if (encoderPos != lastEncoderPos) {
        config_temp.pressMin += (encoderPos > lastEncoderPos) ? 5 : -5;
        if (config_temp.pressMin < -100) config_temp.pressMin = -100;
        if (config_temp.pressMin > config_temp.pressMax - 10) config_temp.pressMin = config_temp.pressMax - 10;
        lastEncoderPos = encoderPos;
      }
      
      drawFooter("[OK] Siguiente");
    } 
    // Editar maximo
    else if (editingParam == 2) {
      display.setCursor(10, 18);
      display.print("Minimo:");
      drawValueBox(60, 16, config_temp.pressMin, false);
      display.setCursor(100, 18);
      display.print("psi");
      
      display.setCursor(10, 32);
      display.print("Maximo:");
      drawValueBox(60, 30, config_temp.pressMax, true);
      display.setCursor(100, 32);
      display.print("psi");
      
      // Ajustar con encoder
      if (encoderPos != lastEncoderPos) {
        config_temp.pressMax += (encoderPos > lastEncoderPos) ? 5 : -5;
        if (config_temp.pressMax < config_temp.pressMin + 10) config_temp.pressMax = config_temp.pressMin + 10;
        if (config_temp.pressMax > 500) config_temp.pressMax = 500;
        lastEncoderPos = encoderPos;
      }
      
      drawFooter("[OK] Guardar");
    }
    
    // Visualizacion del rango actual
    drawProgressBar(10, 46, 108, 6, 50, config_temp.pressMin, config_temp.pressMax);
  }
}

void displayMenu2() {
  // Menu 2: Usa funcion generica (refactorizacion v3.2)
  displayMenuGeneric(2);
}

void displayMenu3() {
  // Menu 3: Usa funcion generica (refactorizacion v3.2)
  displayMenuGeneric(3);
}

void displayMenu4() {
  // Menu 4: Usa funcion generica (refactorizacion v3.2)
  displayMenuGeneric(4);
}

bool validateConfig(Config& cfg) {
  // Validar que la configuracion es valida
  if (cfg.pressMin >= cfg.pressMax - 10) {
    Serial.println("❌ ERROR: Min debe ser < Max - 10");
    return false;
  }
  if (cfg.setpoint < 10 || cfg.setpoint > 100) {
    Serial.println("❌ ERROR: Setpoint debe estar entre 10-100");
    return false;
  }
  if (cfg.deadband < 5 || cfg.deadband > 40) {
    Serial.println("❌ ERROR: Deadband debe estar entre 5-40");
    return false;
  }
  if (cfg.minOnTime < 1 || cfg.minOnTime > 10) {
    Serial.println("❌ ERROR: Tiempo minimo debe estar entre 1-10s");
    return false;
  }
  return true;
}

void displayMenu5() {
  // Menu 5: Usa funcion generica (refactorizacion v3.2)
  displayMenuGeneric(5);
}

void displayMenu6() {
  // Menu 6: Usa funcion generica (refactorizacion v3.2)
  displayMenuGeneric(6);
}

void handleMenuNavigation() {
  if (currentMenu == 1) {
    // Menu 1: tiene 2 parametros
    if (editingParam == 1) {
      editingParam = 2;
      encoderPos = 0;
    } else {
      // Validar antes de guardar
      if (!validateConfig(config_temp)) {
        Serial.println("⚠️ Configuracion invalida - rechazada");
        return;  // No guardar si es invalida
      }

      // Aplicar cambios de config_temp a config
      config = config_temp;
      usingTempConfig = false;
      // Guardar y volver con animacion
      saveConfig();
      Serial.println("✓ Configuracion guardada correctamente");
      showSaveAnimation();
      currentMode = MODE_MENU_LIST;
      editingValue = false;
      editingParam = 0;
    }
  } else if (currentMenu == 5) {
    // Menu 5: Valvula (solo 1 parametro boolean)
    // Validar y guardar directamente
    if (!validateConfig(config_temp)) {
      Serial.println("⚠️ Configuracion invalida - rechazada");
      return;
    }

    config = config_temp;
    usingTempConfig = false;
    saveConfig();
    Serial.println("✓ Configuracion valvula guardada correctamente");
    if (config.valveImplemented) {
      Serial.println("💧 Valvula de entrada HABILITADA");
    } else {
      Serial.println("✓ Valvula de entrada DESHABILITADA");
    }
    // Resetear tracking de valvula cuando cambia la configuracion
    resetValveTracking();
    showSaveAnimation();
    currentMode = MODE_MENU_LIST;
    editingValue = false;
  } else if (currentMenu == 6) {
    // Menu 6: Sensor de tanque (solo 1 parametro boolean)
    // Validar y guardar directamente
    if (!validateConfig(config_temp)) {
      Serial.println("⚠️ Configuracion invalida - rechazada");
      return;
    }

    config = config_temp;
    usingTempConfig = false;
    saveConfig();
    Serial.println("✓ Configuracion sensor tanque guardada correctamente");
    if (config.tankSensorImplemented) {
      Serial.println("📊 Sensor de bajo nivel de tanque HABILITADO");
    } else {
      Serial.println("✓ Sensor de bajo nivel de tanque DESHABILITADO");
    }
    showSaveAnimation();
    currentMode = MODE_MENU_LIST;
    editingValue = false;
  } else {
    // Otros menus (2, 3, 4): solo 1 parametro
    // Validar antes de guardar
    if (!validateConfig(config_temp)) {
      Serial.println("⚠️ Configuracion invalida - rechazada");
      return;  // No guardar si es invalida
    }

    // Aplicar cambios de config_temp a config
    config = config_temp;
    usingTempConfig = false;
    // Guardar
    saveConfig();
    Serial.println("✓ Configuracion guardada correctamente");
    showSaveAnimation();
    currentMode = MODE_MENU_LIST;
    editingValue = false;
  }
}

// ========== FUNCIONES DE ALMACENAMIENTO PERSISTENTE ==========

void loadConfig() {
  #ifdef ESP32
    // ESP32 usa Preferences
    preferences.begin("pressure", false);

    // Verificar si hay datos guardados y version compatible
    byte storedVersion = preferences.getUChar("version", 0);

    if (preferences.getBool("saved", false) && storedVersion == CONFIG_VERSION) {
      config.version = CONFIG_VERSION;
      config.pressMin = preferences.getInt("pressMin", DEFAULT_PRESS_MIN);
      config.pressMax = preferences.getInt("pressMax", DEFAULT_PRESS_MAX);
      config.setpoint = preferences.getInt("setpoint", DEFAULT_SETPOINT);
      config.deadband = preferences.getInt("deadband", DEFAULT_DEADBAND);
      config.minOnTime = preferences.getInt("minOnTime", DEFAULT_MIN_TIME);
      config.valveImplemented = preferences.getBool("valveImplemented", DEFAULT_VALVE_IMPLEMENTED);
      config.tankSensorImplemented = preferences.getBool("tankSensorImplemented", DEFAULT_TANK_SENSOR_IMPLEMENTED);
      Serial.println("✓ Configuracion cargada desde Preferences (v" + String(storedVersion) + ")");
    } else if (preferences.getBool("saved", false) && storedVersion != CONFIG_VERSION) {
      // Estructura cambio, regenerar defaults
      loadDefaults();
      saveConfig();
      Serial.println("⚠️ Version de estructura cambio, regenerando configuracion por defecto");
    } else {
      // Primera vez, usar valores por defecto
      loadDefaults();
      Serial.println("Primera ejecucion, usando valores por defecto");
    }
    preferences.end();
  #else
    // Arduino AVR usa EEPROM
    EEPROM.get(0, config);

    // Validar version y checksum
    byte calculatedChecksum = calculateChecksum();
    if (config.version != CONFIG_VERSION) {
      // Estructura cambio, regenerar defaults
      loadDefaults();
      saveConfig();
      Serial.println("⚠️ Version de estructura cambio (v" + String(config.version) + " -> v" + String(CONFIG_VERSION) + "), regenerando configuracion");
    } else if (config.checksum != calculatedChecksum) {
      // Datos invalidos, cargar defaults
      loadDefaults();
      saveConfig();
      Serial.println("❌ Checksum invalido, usando valores por defecto");
    } else {
      Serial.println("✓ Configuracion cargada desde EEPROM (v" + String(config.version) + ")");
    }
  #endif

  // Mostrar configuracion cargada con separador visible
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     CONFIGURACION ACTUAL CARGADA      ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.print("║ Min Presion: ");
  Serial.print(config.pressMin);
  Serial.println(" psi");
  Serial.print("║ Max Presion: ");
  Serial.print(config.pressMax);
  Serial.println(" psi");
  Serial.print("║ Setpoint:    ");
  Serial.print(config.setpoint);
  Serial.println(" psi");
  Serial.print("║ Deadband:    ");
  Serial.print(config.deadband);
  Serial.println(" psi");
  Serial.print("║ Tiempo Min:  ");
  Serial.print(config.minOnTime);
  Serial.println(" seg");
  Serial.print("║ Valvula:     ");
  Serial.println(config.valveImplemented ? "IMPLEMENTADA ✓" : "NO IMPLEMENTADA");
  Serial.print("║ Tank Sensor: ");
  Serial.println(config.tankSensorImplemented ? "IMPLEMENTADO ✓" : "NO IMPLEMENTADO");
  Serial.println("╚════════════════════════════════════════╝\n");

  // Resetear tracking de falla de valvula
  resetValveTracking();
}

void resetValveTracking() {
  // Resetear variables de seguimiento de valvula cuando se carga configuracion
  valveFaultDetected = false;
  valveHighStartTime = 0;
  hasValveStateChanged = false;
  lastInletValveState = false;
  inletValveOpen = false;
  Serial.println("  Valve tracking reseteado");
}

void saveConfig() {
  #ifdef ESP32
    // ESP32 usa Preferences
    preferences.begin("pressure", false);
    preferences.putBool("saved", true);
    preferences.putUChar("version", CONFIG_VERSION);
    preferences.putInt("pressMin", config.pressMin);
    preferences.putInt("pressMax", config.pressMax);
    preferences.putInt("setpoint", config.setpoint);
    preferences.putInt("deadband", config.deadband);
    preferences.putInt("minOnTime", config.minOnTime);
    preferences.putBool("valveImplemented", config.valveImplemented);
    preferences.putBool("tankSensorImplemented", config.tankSensorImplemented);
    preferences.end();
    Serial.println("✓ Configuracion guardada en Preferences (v" + String(CONFIG_VERSION) + ")");
  #else
    // Arduino AVR usa EEPROM
    config.version = CONFIG_VERSION;
    config.checksum = calculateChecksum();
    EEPROM.put(0, config);
    Serial.println("✓ Configuracion guardada en EEPROM (v" + String(CONFIG_VERSION) + ")");
  #endif
}

void loadDefaults() {
  config.version = CONFIG_VERSION;
  config.pressMin = DEFAULT_PRESS_MIN;
  config.pressMax = DEFAULT_PRESS_MAX;
  config.setpoint = DEFAULT_SETPOINT;
  config.deadband = DEFAULT_DEADBAND;
  config.minOnTime = DEFAULT_MIN_TIME;
  config.valveImplemented = DEFAULT_VALVE_IMPLEMENTED;
  config.tankSensorImplemented = DEFAULT_TANK_SENSOR_IMPLEMENTED;
  config.checksum = calculateChecksum();
}

byte calculateChecksum() {
  // Usar CRC-8 mejorado en lugar de suma simple
  byte crc = 0xAB;  // Valor inicial

  // Procesar cada byte del struct
  byte* data = (byte*)&config;
  for (int i = 0; i < sizeof(config) - 1; i++) {  // -1 para excluir el checksum
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x07;  // Polinomio CRC-8
      } else {
        crc = (crc << 1);
      }
      crc &= 0xFF;
    }
  }
  return crc;
}

void clearStoredConfig() {
  // Limpiar configuracion guardada (factory reset)
  #ifdef ESP32
    preferences.begin("pressure", false);
    preferences.clear();
    preferences.end();
    Serial.println("✓ Memoria Preferences limpiada (factory reset)");
  #else
    // Limpiar EEPROM
    for (int i = 0; i < sizeof(config); i++) {
      EEPROM.write(i, 0xFF);
    }
    Serial.println("✓ Memoria EEPROM limpiada (factory reset)");
  #endif

  // Cargar defaults
  loadDefaults();
  Serial.println("✓ Configuracion reseteada a valores de fabrica");
}