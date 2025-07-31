// Dashboard Library Functions

// Minimal dashboard-lib.js for dashboard event logic
// All functions are safe no-ops if not used, but can be expanded as needed

export function updateTunerVisibilityForModel(model) {
  const tuningDot = document.getElementById('tuning-dot');
  const swrDot = document.getElementById('swr-dot');
  const tuningLabel = tuningDot ? tuningDot.parentElement : null;
  const swrLabel = swrDot ? swrDot.parentElement : null;
  if (tuningDot) tuningDot.style.display = (model === '998') ? 'none' : '';
  if (swrDot) swrDot.style.display = (model === '998') ? 'none' : '';
  if (tuningLabel) tuningLabel.style.display = (model === '998') ? 'none' : '';
  if (swrLabel) swrLabel.style.display = (model === '998') ? 'none' : '';
  const autoBtn = document.getElementById('button-auto');
  if (autoBtn) autoBtn.style.display = (model === '998') ? 'none' : '';
  const rightChainBtn = document.getElementById('lup-ldn-link-btn');
  if (rightChainBtn) rightChainBtn.style.display = (model === '998') ? 'none' : '';
  const swrBeeperShortcut = document.getElementById('swr-beeper-shortcut');
  if (swrBeeperShortcut) swrBeeperShortcut.style.display = (model === '998') ? 'none' : '';
}

export function updateElementIfExists(id, value) {
  const el = document.getElementById(id);
  if (el) {
    el.textContent = value;
  }
}

export function updateSystemStatus(status) {
  // Example: Update system status indicator
  // For now, just log
  console.log('updateSystemStatus:', status);
}

export function updateDashboard(data) {
  // Example: Update dashboard UI from backend data
  // For now, just log
  console.log('updateDashboard:', data);
}
}

export function updateElementIfExists(selector, value) {
  const element = document.querySelector(selector);
  if (element) {
    element.textContent = value;
  }
}

export function updateSystemStatus(data) {
  const statusElements = document.querySelectorAll('.status-indicator');
  statusElements.forEach(indicator => {
    if (data.ota_active) {
      indicator.className = 'status-indicator status-warning';
    } else if (data.captive_portal_active) {
      indicator.className = 'status-indicator status-offline';
    } else {
      indicator.className = 'status-indicator status-online';
    }
  });
}

export function updateDashboard(data) {
  // --- Tuner Control Card: Update latch button states and labels ---
  const antBtn = document.getElementById('button-ant');
  if (antBtn) {
    const isAnt2 = (data.ant_state && data.ant_state.trim() === 'ANT 2');
    antBtn.classList.toggle('active', isAnt2);
    antBtn.textContent = isAnt2 ? 'ANT 2' : 'ANT 1';
  }
  const autoBtn = document.getElementById('button-auto');
  if (autoBtn) {
    const isAuto = (data.auto_state && data.auto_state.trim() === 'AUTO');
    autoBtn.classList.toggle('active', isAuto);
    autoBtn.textContent = isAuto ? 'AUTO' : 'SEMI';
  }
  // TUNING/SWR indicators
  const tuningDot = document.getElementById('tuning-dot');
  if (tuningDot) {
    tuningDot.className = 'indicator-dot tuning' + ((typeof data.tuning_active !== 'undefined' && data.tuning_active) ? '' : ' gray');
  }
  const swrDot = document.getElementById('swr-dot');
  if (swrDot) {
    swrDot.className = 'indicator-dot swr' + ((typeof data.swr_ok !== 'undefined' && data.swr_ok) ? '' : ' gray');
  }
  updateElementIfExists('#current-time', data.time || 'TIME_NOT_SET');
  updateElementIfExists('[data-metric="mem-total"]', `${data.mem_total || 0} KB`);
  updateElementIfExists('[data-metric="mem-used"]', `${data.mem_used || 0} KB`);
  updateElementIfExists('[data-metric="mem-free"]', `${data.mem_free || 0} KB`);
  updateElementIfExists('[data-metric="flash-total"]', `${data.flash_total || 0} KB`);
  updateElementIfExists('[data-metric="flash-used"]', `${data.flash_used || 0} KB`);
  updateElementIfExists('[data-metric="flash-free"]', `${data.flash_free || 0} KB`);
  let d = (typeof data.uptime_days === 'number') ? data.uptime_days : 0;
  let h = (typeof data.uptime_hours === 'number') ? data.uptime_hours : 0;
  let m = (typeof data.uptime_minutes === 'number') ? data.uptime_minutes : 0;
  let s = (typeof data.uptime_seconds === 'number') ? data.uptime_seconds : 0;
  let uptimeStr = '';
  if (d > 0) uptimeStr += d + ' days ';
  if (d > 0 || h > 0) uptimeStr += String(h).padStart(2, '0') + ':';
  uptimeStr += String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
  updateElementIfExists('[data-metric="uptime"]', uptimeStr);
  updateElementIfExists('[data-metric="chip-id"]', data.chip_id || 'Unknown');
  updateElementIfExists('[data-metric="chip-rev"]', data.chip_rev || 'Unknown');
  updateElementIfExists('[data-metric="cpu-freq"]', `${data.cpu_freq || 0} MHz`);
  updateElementIfExists('[data-metric="psram-size"]', `${data.psram_size || 0} KB`);
  updateElementIfExists('[data-metric="ant-state"]', data.ant_state || 'Unknown');
  updateElementIfExists('[data-metric="auto-state"]', data.auto_state || 'Unknown');
  updateElementIfExists('[data-metric="civ-baud"]', '19200');
  const devNumInput = document.querySelector('[data-metric="device-number"]');
  let devNum = 1;
  if (data.device_number) {
    devNum = parseInt(data.device_number);
  } else if (devNumInput && devNumInput.value) {
    devNum = parseInt(devNumInput.value);
  }
  if (isNaN(devNum) || devNum < 1) devNum = 1;
  if (devNum > 4) devNum = 4;
  if (devNumInput) devNumInput.value = devNum;
  let civHex = (0xB7 + devNum).toString(16).toUpperCase();
  if (civHex.length < 2) civHex = '0' + civHex;
  let civDisplay = '0x' + civHex;
  updateElementIfExists('[data-metric="civ-address"]', civDisplay);
  if (typeof data.civ_model !== 'undefined') {
    const civ991 = document.getElementById('civ-model-991');
    const civ998 = document.getElementById('civ-model-998');
    if (civ991 && civ998) {
      civ991.checked = (data.civ_model === '991-994');
      civ998.checked = (data.civ_model === '998');
      updateTunerVisibilityForModel(data.civ_model);
    }
  }
  updateElementIfExists('[data-metric="ip-address"]', data.ip || 'Unknown');
  let wsServerDisplay = data.websocket_port || 'Unknown';
  if (/^\d+$/.test(wsServerDisplay) && data.ip) {
    wsServerDisplay = data.ip + ':' + wsServerDisplay;
  }
  updateElementIfExists('[data-metric="websocket-port"]', wsServerDisplay);
  updateElementIfExists('[data-metric="udp-port"]', data.udp_port || 'Unknown');
  const remoteWsIndicator = document.getElementById('remote-ws-indicator');
  const remoteWsLabel = document.getElementById('remote-ws-label');
  if (typeof data.remote_ws_connected !== 'undefined') {
    if (remoteWsIndicator) remoteWsIndicator.className = 'status-indicator ' + (data.remote_ws_connected ? 'status-online' : 'status-offline');
    if (remoteWsLabel) {
      remoteWsLabel.textContent = data.remote_ws_connected ? 'Connected' : 'Disconnected';
      remoteWsLabel.style.color = data.remote_ws_connected ? '#4CAF50' : '#F44336';
    }
  } else {
    if (remoteWsIndicator) remoteWsIndicator.className = 'status-indicator status-offline';
    if (remoteWsLabel) {
      remoteWsLabel.textContent = 'Unknown';
      remoteWsLabel.style.color = '#888';
    }
  }
  const now = new Date().toLocaleTimeString();
  updateElementIfExists('.footer', `Dashboard updates in real-time via WebSocket. Last updated: ${now}`);
  updateSystemStatus(data);
}
}
