// WebSocket Variablen
let socket;
let isConnected = false;
const RECONNECT_INTERVAL = 5000;
const HEARTBEAT_INTERVAL = 10000;
let heartbeatTimer = null;
let lastHeartbeatResponse = Date.now();
const HEARTBEAT_TIMEOUT = 20000;
let reconnectTimer = null;

// WebSocket Funktionen
function startHeartbeat() {
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    
    heartbeatTimer = setInterval(() => {
        // Prüfe ob zu lange keine Antwort kam
        if (Date.now() - lastHeartbeatResponse > HEARTBEAT_TIMEOUT) {
            isConnected = false;
            updateConnectionStatus();
            if (socket) {
                socket.close();
                socket = null;
            }
            return;
        }

        if (!socket || socket.readyState !== WebSocket.OPEN) {
            isConnected = false;
            updateConnectionStatus();
            return;
        }
        
        try {
            // Sende Heartbeat
            socket.send(JSON.stringify({ type: 'heartbeat' }));
        } catch (error) {
            isConnected = false;
            updateConnectionStatus();
            if (socket) {
                socket.close();
                socket = null;
            }
        }
    }, HEARTBEAT_INTERVAL);
}

function initWebSocket() {
    // Clear any existing reconnect timer
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    // Wenn eine existierende Verbindung besteht, diese erst schließen
    if (socket) {
        socket.close();
        socket = null;
    }

    try {
        socket = new WebSocket('ws://' + window.location.host + '/ws');
        
        socket.onopen = function() {
            isConnected = true;
            updateConnectionStatus();
            startHeartbeat(); // Starte Heartbeat nach erfolgreicher Verbindung
        };
        
        socket.onclose = function() {
            isConnected = false;
            updateConnectionStatus();
            if (heartbeatTimer) clearInterval(heartbeatTimer);
            
            // Nur neue Verbindung versuchen, wenn kein Timer läuft
            if (!reconnectTimer) {
                reconnectTimer = setTimeout(() => {
                    initWebSocket();
                }, RECONNECT_INTERVAL);
            }
        };
        
        socket.onerror = function(error) {
            isConnected = false;
            updateConnectionStatus();
            if (heartbeatTimer) clearInterval(heartbeatTimer);
            
            // Bei Fehler Verbindung schließen und neu aufbauen
            if (socket) {
                socket.close();
                socket = null;
            }
        };
        
        socket.onmessage = function(event) {
            lastHeartbeatResponse = Date.now(); // Aktualisiere Zeitstempel bei jeder Server-Antwort
            
            const data = JSON.parse(event.data);
            if (data.type === 'amsData') {
                displayAmsData(data.payload);
            } else if (data.type === 'nfcTag') {
                updateNfcStatusIndicator(data.payload);
            } else if (data.type === 'nfcData') {
                updateNfcData(data.payload);
            } else if (data.type === 'writeNfcTag') {
                handleWriteNfcTagResponse(data.success);
            } else if (data.type === 'heartbeat') {
                // Optional: Spezifische Behandlung von Heartbeat-Antworten
                // Update status dots
                const bambuDot = document.getElementById('bambuDot');
                const spoolmanDot = document.getElementById('spoolmanDot');
                const ramStatus = document.getElementById('ramStatus');
                
                if (bambuDot) {
                    bambuDot.className = 'status-dot ' + (data.bambu_connected ? 'online' : 'offline');
                    // Add click handler only when offline
                    if (!data.bambu_connected) {
                        bambuDot.style.cursor = 'pointer';
                        bambuDot.onclick = function() {
                            if (socket && socket.readyState === WebSocket.OPEN) {
                                socket.send(JSON.stringify({
                                    type: 'reconnect',
                                    payload: 'bambu'
                                }));
                            }
                        };
                    } else {
                        bambuDot.style.cursor = 'default';
                        bambuDot.onclick = null;
                    }
                }
                if (spoolmanDot) {
                    spoolmanDot.className = 'status-dot ' + (data.spoolman_connected ? 'online' : 'offline');
                    // Add click handler only when offline
                    if (!data.spoolman_connected) {
                        spoolmanDot.style.cursor = 'pointer';
                        spoolmanDot.onclick = function() {
                            if (socket && socket.readyState === WebSocket.OPEN) {
                                socket.send(JSON.stringify({
                                    type: 'reconnect',
                                    payload: 'spoolman'
                                }));
                            }
                        };
                    } else {
                        spoolmanDot.style.cursor = 'default';
                        spoolmanDot.onclick = null;
                    }
                }
                if (ramStatus) {
                    ramStatus.textContent = `${data.freeHeap}k`;
                }
            }
        };
    } catch (error) {
        isConnected = false;
        updateConnectionStatus();
        
        // Nur neue Verbindung versuchen, wenn kein Timer läuft
        if (!reconnectTimer) {
            reconnectTimer = setTimeout(() => {
                initWebSocket();
            }, RECONNECT_INTERVAL);
        }
    }
}

function updateConnectionStatus() {
    const statusElement = document.querySelector('.connection-status');
    if (!isConnected) {
        statusElement.classList.remove('hidden');
        // Verzögerung hinzufügen, damit die CSS-Transition wirken kann
        setTimeout(() => {
            statusElement.classList.add('visible');
        }, 10);
    } else {
        statusElement.classList.remove('visible');
        // Warte auf das Ende der Fade-out Animation bevor wir hidden setzen
        setTimeout(() => {
            statusElement.classList.add('hidden');
        }, 300);
    }
}

// Event Listeners
document.addEventListener("DOMContentLoaded", function() {
    initWebSocket();
    
    // Event Listener für Checkbox
    document.getElementById("onlyWithoutSmId").addEventListener("change", function() {
        const spoolsData = window.getSpoolData();
        window.populateVendorDropdown(spoolsData);
    });
});

// Event Listener für Spoolman Events
document.addEventListener('spoolDataLoaded', function(event) {
    window.populateVendorDropdown(event.detail);
});

document.addEventListener('spoolmanError', function(event) {
    showNotification(`Spoolman Error: ${event.detail.message}`, false);
});

document.addEventListener('filamentSelected', function(event) {
    updateNfcInfo();
    // Zeige Spool-Buttons wenn ein Filament ausgewählt wurde
    const selectedText = document.getElementById("selected-filament").textContent;
    updateSpoolButtons(selectedText !== "Please choose...");
});

// Hilfsfunktion für kontrastreiche Textfarbe
function getContrastColor(hexcolor) {
    // Konvertiere Hex zu RGB
    const r = parseInt(hexcolor.substr(0,2),16);
    const g = parseInt(hexcolor.substr(2,2),16);
    const b = parseInt(hexcolor.substr(4,2),16);
    
    // Berechne Helligkeit (YIQ Formel)
    const yiq = ((r*299)+(g*587)+(b*114))/1000;
    
    // Return schwarz oder weiß basierend auf Helligkeit
    return (yiq >= 128) ? '#000000' : '#FFFFFF';
}

function updateNfcInfo() {
    const selectedText = document.getElementById("selected-filament").textContent;
    const nfcInfo = document.getElementById("nfcInfo");
    const writeButton = document.getElementById("writeNfcButton");

    if (selectedText === "Please choose...") {
        nfcInfo.textContent = "No Filament selected";
        nfcInfo.classList.remove("nfc-success", "nfc-error");
        writeButton.classList.add("hidden");
        return;
    }

    // Finde die ausgewählte Spule in den Daten
    const selectedSpool = spoolsData.find(spool => 
        `${spool.id} | ${spool.filament.name} (${spool.filament.material})` === selectedText
    );

    if (selectedSpool && selectedSpool.extra.nfc_id) {
        nfcInfo.textContent = "NFC Tag assigned";
        nfcInfo.classList.add("nfc-success");
        nfcInfo.classList.remove("nfc-error");
    } else {
        nfcInfo.textContent = "No NFC-Tag assigned";
        nfcInfo.classList.add("nfc-error");
        nfcInfo.classList.remove("nfc-success");
    }

    if (selectedSpool) {
        writeButton.classList.remove("hidden");
    } else {
        writeButton.classList.add("hidden");
    }
}

function displayAmsData(amsData) {
    const amsDataContainer = document.getElementById('amsData');
    amsDataContainer.innerHTML = ''; 

    amsData.forEach((ams) => {
        // Bestimme den Anzeigenamen für das AMS
        const amsDisplayName = ams.ams_id === 255 ? 'External Spool' : `AMS ${ams.ams_id}`;
        
        const trayHTML = ams.tray.map(tray => {
            // Prüfe ob überhaupt Daten vorhanden sind
            const relevantFields = ['tray_type', 'tray_sub_brands', 'tray_info_idx', 'setting_id'];
            const hasAnyContent = relevantFields.some(field => 
                tray[field] !== null && 
                tray[field] !== undefined && 
                tray[field] !== '' &&
                tray[field] !== 'null'
            );

            // Bestimme den Anzeigenamen für das Tray
            const trayDisplayName = (ams.ams_id === 255) ? 'External' : `Tray ${tray.id}`;

            // Nur für nicht-leere Trays den Button-HTML erstellen
            const buttonHtml = `
                <button class="spool-button" onclick="handleSpoolIn(${ams.ams_id}, ${tray.id})" 
                        style="position: absolute; top: -30px; left: -15px; 
                               background: none; border: none; padding: 0; 
                               cursor: pointer; display: none;">
                    <img src="spool_in.png" alt="Spool In" style="width: 48px; height: 48px;">
                </button>`;
            
                        // Nur für nicht-leere Trays den Button-HTML erstellen
            const outButtonHtml = `
                <button class="spool-button" onclick="handleSpoolOut()" 
                        style="position: absolute; top: -35px; right: -15px; 
                               background: none; border: none; padding: 0; 
                               cursor: pointer; display: block;">
                    <img src="spool_in.png" alt="Spool In" style="width: 48px; height: 48px; transform: rotate(180deg) scaleX(-1);">
                </button>`;

            if (!hasAnyContent) {
                return `
                    <div class="tray">
                        <p class="tray-head">${trayDisplayName}</p>
                        <p>
                            ${(ams.ams_id === 255 && tray.tray_type === '') ? buttonHtml : ''}
                            Empty
                        </p>
                    </div>
                    <hr>`;
            }

            // Generiere den Type mit Color-Box zusammen
            const typeWithColor = tray.tray_type ? 
                `<p>Typ: ${tray.tray_type} ${tray.tray_color ? `<span style="
                    background-color: #${tray.tray_color}; 
                    width: 20px; 
                    height: 20px; 
                    display: inline-block; 
                    vertical-align: middle;
                    border: 1px solid #333;
                    border-radius: 3px;
                    margin-left: 5px;"></span>` : ''}</p>` : '';

            // Array mit restlichen Tray-Eigenschaften
            const trayProperties = [
                { key: 'tray_sub_brands', label: 'Sub Brands' },
                { key: 'tray_info_idx', label: 'Filament IDX' },
                { key: 'setting_id', label: 'Setting ID' },
                { key: 'cali_idx', label: 'Calibration IDX' }  // Add new property
            ];

            // Nur gültige Felder anzeigen
            const trayDetails = trayProperties
                .filter(prop => 
                    tray[prop.key] !== null && 
                    tray[prop.key] !== undefined && 
                    tray[prop.key] !== '' &&
                    tray[prop.key] !== 'null'
                )
                .map(prop => {
                    // Spezielle Behandlung für setting_id
                    if (prop.key === 'cali_idx' && tray[prop.key] === '-1') {
                        return `<p>${prop.label}: not calibrated</p>`;
                    }
                    return `<p>${prop.label}: ${tray[prop.key]}</p>`;
                })
                .join('');

            // Temperaturen nur anzeigen, wenn beide nicht 0 sind
            const tempHTML = (tray.nozzle_temp_min > 0 && tray.nozzle_temp_max > 0) 
                ? `<p>Nozzle Temp: ${tray.nozzle_temp_min}°C - ${tray.nozzle_temp_max}°C</p>`
                : '';

            return `
                <div class="tray" ${tray.tray_color ? `style="border-left: 4px solid #${tray.tray_color};"` : 'style="border-left: 4px solid #007bff;"'}>
                    <div style="position: relative;">
                        ${buttonHtml}
                        <p class="tray-head">${trayDisplayName}</p>
                        ${typeWithColor}
                        ${trayDetails}
                        ${tempHTML}
                        ${(ams.ams_id === 255 && tray.tray_type !== '') ? outButtonHtml : ''}
                    </div>
                    
                </div>`;
        }).join('');

        const amsInfo = `
            <div class="feature">
                <h3>${amsDisplayName}:</h3>
                <div id="trayContainer">
                    ${trayHTML}
                </div>
            </div>`;
        
        amsDataContainer.innerHTML += amsInfo;
    });
}

// Neue Funktion zum Anzeigen/Ausblenden der Spool-Buttons
function updateSpoolButtons(show) {
    const spoolButtons = document.querySelectorAll('.spool-button');
    spoolButtons.forEach(button => {
        button.style.display = show ? 'block' : 'none';
    });
}

function handleSpoolOut() {
    // Erstelle Payload
    const payload = {
        type: 'setBambuSpool',
        payload: {
            amsId: 255,
            trayId: 254,
            color: "FFFFFF",
            nozzle_temp_min: 0,
            nozzle_temp_max: 0,
            type: "",
            brand: ""
        }
    };

    try {
        socket.send(JSON.stringify(payload));
        showNotification(`External Spool removed. Pls wait`, true);
    } catch (error) {
        console.error("Fehler beim Senden der WebSocket Nachricht:", error);
        showNotification("Error while sending!", false);
    }
}

// Neue Funktion zum Behandeln des Spool-In-Klicks
function handleSpoolIn(amsId, trayId) {
    // Prüfe WebSocket Verbindung zuerst
    if (!socket || socket.readyState !== WebSocket.OPEN) {
        showNotification("No active WebSocket connection!", false);
        console.error("WebSocket not connected");
        return;
    }

    // Hole das ausgewählte Filament
    const selectedText = document.getElementById("selected-filament").textContent;
    if (selectedText === "Please choose...") {
        showNotification("Choose Filament first", false);
        return;
    }

    // Finde die ausgewählte Spule in den Daten
    const selectedSpool = spoolsData.find(spool => 
        `${spool.id} | ${spool.filament.name} (${spool.filament.material})` === selectedText
    );

    if (!selectedSpool) {
        showNotification("Selected Spool not found", false);
        return;
    }

    // Temperaturwerte extrahieren
    let minTemp = "175";
    let maxTemp = "275";

    if (Array.isArray(selectedSpool.filament.nozzle_temperature) && 
        selectedSpool.filament.nozzle_temperature.length >= 2) {
        minTemp = selectedSpool.filament.nozzle_temperature[0];
        maxTemp = selectedSpool.filament.nozzle_temperature[1];
    }

    // Erstelle Payload
    const payload = {
        type: 'setBambuSpool',
        payload: {
            amsId: amsId,
            trayId: trayId,
            color: selectedSpool.filament.color_hex || "FFFFFF",
            nozzle_temp_min: parseInt(minTemp),
            nozzle_temp_max: parseInt(maxTemp),
            type: selectedSpool.filament.material,
            brand: selectedSpool.filament.vendor.name,
            tray_info_idx: selectedSpool.filament.extra.bambu_idx.replace(/['"]+/g, '').trim()
        }
    };

    // Prüfe, ob der Key bambu_setting_id vorhanden ist
    if (selectedSpool.filament.extra.bambu_setting_id) {
        payload.payload.cali_idx = selectedSpool.filament.extra.bambu_setting_id.replace(/['"]+/g, '').trim();
    } else {
        payload.payload.cali_idx = "-1";
    }

    try {
        socket.send(JSON.stringify(payload));
        showNotification(`Spool set in AMS ${amsId} Tray ${trayId}. Pls wait`, true);
    } catch (error) {
        console.error("Fehler beim Senden der WebSocket Nachricht:", error);
        showNotification("Error while sending", false);
    }
}

function updateNfcStatusIndicator(data) {
    const indicator = document.getElementById('nfcStatusIndicator');
    
    if (data.found === 0) {
        // Kein NFC Tag gefunden
        indicator.className = 'status-circle';
    } else if (data.found === 1) {
        // NFC Tag erfolgreich gelesen
        indicator.className = 'status-circle success';
    } else {
        // Fehler beim Lesen
        indicator.className = 'status-circle error';
    }
}

function updateNfcData(data) {
    // Den Container für den NFC Status finden
    const nfcStatusContainer = document.querySelector('.nfc-status-display');
    
    // Bestehende Daten-Anzeige entfernen falls vorhanden
    const existingData = nfcStatusContainer.querySelector('.nfc-data');
    if (existingData) {
        existingData.remove();
    }

    // Neues div für die Datenanzeige erstellen
    const nfcDataDiv = document.createElement('div');
    nfcDataDiv.className = 'nfc-data';

    // Wenn ein Fehler vorliegt oder keine Daten vorhanden sind
    if (data.error || data.info || !data || Object.keys(data).length === 0) {
        // Zeige Fehlermeldung oder leere Nachricht
        if (data.error || data.info) {
            if (data.error) {
                nfcDataDiv.innerHTML = `
                    <div class="error-message" style="margin-top: 10px; color: #dc3545;">
                        <p><strong>Error:</strong> ${data.error}</p>
                    </div>`;
            } else {
                nfcDataDiv.innerHTML = `
                    <div class="info-message" style="margin-top: 10px; color:rgb(18, 210, 0);">
                        <p><strong>Info:</strong> ${data.info}</p>
                    </div>`;
            }

        } else {
            nfcDataDiv.innerHTML = '<div style="margin-top: 10px;"></div>';
        }
        nfcStatusContainer.appendChild(nfcDataDiv);
        return;
    }

    // HTML für die Datenanzeige erstellen
    let html = `
        <div class="nfc-card-data" style="margin-top: 10px;">
            <p><strong>Brand:</strong> ${data.brand || 'N/A'}</p>
            <p><strong>Type:</strong> ${data.type || 'N/A'} ${data.color_hex ? `<span style="
                background-color: #${data.color_hex}; 
                width: 20px; 
                height: 20px; 
                display: inline-block; 
                vertical-align: middle;
                border: 1px solid #333;
                border-radius: 3px;
                margin-left: 5px;
            "></span>` : ''}</p>
    `;

    // Spoolman ID anzeigen
    html += `<p><strong>Spoolman ID:</strong> ${data.sm_id || 'No Spoolman ID'}</p>`;

    // Nur wenn eine sm_id vorhanden ist, aktualisiere die Dropdowns
    if (data.sm_id) {
        const matchingSpool = spoolsData.find(spool => spool.id === parseInt(data.sm_id));
        if (matchingSpool) {
            // Zuerst Hersteller-Dropdown aktualisieren
            document.getElementById("vendorSelect").value = matchingSpool.filament.vendor.id;
            
            // Dann Filament-Dropdown aktualisieren und Spule auswählen
            updateFilamentDropdown();
            setTimeout(() => {
                // Warte kurz bis das Dropdown aktualisiert wurde
                selectFilament(matchingSpool);
            }, 100);
        }
    }

    html += '</div>';
    nfcDataDiv.innerHTML = html;

    
    // Neues div zum Container hinzufügen
    nfcStatusContainer.appendChild(nfcDataDiv);
}

function writeNfcTag() {
    const selectedText = document.getElementById("selected-filament").textContent;
    if (selectedText === "Please choose...") {
        alert('Please select a Spool first.');
        return;
    }

    const spoolsData = window.getSpoolData();
    const selectedSpool = spoolsData.find(spool => 
        `${spool.id} | ${spool.filament.name} (${spool.filament.material})` === selectedText
    );

    if (!selectedSpool) {
        alert('Ausgewählte Spule konnte nicht gefunden werden.');
        return;
    }

    // Temperaturwerte korrekt extrahieren
    let minTemp = "175";
    let maxTemp = "275";
    
    if (Array.isArray(selectedSpool.filament.nozzle_temperature) && 
        selectedSpool.filament.nozzle_temperature.length >= 2) {
        minTemp = String(selectedSpool.filament.nozzle_temperature[0]);
        maxTemp = String(selectedSpool.filament.nozzle_temperature[1]);
    }

    // Erstelle das NFC-Datenpaket mit korrekten Datentypen
    const nfcData = {
        version: "2.0",
        protocol: "openspool",
        color_hex: selectedSpool.filament.color_hex || "FFFFFF",
        type: selectedSpool.filament.material,
        min_temp: minTemp,
        max_temp: maxTemp,
        brand: selectedSpool.filament.vendor.name,
        sm_id: String(selectedSpool.id) // Konvertiere zu String
    };

    if (socket?.readyState === WebSocket.OPEN) {
        const writeButton = document.getElementById("writeNfcButton");
        writeButton.classList.add("writing");
        writeButton.textContent = "Writing";
        socket.send(JSON.stringify({
            type: 'writeNfcTag',
            payload: nfcData
        }));
    } else {
        alert('Not connected to Server. Please check connection.');
    }
}

function handleWriteNfcTagResponse(success) {
    const writeButton = document.getElementById("writeNfcButton");
    writeButton.classList.remove("writing");
    writeButton.classList.add(success ? "success" : "error");
    writeButton.textContent = success ? "Write success" : "Write failed";

    setTimeout(() => {
        writeButton.classList.remove("success", "error");
        writeButton.textContent = "Write Tag";
    }, 5000);
}

function showNotification(message, isSuccess) {
    const notification = document.createElement('div');
    notification.className = `notification ${isSuccess ? 'success' : 'error'}`;
    notification.textContent = message;
    document.body.appendChild(notification);

    // Nach 3 Sekunden ausblenden
    setTimeout(() => {
        notification.classList.add('fade-out');
        setTimeout(() => {
            notification.remove();
        }, 300);
    }, 3000);
}
