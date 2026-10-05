# Krabička dashboard

Dotykový informační dashboard pro
[Waveshare ESP32-S3-Touch-LCD-4B](https://rpishop.cz/642107/waveshare-vyvojova-deska-esp32-s3-s-4-lcd-kapacitnim-displejem/)
s kapacitním displejem 480 × 480, 16 MB flash a 8 MB octal PSRAM. Zařízení se
ovládá přímo dotykem a po prvním nastavení funguje samostatně bez počítače.

## Funkce

- průvodce připojením k 2,4GHz Wi-Fi a česká dotyková klávesnice,
- automatické opětovné připojení a uložení nastavení do NVS,
- čas synchronizovaný přes SNTP, české datum a volba časového pásma,
- aktuální počasí a pětidenní výhled pro zvolené město,
- BTC v USD, EUR nebo CZK včetně 24hodinové změny a hodinových svíček,
- zbývající Claude limit pro 5hodinové a týdenní okno,
- nastavení Wi-Fi, města, časového pásma, měny a jasu,
- zachování posledních platných dat při dočasném výpadku služby,
- ruční i automatická aktualizace bez blokování LVGL rozhraní.

Počasí, předpověď, BTC a Claude limity se automaticky načítají každých
15 minut. Klepnutí na dashboard nebo datovou kartu spustí společnou ruční
aktualizaci. Modrý stav **Aktualizuji data…** se zobrazuje pouze během
skutečných síťových požadavků.

## Hardware a software

- Waveshare ESP32-S3-Touch-LCD-4B / ESP32-S3-WROOM-1-N16R8,
- ESP-IDF 6.0.1 přes PlatformIO,
- `espressif32@7.0.1`,
- LVGL 9.5,
- Waveshare BSP 2.0.0,
- displej ST7701 a dotykový řadič GT911.

Přesné verze spravovaných komponent jsou uzamčené v `dependencies.lock`.

## Sestavení

### Požadavky

- PlatformIO Core 6.2 nebo novější,
- datový USB kabel,
- macOS, Linux nebo Windows s dostupným sériovým portem.

Po naklonování repozitáře spusťte v jeho kořenovém adresáři:

```sh
pio run
```

První sestavení stáhne ESP-IDF toolchain a komponenty do lokálního adresáře
`managed_components/`. Tento adresář se negituje; jeho obsah lze kdykoliv
obnovit z `main/idf_component.yml` a `dependencies.lock`.

## Nahrání firmwaru

Připojte nativní USB port ESP32-S3 a zobrazte dostupná zařízení:

```sh
pio device list
```

Firmware lze nahrát automaticky:

```sh
pio run --target upload
```

Pokud PlatformIO vybere nesprávný port, zadejte jej explicitně:

```sh
pio run --target upload --upload-port /dev/cu.usbmodemXXXX
```

Na Linuxu bude port obvykle `/dev/ttyACM0`, na Windows například `COM5`.
Sériový výpis lze otevřít příkazem:

```sh
pio device monitor
```

## První spuštění

1. Na úvodní obrazovce klepněte na **Připojit k Wi-Fi**.
2. Vyberte 2,4GHz síť, zadejte heslo a potvrďte klávesou OK.
3. Po synchronizaci času se načte počasí pro Prahu a BTC v USD.
4. Ozubené kolečko otevře změnu sítě, města, časového pásma, měny a jasu.
5. **Výhled na 5 dní** otevře podrobnou předpověď.

Výchozí nastavení lze později změnit přímo na displeji. Zařízení nepodporuje
5GHz-only sítě, podnikové 802.1X sítě ani captive portály vyžadující přihlášení
ve webovém prohlížeči.

## Claude limity

Integrace zobrazuje zbývající kapacitu 5hodinového a týdenního okna, barevné
pruhy a odpočet k resetu. Nastavení probíhá pouze v lokální síti:

1. Na počítači spusťte `claude setup-token`.
2. Na dashboardu klepněte na kartu **CLAUDE**, případně otevřete
   **Nastavení → Claude limity**.
3. Klepněte na **Povolit nastavení na 10 minut**.
4. V prohlížeči otevřete adresu zobrazenou na displeji.
5. Vložte setup token a formulář odešlete.

Po uložení se stránka automaticky zamkne. Token se nezobrazuje zpět v UI ani
se nevypisuje do logu. Integrace limitů používá rozhraní a quota hlavičky určené
pro Claude Code, proto ji považujte za experimentální — poskytovatel může jejich
chování změnit.

## Datové zdroje

| Data | Zdroj |
| --- | --- |
| Počasí a geocoding | Open-Meteo |
| BTC ticker a OHLC svíčky | Coinbase Exchange API |
| Čas | Cloudflare NTP a pool.ntp.org |
| Claude limity | Anthropic Messages API / Claude Code quota hlavičky |

Všechna vzdálená API jsou volána přes HTTPS s ověřením certifikátu pomocí
certifikačního balíku ESP-IDF.

## Bezpečnost a soukromí

Ve zdrojových souborech nejsou žádná hesla ani tokeny. Wi-Fi údaje a Claude
setup token se zadávají až na konkrétním zařízení a ukládají se do jeho NVS.

Současná verze má důležitá omezení:

- Wi-Fi heslo a Claude token nejsou v NVS aplikačně šifrované.
- Lokální stránka pro vložení tokenu používá HTTP, nikoliv HTTPS.
- Stránka je dostupná jen po ručním povolení na displeji a po 10 minutách nebo
  po uložení se znovu zamkne, přesto ji používejte výhradně na důvěryhodné síti.
- Před prodejem nebo předáním zařízení vymažte flash, aby v něm nezůstala
  uložená konfigurace.

Do veřejného repozitáře necommitujte obsah `.pio/`, `managed_components/`,
`backups/`, vygenerovaný `sdkconfig.*` ani binární firmware. Pravidla jsou
součástí `.gitignore`.

## Řešení problémů

### Zařízení se nezobrazuje jako sériový port

- použijte datový, nikoliv pouze nabíjecí USB kabel,
- zkuste jiný USB port,
- spusťte `pio device list`, protože název portu se může po resetu změnit,
- při potížích podržte BOOT, krátce stiskněte RESET a poté BOOT uvolněte.

### Wi-Fi se nepřipojí

- ověřte, že síť vysílá na 2,4 GHz,
- znovu zadejte heslo,
- veřejné sítě s captive portálem nejsou podporované.

### Některá data se nenačtou

- zelená síťová indikace potvrzuje fungující internetové spojení,
- klepnutím na dashboard spusťte ruční aktualizaci,
- zkontrolujte sériový log příkazem `pio device monitor`,
- jednotlivá veřejná API mohou mít dočasný výpadek nebo změnit odpověď.

## Struktura projektu

```text
main/
├── app_main.c       orchestrace úloh a aktualizací
├── app_ui.c         LVGL obrazovky a komponenty
├── app_network.c    Wi-Fi STA, skenování a reconnect
├── app_data.c       HTTPS, parsování API a datové zdroje
├── app_storage.c    konfigurace uložená v NVS
├── app_web.c        časově omezené lokální nastavení Claude tokenu
└── fonts/           české fonty a symboly počasí
```

Další důležité soubory:

- `platformio.ini` — PlatformIO prostředí,
- `sdkconfig.defaults` — reprodukovatelné výchozí nastavení ESP-IDF,
- `partitions.csv` — rozložení 16MB flash,
- `dependencies.lock` — uzamčené verze komponent,
- `PLAN.md` — implementační plán a roadmapa.

## Stav projektu

Projekt je funkční hobby firmware vyvíjený a testovaný na konkrétní desce
Waveshare ESP32-S3-Touch-LCD-4B. Před použitím v produkčním nebo nedůvěryhodném
síťovém prostředí je vhodné doplnit šifrování NVS, bezpečnější provisioning a
mechanismus podepsaných aktualizací.
