// Globale Variablen
let spoolmanUrl = '';
let spoolsData = [];

// Hilfsfunktionen für Datenmanipulation
function processSpoolData(data) {
    return data.map(spool => ({
        id: spool.id,
        remaining_weight: spool.remaining_weight,
        remaining_length: spool.remaining_length,
        filament: spool.filament,
        extra: spool.extra
    }));
}

// Dropdown-Funktionen
function populateVendorDropdown(data, selectedSmId = null) {
    const vendorSelect = document.getElementById("vendorSelect");
    if (!vendorSelect) {
        console.error('vendorSelect Element nicht gefunden');
        return;
    }
    const onlyWithoutSmId = document.getElementById("onlyWithoutSmId");
    if (!onlyWithoutSmId) {
        console.error('onlyWithoutSmId Element nicht gefunden');
        return;
    }

    // Separate Objekte für alle Hersteller und gefilterte Hersteller
    const allVendors = {};
    const filteredVendors = {};

    vendorSelect.innerHTML = '<option value="">Bitte wählen...</option>';

    let vendorIdToSelect = null;
    let totalSpools = 0;
    let spoolsWithoutTag = 0;
    let totalWeight = 0;
    let totalLength = 0;
    // Neues Objekt für Material-Gruppierung
    const materials = {};

    data.forEach(spool => {
        if (!spool.filament || !spool.filament.vendor) {
            return;
        }

        totalSpools++;
        
        // Material zählen und gruppieren
        if (spool.filament.material) {
            const material = spool.filament.material.toUpperCase(); // Normalisierung
            materials[material] = (materials[material] || 0) + 1;
        }

        // Addiere Gewicht und Länge
        if (spool.remaining_weight) {
            totalWeight += spool.remaining_weight;
        }
        if (spool.remaining_length) {
            totalLength += spool.remaining_length;
        }

        const vendor = spool.filament.vendor;
        
        const hasValidNfcId = spool.extra && 
                             spool.extra.nfc_id && 
                             spool.extra.nfc_id !== '""' && 
                             spool.extra.nfc_id !== '"\\"\\"\\""';
        
        if (!hasValidNfcId) {
            spoolsWithoutTag++;
        }

        // Alle Hersteller sammeln
        if (!allVendors[vendor.id]) {
            allVendors[vendor.id] = vendor.name;
        }

        // Gefilterte Hersteller für Dropdown
        if (!filteredVendors[vendor.id]) {
            if (!onlyWithoutSmId.checked || !hasValidNfcId) {
                filteredVendors[vendor.id] = vendor.name;
            }
        }
    });

    // Nach der Schleife: Formatierung der Gesamtlänge
    console.log("Total Lenght: ", totalLength);
    const formattedLength = totalLength > 1000 
        ? (totalLength / 1000).toFixed(2) + " km" 
        : totalLength.toFixed(2) + " m";

    // Formatierung des Gesamtgewichts (von g zu kg zu t)
    const weightInKg = totalWeight / 1000;  // erst in kg umrechnen
    const formattedWeight = weightInKg > 1000 
        ? (weightInKg / 1000).toFixed(2) + " t" 
        : weightInKg.toFixed(2) + " kg";

    // Dropdown mit gefilterten Herstellern befüllen - alphabetisch sortiert
    Object.entries(filteredVendors)
        .sort(([, nameA], [, nameB]) => nameA.localeCompare(nameB)) // Sort vendors alphabetically by name
        .forEach(([id, name]) => {
            const option = document.createElement("option");
            option.value = id;
            option.textContent = name;
            vendorSelect.appendChild(option);
        });

    document.getElementById("totalSpools").textContent = totalSpools;
    document.getElementById("spoolsWithoutTag").textContent = spoolsWithoutTag;
    // Zeige die Gesamtzahl aller Hersteller an
    document.getElementById("totalVendors").textContent = Object.keys(allVendors).length;
    
    // Neue Statistiken hinzufügen
    document.getElementById("totalWeight").textContent = formattedWeight;
    document.getElementById("totalLength").textContent = formattedLength;

    // Material-Statistiken zum DOM hinzufügen
    const materialsList = document.getElementById("materialsList");
    materialsList.innerHTML = '';
    Object.entries(materials)
        .sort(([,a], [,b]) => b - a) // Sortiere nach Anzahl absteigend
        .forEach(([material, count]) => {
            const li = document.createElement("li");
            li.textContent = `${material}: ${count} ${count === 1 ? 'Spool' : 'Spools'}`;
            materialsList.appendChild(li);
        });

    if (vendorIdToSelect) {
        vendorSelect.value = vendorIdToSelect;
        updateFilamentDropdown(selectedSmId);
    }
}

function updateFilamentDropdown(selectedSmId = null) {
    const vendorId = document.getElementById("vendorSelect").value;
    const dropdownContentInner = document.getElementById("filament-dropdown-content");
    const filamentSection = document.getElementById("filamentSection");
    const onlyWithoutSmId = document.getElementById("onlyWithoutSmId").checked;
    const selectedText = document.getElementById("selected-filament");
    const selectedColor = document.getElementById("selected-color");

    dropdownContentInner.innerHTML = '';
    selectedText.textContent = "Bitte wählen...";
    selectedColor.style.backgroundColor = '#FFFFFF';

    if (vendorId) {
        const filteredFilaments = spoolsData.filter(spool => {
            const hasValidNfcId = spool.extra && 
                                 spool.extra.nfc_id && 
                                 spool.extra.nfc_id !== '""' && 
                                 spool.extra.nfc_id !== '"\\"\\"\\""';
            
            return spool.filament.vendor.id == vendorId && 
                   (!onlyWithoutSmId || !hasValidNfcId);
        });

        filteredFilaments.forEach(spool => {
            const option = document.createElement("div");
            option.className = "dropdown-option";
            option.setAttribute("data-value", spool.filament.id);
            option.setAttribute("data-nfc-id", spool.extra.nfc_id || "");
            
            const colorHex = spool.filament.color_hex || 'FFFFFF';
            option.innerHTML = `
                <div class="option-color" style="background-color: #${colorHex}"></div>
                <span>${spool.id} | ${spool.filament.name} (${spool.filament.material})</span>
            `;
            
            option.onclick = () => selectFilament(spool);
            dropdownContentInner.appendChild(option);
        });

        filamentSection.classList.remove("hidden");
    } else {
        filamentSection.classList.add("hidden");
    }
}

function selectFilament(spool) {
    const selectedColor = document.getElementById("selected-color");
    const selectedText = document.getElementById("selected-filament");
    const dropdownContent = document.getElementById("filament-dropdown-content");
    
    selectedColor.style.backgroundColor = `#${spool.filament.color_hex || 'FFFFFF'}`;
    selectedText.textContent = `${spool.id} | ${spool.filament.name} (${spool.filament.material})`;
    dropdownContent.classList.remove("show");
    
    document.dispatchEvent(new CustomEvent('filamentSelected', { 
        detail: spool 
    }));
}

// Initialisierung und Event-Handler
async function initSpoolman() {
    try {
        const response = await fetch('/api/url');
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        
        const data = await response.json();
        if (!data.spoolman_url) {
            throw new Error('spoolman_url nicht in der Antwort gefunden');
        }
        
        spoolmanUrl = data.spoolman_url;
        
        const fetchedData = await fetchSpoolData();
        spoolsData = processSpoolData(fetchedData);
        
        document.dispatchEvent(new CustomEvent('spoolDataLoaded', { 
            detail: spoolsData 
        }));
    } catch (error) {
        console.error('Fehler beim Initialisieren von Spoolman:', error);
        document.dispatchEvent(new CustomEvent('spoolmanError', { 
            detail: { message: error.message } 
        }));
    }
}

async function fetchSpoolData() {
    try {
        if (!spoolmanUrl) {
            throw new Error('Spoolman URL ist nicht initialisiert');
        }
        
        const response = await fetch(`${spoolmanUrl}/api/v1/spool`);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        
        const data = await response.json();
        return data;
    } catch (error) {
        console.error('Fehler beim Abrufen der Spulen-Daten:', error);
        return [];
    }
}

/*
// Exportiere Funktionen
window.getSpoolData = () => spoolsData;
window.reloadSpoolData = initSpoolman;
window.populateVendorDropdown = populateVendorDropdown;
window.updateFilamentDropdown = updateFilamentDropdown;
window.toggleFilamentDropdown = () => {
    const content = document.getElementById("filament-dropdown-content");
    content.classList.toggle("show");
};
*/

// Event Listener
document.addEventListener('DOMContentLoaded', () => {
    initSpoolman();
    
    const vendorSelect = document.getElementById('vendorSelect');
    if (vendorSelect) {
        vendorSelect.addEventListener('change', () => updateFilamentDropdown());
    }
    
    const onlyWithoutSmId = document.getElementById('onlyWithoutSmId');
    if (onlyWithoutSmId) {
        onlyWithoutSmId.addEventListener('change', () => {
            populateVendorDropdown(spoolsData);
            updateFilamentDropdown();
        });
    }
    
    document.addEventListener('spoolDataLoaded', (event) => {
        populateVendorDropdown(event.detail);
    });
    
    window.onclick = function(event) {
        if (!event.target.closest('.custom-dropdown')) {
            const dropdowns = document.getElementsByClassName("dropdown-content");
            for (let dropdown of dropdowns) {
                dropdown.classList.remove("show");
            }
        }
    };

    const refreshButton = document.getElementById('refreshSpoolman');
    if (refreshButton) {
        refreshButton.addEventListener('click', async () => {
            try {
                refreshButton.disabled = true;
                refreshButton.textContent = 'Wird aktualisiert...';
                await initSpoolman();
                refreshButton.textContent = 'Refresh Spoolman';
            } finally {
                refreshButton.disabled = false;
            }
        });
    }
});

// Exportiere Funktionen
window.getSpoolData = () => spoolsData;
window.setSpoolData = (data) => { spoolsData = data; };
window.reloadSpoolData = initSpoolman;
window.populateVendorDropdown = populateVendorDropdown;
window.updateFilamentDropdown = updateFilamentDropdown;
window.toggleFilamentDropdown = () => {
    const content = document.getElementById("filament-dropdown-content");
    content.classList.toggle("show");
};

// Event Listener für Click außerhalb Dropdown
window.onclick = function(event) {
    if (!event.target.closest('.custom-dropdown')) {
        const dropdowns = document.getElementsByClassName("dropdown-content");
        for (let dropdown of dropdowns) {
            dropdown.classList.remove("show");
        }
    }
};