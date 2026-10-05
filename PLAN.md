# ESP32-S3 Touch LCD 4B Dashboard — implementační plán

## Cíl

Vytvořit dotykový dashboard pro Waveshare ESP32-S3-Touch-LCD-4B (480 × 480,
16 MB flash, 8 MB PSRAM), který lze přímo na zařízení připojit k 2,4GHz Wi-Fi
a následně zobrazuje:

- čas ve zvoleném časovém pásmu (výchozí `Europe/Prague`),
- aktuální počasí a předpověď pro zvolené místo (výchozí Praha),
- aktuální cenu BTC v USD, EUR nebo CZK a historický graf,
- zbývající Claude limit v pětihodinovém a týdenním okně,
- stav připojení a stáří naposledy získaných dat.

## Technická rozhodnutí

- Framework: ESP-IDF 5.3+.
- Hardware: oficiální komponenta `waveshare/esp32_s3_touch_lcd_4b`.
- UI: LVGL 9, rozlišení 480 × 480, velké dotykové prvky.
- Konfigurace: NVS; heslo k Wi-Fi nebude součástí zdrojového kódu.
- Síť: HTTPS s ověřením serverového certifikátu.
- Počasí: Open-Meteo Forecast API a Geocoding API.
- BTC: veřejné Coinbase ticker a candles endpointy; volba USD, EUR nebo CZK.
- Čas: SNTP, lokální POSIX TZ pravidla, výchozí Praha; RTC jako pozdější
  doplněk pro kvalitní offline provoz.
- Architektura: síťové požadavky poběží mimo LVGL task a UI se bude měnit
  pouze přes bezpečně předané události.

## Obrazovky

1. Start a stav inicializace.
2. Průvodce Wi-Fi:
   - seznam nalezených sítí,
   - zadání hesla dotykovou klávesnicí,
   - připojování, úspěch a srozumitelná chyba.
3. Úvodní nastavení:
   - vyhledání místa,
   - výběr časového pásma.
4. Hlavní dashboard:
   - datum a čas,
   - aktuální počasí,
   - BTC ve zvolené měně, změna a svíčkový graf,
   - indikace Wi-Fi a poslední aktualizace.
5. Detail počasí s pětidenní předpovědí; hodinová předpověď je pozdější rozšíření.
6. Detail BTC s volbou období grafu.
7. Nastavení Wi-Fi, místa, pásma, jasu a intervalů aktualizace.
8. Detail Claude limitů s lokálním, časově omezeným nastavením tokenu.
9. Offline a chybové stavy se zachováním posledních platných dat.

## Etapy a ověření

### 1. Základ projektu a bring-up hardwaru

- [x] Inicializovat ESP-IDF projekt a závislosti pro přesnou variantu 4B.
- [x] Nastavit ESP32-S3, 16MB flash, octal PSRAM a partition table.
- [x] Sestavit minimální firmware.
- [x] Ověřit displej, podsvícení, rozlišení a dotyk v boot logu.
- [x] Před prvním flashováním uložit zálohu továrního firmwaru.

### 2. UI kostra

- [x] Definovat motiv, typografii a komponenty.
- [x] Doplnit český font, diakritiku a českou klávesnici pro názvy míst.
- [x] Implementovat hlavní dashboard se zástupnými daty.
- [x] Implementovat obrazovky Wi-Fi, místa, pásma a nastavení.
- [x] Doplnit samostatnou obrazovku pětidenní předpovědi počasí.
- [ ] Doplnit samostatnou detailní obrazovku BTC.
- [ ] Ověřit velikosti dotykových ploch a plynulost překreslování.

### 3. Wi-Fi a konfigurace

- [x] Skenovat 2,4GHz sítě a zobrazit sílu signálu a zabezpečení.
- [x] Přidat dotykovou klávesnici a bezpečné zadání hesla.
- [x] Ukládat konfiguraci do NVS až po úspěšném připojení a automaticky se znovu připojovat.
- [x] Přidat možnost změnit nebo zapomenout síť.
- [x] Přidat timeouty, retry a offline stav.

### 4. Čas, počasí a místo

- [x] Synchronizovat UTC přes SNTP.
- [x] Implementovat předvolby časových pásem a automatický letní čas.
- [x] Vyhledat místo přes geocoding a uložit souřadnice.
- [x] Načíst a parsovat current/daily data Open-Meteo.
- [x] Přidat české textové popisy WMO stavů počasí.
- [x] Doplnit ikony pro hlavní WMO stavy počasí.
- [x] Zobrazit pětidenní předpověď s maximem, minimem a pravděpodobností deště.
- [ ] Doplnit hodinovou předpověď a hardwarové RTC.

### 5. BTC

- [x] Načíst ticker BTC/USD.
- [x] Načíst hodinové OHLC body pro 24hodinový graf.
- [x] Vykreslit adaptivní svíčkový graf a 24hodinovou změnu.
- [x] Doplnit volbu měny USD, EUR nebo CZK a uložit ji do NVS.
- [x] Omezit frekvenci dotazů a zachovat poslední platná data v RAM.
- [ ] Doplnit přepínání období 1 h, 7 d a 30 d.

### 6. Odolnost a dokončení

- [x] Oddělit UI, síť, úložiště a datové modely.
- [x] Ošetřit neplatný JSON, TLS/DNS chyby, výpadek Wi-Fi a timeouty.
- [x] Přidat watchdog-friendly běh bez blokování UI.
- [x] Zkontrolovat spotřebu RAM/PSRAM a velikost firmwaru.
- [x] Přidat stručný README s build/upload postupem.
- [x] Sestavit, nahrát a ověřit boot, PSRAM, LCD a GT911 na připojeném zařízení.
- [ ] Dokončit funkční test po zadání skutečných Wi-Fi údajů uživatelem.

### 7. Claude limity

- [x] Přidat kompaktní ukazatele zbývajících 5h a 7d limitů do horní lišty.
- [x] Přidat detail s procenty, barevnými pruhy a odpočtem do obnovy.
- [x] Načítat data přímo z ESP32 přes ověřené HTTPS spojení s Anthropicem.
- [x] Přidat lokální stránku pro vložení `claude setup-token`, otevřenou pouze
  deset minut po povolení na displeji; token nikdy nezobrazovat zpět.
- [x] Automaticky obnovovat po patnácti minutách a přidat ruční obnovení z detailu.
- [ ] Ověřit skutečnou odpověď účtu po vložení uživatelova setup tokenu.

## Kritéria hotového MVP

- Po čistém startu lze na displeji vybrat Wi-Fi a zadat heslo.
- Po restartu se zařízení připojí bez nového zadání.
- Dashboard ukazuje správný pražský čas, počasí pro Prahu a BTC/USD.
- Místo a časové pásmo lze změnit v nastavení.
- Při výpadku internetu UI nezamrzne a označí data jako neaktuální.
- Firmware se reprodukovatelně sestaví a funguje na fyzické desce.

## Mimo první verzi

- obchodování nebo ukládání burzovních API klíčů,
- 5GHz Wi-Fi a podnikové 802.1X sítě,
- přihlašování do veřejných captive portal sítí,
- mobilní aplikace,
- OTA aktualizace (architektura s nimi ale bude počítat).
