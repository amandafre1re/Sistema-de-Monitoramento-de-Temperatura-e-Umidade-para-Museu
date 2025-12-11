let isTogglingManually = false;
let modoManual = false;

const WIFI_RANGES = [
  { min: -50, label: 'Sinal Excelente', cls: 'wifi-excelente' },
  { min: -65, label: 'Sinal Bom', cls: 'wifi-bom' },
  { min: -75, label: 'Sinal Aceitável', cls: 'wifi-aceitavel' },
  { min: -85, label: 'Sinal Fraco', cls: 'wifi-fraco' },
  { min: -120, label: 'Sinal Muito fraco', cls: 'wifi-muito-fraco' }
];

const WIFI_CLASSES = WIFI_RANGES.map(range => range.cls).concat('wifi-desconectado');
const wifiStatusEl = document.getElementById('wifi-status');
const wifiRssiEl = document.getElementById('wifi-rssi');

function resumirWifi(rssi) {
  if (typeof rssi !== 'number' || rssi <= -127) {
    return { label: 'Sem conexão', cls: 'wifi-desconectado' };
  }
  return WIFI_RANGES.find(range => rssi >= range.min) || WIFI_RANGES[WIFI_RANGES.length - 1];
}

async function fetchDados() {
  try {
    const res = await fetch('/dados');
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    // Atualiza valores de sensores
    document.getElementById('temperature-value').textContent = data.temperatura.toFixed(1) + ' °C';
    document.getElementById('humidity-value').textContent = data.umidade.toFixed(1) + ' %';

    //Atualiza condicao do ambiente
    document.getElementById('status-temperatura').textContent = data.situacao;
    document.getElementById('status-umidade').textContent = data.situacaoUmid;

    // Atualiza tempos das funções
    // Tempo de leitura do sensor (unificado: temperatura + umidade)
    const tSensorEl = document.getElementById('tSensor');
    if (tSensorEl) tSensorEl.textContent = (typeof data.tempoLerSensor !== 'undefined') ? (data.tempoLerSensor + ' ms') : '--';

    document.getElementById('tUmid').textContent = data.tempoConectarServidor + ' ms';
    document.getElementById('tLigar').textContent = data.tempoLigarAlarme + ' ms';
    document.getElementById('tDesligar').textContent = data.tempoDesligarAlarme + ' ms';

    // Atualiza modo geral
    modoManual = !!data.modo_geral;
    
    // Atualiza UI do modo
    const modoText = document.getElementById('modo-text');
    const modoDescription = document.getElementById('modo-description');
    const btnAlterarModo = document.getElementById('btnAlterarModo');
    const controlesOverlay = document.getElementById('controles-overlay');
    const controlesWrapper = document.querySelector('.controles-wrapper');
    
    if (modoManual) {
      modoText.textContent = 'Manual';
      modoDescription.textContent = 'Controle manual ativado. Use os botões abaixo para controlar os dispositivos.';
      btnAlterarModo.textContent = 'Alternar para Modo Automático';
      controlesOverlay.classList.add('hidden');
      controlesWrapper.classList.add('modo-manual');
    } else {
      modoText.textContent = 'Automático';
      modoDescription.textContent = 'Sistema opera automaticamente baseado nos sensores. Alarmes e LEDs são controlados automaticamente.';
      btnAlterarModo.textContent = 'Alternar para Modo Manual';
      controlesOverlay.classList.remove('hidden');
      controlesWrapper.classList.remove('modo-manual');
    }

    // Só atualiza os switches se NÃO estiver toggleando manualmente
    if (!isTogglingManually) {
      document.getElementById('btnToggleBuzzer').checked = !!data.alarme;
      document.getElementById('btnToggleLed').checked = !!data.led;
      document.getElementById('btnToggleLedAzul').checked = !!data.led_azul;
    }

    // Atualiza status visual (ex: alerta)
    const fireAlert = document.getElementById('fire-alert');
    if (fireAlert) {
      if (data.alarme) {
        fireAlert.classList.remove('hidden');
      } else {
        fireAlert.classList.add('hidden');
      }
    }

    // Atualiza horário da última leitura
    const now = new Date();
    document.getElementById('update-time').textContent = now.toLocaleTimeString();

    // Atualiza informação do Wi-Fi
    if (wifiStatusEl && wifiRssiEl) {
      const rssi = (typeof data.wifiRSSI === 'number') ? data.wifiRSSI : -127;
      const info = resumirWifi(rssi);
      wifiStatusEl.textContent = info.label;
      wifiRssiEl.textContent = (rssi <= -127) ? '-- dBm' : `${rssi} dBm`;

      wifiStatusEl.classList.remove(...WIFI_CLASSES);
      wifiStatusEl.classList.add(info.cls);
      wifiRssiEl.classList.remove(...WIFI_CLASSES);
      wifiRssiEl.classList.add(info.cls);
    }

  } catch (e) {
    console.error('Erro ao buscar /dados', e);
    const alert = document.getElementById('connection-alert');
    if (alert) alert.classList.remove('hidden');
    if (wifiStatusEl && wifiRssiEl) {
      const info = resumirWifi(-127);
      wifiStatusEl.textContent = info.label;
      wifiRssiEl.textContent = '-- dBm';
      wifiStatusEl.classList.remove(...WIFI_CLASSES);
      wifiStatusEl.classList.add(info.cls);
      wifiRssiEl.classList.remove(...WIFI_CLASSES);
      wifiRssiEl.classList.add(info.cls);
    }
  }
}

async function toggle(url) {
  // Bloqueia atualizações durante o toggle
  isTogglingManually = true;
  
  try {
    const response = await fetch(url);
    if (!response.ok) {
      const text = await response.text();
      alert(text);
    }
    console.log('Comando enviado:', url);
  } catch (e) {
    console.error('Erro ao enviar comando', e);
  }
  
  // LED Azul precisa de mais tempo por causa do controle automático de umidade
  const delay = url.includes('LedAzul') ? 1500 : 1000;
  
  setTimeout(() => {
    isTogglingManually = false;
    console.log('Liberado para atualizar checkboxes');
  }, delay);
}

async function alterarModoGeral() {
  isTogglingManually = true;
  
  try {
    await fetch('/toggleModoGeral');
    console.log('Modo geral alterado');
    
    // Aguarda um pouco e atualiza
    await new Promise(resolve => setTimeout(resolve, 300));
    await fetchDados();
  } catch (e) {
    console.error('Erro ao alterar modo', e);
  }
  
  setTimeout(() => {
    isTogglingManually = false;
  }, 500);
}

// Evento do botão de alternar modo
document.getElementById('btnAlterarModo').addEventListener('click', alterarModoGeral);

// Eventos dos switches de controle
document.getElementById('btnToggleBuzzer').addEventListener('change', () => toggle('/toggleBuzzer'));
document.getElementById('btnToggleLed').addEventListener('change', () => toggle('/toggleLed'));
document.getElementById('btnToggleLedAzul').addEventListener('change', () => toggle('/toggleLedAzul'));

// Função para buscar e exibir o histórico
async function atualizarHistorico() {
  try {
    const response = await fetch('/historico');
    const historico = await response.text();
    const tbody = document.getElementById('history-data');
    if (tbody) {
      tbody.innerHTML = historico.split('\n').map(linha => 
        `<tr><td>${linha}</td></tr>`
      ).join('');
    }
  } catch (e) {
    console.error('Erro ao buscar histórico:', e);
  }
}

// Atualiza dados e histórico periodicamente
fetchDados();
atualizarHistorico();
setInterval(fetchDados, 2000);
setInterval(atualizarHistorico, 5000);
async function fetchLogs() {
  try {
    // Busca os logs do servidor
    const res = await fetch('/logs');
    const logs = await res.json();
    
    // Limpa a tabela de histórico
    const tbody = document.getElementById('history-data');
    tbody.innerHTML = '';
    
    // Adiciona cada log na tabela
    for (const log of logs) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.textContent = log.mensagem;  // Usa a mensagem formatada que já inclui data/hora
      tr.appendChild(td);
      tbody.appendChild(tr);
    }
  } catch (e) {
    console.error('Erro ao buscar logs:', e);
  }
}

// Configura os intervalos de atualização
setInterval(fetchDados, 2000);    // Atualiza dados dos sensores a cada 2 segundos
setInterval(fetchLogs, 5000);     // Atualiza logs a cada 5 segundos

// Faz a primeira busca de dados
fetchDados();
fetchLogs();