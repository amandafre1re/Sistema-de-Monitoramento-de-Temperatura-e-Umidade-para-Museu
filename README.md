# Sistema de Monitoramento de Temperatura e Umidade para Museu 

Sistema embarcado baseado em ESP32 para monitoramento em tempo real de condições ambientais, desenvolvido para preservação de acervos em ambientes museológicos.

### Grupo
  - Amanda Freire
  - Valentina Lago

## 📋 Sobre o Projeto

Este projeto implementa um sistema completo de monitoramento e controle de temperatura e umidade utilizando ESP32 e sensor DHT11. O sistema oferece interface web responsiva, registro de eventos históricos e sistema de alarmes automático para proteção de acervos.

### Principais Funcionalidades

- ✅ Monitoramento em tempo real de temperatura e umidade
- ✅ Interface web responsiva acessível via WiFi
- ✅ Sistema de alarmes automático com alertas visuais e sonoros
- ✅ Registro persistente de eventos em LittleFS
- ✅ Detecção de desligamentos inesperados
- ✅ Modo manual e automático de operação
- ✅ Indicadores LED para diferentes estados do sistema
- ✅ Sincronização de horário via NTP

## 🔧 Hardware Utilizado

### Componentes Principais

| Componente | Modelo/Especificação |
|------------|---------------------|
| Microcontrolador | ESP32 |
| Sensor de Temperatura/Umidade | DHT11 |
| Buzzer | Indicador de alarme|
| LED Vermelho | Indicador de alarme |
| LED Azul | Indicador de umidade |
| LED Amarelo | Indicador de conectividade |

### Pinagem

```cpp
DHT Sensor    → GPIO 14
Buzzer        → GPIO 33
LED Vermelho  → GPIO 25
LED Azul      → GPIO 27
LED Amarelo   → GPIO 26
```

## Parâmetros de Monitoramento

### Temperatura
- **Ideal**: 18°C - 20°C
- **Aceitável**: < 18°C ou 20°C - 24°C
- **Crítico**: 24°C - 57°C
- **Alerta de Incêndio**: ≥ 57°C (alarme automático)

### Umidade
- **Ideal**: 45% - 55%
- **Baixa**: < 45%
- **Alta**: > 55%


