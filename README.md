# 80s LED Sign — Pico W + Arduino Micro

Modern styrning av en äldre röd LED-skylt med **90 × 7 pixlar** (630 LED-positioner), byggd av **18 paneler** i **sex grupper om tre paneler**. Originalets logikkretsar och LED-drivare används fortfarande; den ursprungliga NEC D8049C-styrningen har ersatts av två mikrokontroller.

**Status (november 2026):** Båda uppstädade Arduino-sketcherna har kompilerats, laddats upp och **testats fungerande tillsammans på den riktiga skylten**. Text kan skickas via REST-API och scrollar på displayen. Originalprocessorn och båda minneskretsarna är fysiskt borttagna; skylten fungerar även efter borttagningen.

## Arkitektur

```text
Wi-Fi / HTTP-klient (curl, Python, ...)
                |
                | POST /api/message
                v
        Raspberry Pi Pico W (3,3 V-logik)
        - Wi-Fi och REST-API
        - UTF-8, 5×7-font, scrollning
        - 90-byte framebuffer
                |
                | UART 57600 baud via nivåomvandlare
                v
        Arduino Micro (5 V-logik)
        - Tar emot och kvitterar bildrutor
        - Multiplexar 15 kolumnpositioner
                |
                | 7-bitars buss + decoder/reset
                v
       Originalets 74LS138, 74LS273, 74LS259
                |
                v
          ULN2003A / STA402A
                |
                v
         18 röda LED-paneler (90 × 7)
```

## 1. Pico W ↔ Arduino Micro: komplett UART-koppling

Kommunikationen går via en **TXB0104**, med **VCCL = 3,3 V** (Pico-sidan) och **VCCH = 5 V** (Micro-sidan). A är lågspänningssidan och B högspänningssidan.

| Funktion | Pico W | TXB0104 | Arduino Micro |
|---|---|---|---|
| Bildrutor, Pico → Micro | **GP0 / UART TX** | **A1 → B1** | **D0 / RX1** |
| ACK/NAK, Micro → Pico | **GP1 / UART RX** | **A2 ← B2** | **D1 / TX1** |
| Gemensam jord | GND | GND | GND |
| Lågspänningsmatning för nivåomvandlaren | **3V3** | **VCCL** | — |
| Högspänningsmatning för nivåomvandlaren | — | **VCCH** | **5V** |

**Kontrollera också TXB0104-modulens OE/enable-koppling** enligt just din modul; dess exakta pinanslutning finns inte dokumenterad i den verifierade programkoden. Anslut inte Micro:s 5 V-TX direkt till Pico:s 3,3 V-GPIO.

### Strömförsörjning (faktisk koppling)

**Pico W och Arduino Micro matas från originalskyltens befintliga 5 V-matning**, inte från någon separat 12 V → 5 V-omvandlare. Till LED-panelerna går 5 V via **tre tunna ledare i originalets flatkabel**. En av dessa 5 V-ledare används som avgreningspunkt för matningen till de nya mikrokontrollerna.

| Förbindelse | Matning |
|---|---|
| Originalskyltens flatkabel, en av tre +5 V-ledare | Avgrening till de nya kortens 5 V |
| Pico W **VSYS**, fysisk pinne 39 | +5 V från skylten |
| Arduino Micro **5V** | +5 V från skylten |
| TXB0104 **VCCH** | +5 V |
| TXB0104 **VCCL** | Pico W:s **3V3-utgång** (3,3 V) |
| Pico W, Micro, TXB0104 och skylt | Gemensam **GND** |

**Driftserfarenhet:** Denna matning har fungerat utan observerade problem under flera timmars drift, inklusive minst **12 timmars kontinuerlig körning**. Flatkabelns ledare har inte känts varma. Detta är en praktisk observation, inte en uppmätt garanti för ström- eller spänningsmarginaler.

**Att kontrollera vid behov:** Mät spänningen vid Pico W/Micro under belastning och kontrollera spänningsfall i matningen. De tre 5 V-ledarnas exakta interna fördelning och skyltens strömbudget är inte fullständigt dokumenterade. Undvik oavsiktlig samtidig USB- och extern 5 V-matning utan att kontrollera respektive korts matningsväg och backmatningsskydd. Pico W:s **3V3** är en utgång, inte en 5 V-ingång.

### Separation från originalprocessorn

Den ursprungliga **NEC D8049C-styrningens signalledningar till skyltens styrgränssnitt är fysiskt avkapade**. Enligt den utförda ombyggnaden återstår endast **5 V och GND** mellan den gamla styrningen och den nya skyltstyrningen. Därmed kan NEC-processorn inte driva samma externt frånkopplade logiksignaler som Arduino Micro.

De tre ursprungliga dator- och minneskretsarna är nu **fysiskt borttagna** ur sina socklar:

| Borttagen krets (märkning) | Funktion | Status |
|---|---|---|
| **NEC 850787 / D8049C** | Ursprunglig MCS-48-mikrokontroller | Borttagen |
| **M2764A-2FI** | 8 KiB EPROM med originalprogram | Borttagen |
| **SONY 8L 53 / CXK5816PN-10L** | 2 KiB SRAM | Borttagen |

**Funktionskontroll efter borttagningen:** Skylten startar och fortsätter att visa scrollande text med Pico W och Arduino Micro. De borttagna kretsarna behövs alltså inte för den nuvarande funktionen. Någon strömbesparing har inte mätts upp. **74LS138, 74LS273, 74LS259 och drivkretsarna sitter kvar** och används fortfarande.

## 2. Arduino Micro → LED-skyltens styrkontakt

Följande är den **verifierade mappningen i den fungerande Micro-sketchens kod**. Styrsignalerna motsvarar den ursprungliga 20-poliga bandkabelns signaler. Bandkabeln beskrivs här som **rad 1 och rad 2, position 1–10** enligt den tidigare kontinuitetskartläggningen; detta är **inte** ett påstående om standardnumrering för en 2×10 IDC-kontakt. Kontrollera kontaktens orientering före inkoppling.

### Databuss: sju bitar

| Arduino Micro | Bussignal | Bandkabel, rad 2 | Ursprunglig NEC D8049C | Användning |
|---|---|---|---|---|
| **D2** | P10 | 2 | pin 27 | Bussbit 0; 74LS259 D |
| **D3** | P11 | 3 | pin 28 | Bussbit 1; 74LS259 A0 |
| **D4** | P12 | 4 | pin 29 | Bussbit 2; 74LS259 A1 |
| **D5** | P13 | 5 | pin 30 | Bussbit 3; 74LS259 A2 |
| **D6** | P14 | 6 | pin 31 | Bussbit 4 |
| **D7** | P15 | 7 | pin 32 | Bussbit 5 |
| **D8** | P16 | 8 | pin 33 | Bussbit 6 |

### Decoder, reset och matning

| Arduino Micro | Signal | Bandkabel | Destination på originalkortet |
|---|---|---|---|
| **D9** | Aktiv låg reset | rad 2, pos 9 | 74LS273 /MR och 74LS259 /CLR |
| **A0** | /G2A (PROG) | rad 1, pos 4 | 74LS138 pin 4 |
| **A1** | C (adressbit 2) | rad 1, pos 5 | 74LS138 pin 3 |
| **A2** | B (adressbit 1) | rad 1, pos 6 | 74LS138 pin 2 |
| **A3** | A (adressbit 0) | rad 1, pos 7 | 74LS138 pin 1 |
| **A4** | /G2B | rad 1, pos 8 | 74LS138 pin 5 |
| **GND** | Jord | rad 1, pos 1–3; rad 2, pos 1 | Gemensam jord |
| **5 V-systemmatning** | +5 V | rad 1, pos 9–10; rad 2, pos 10 | Originalkortets logikmatning |

**Viktigt:** +5 V-raderna ovan dokumenterar **bandkabelns uppmätta matningsanslutningar**, inte en rekommendation att driva hela skylten från Arduino Micros 5 V-pin. Skylten behöver en korrekt dimensionerad 5 V-försörjning.

### Sammanfattning av använda Micro-pinnar

```text
D0  <- Pico GP0 via TXB0104       UART RX1
D1  -> Pico GP1 via TXB0104       UART TX1
D2  -> P10                        databuss bit 0
D3  -> P11                        databuss bit 1
D4  -> P12                        databuss bit 2
D5  -> P13                        databuss bit 3
D6  -> P14                        databuss bit 4
D7  -> P15                        databuss bit 5
D8  -> P16                        databuss bit 6
D9  -> /RESET                     aktiv låg
A0  -> 74LS138 /G2A
A1  -> 74LS138 C
A2  -> 74LS138 B
A3  -> 74LS138 A
A4  -> 74LS138 /G2B
GND -> gemensam jord
5V  <- skyltens befintliga 5 V via avgrening på flatkabel
```

## 3. Vad signalerna gör på originalkortet

### 74LS138: 3-till-8-avkodare

Micro sätter adressen på **A3/A2/A1** (74LS138 A/B/C), lägger bussen på **D2–D8** och styr avkodaren med **A0/A4** (/G2A och /G2B). 74LS138 G1 är kopplad till +5 V.

| Adress | 74LS138-utgång (aktiv låg) | IC-pin | Destination |
|---|---|---|---|
| 0 | /Y0 | 15 | 74LS273 radregister för panelgrupp 1 (panel 1–3) |
| 1 | /Y1 | 14 | 74LS273 för grupp 2 (panel 4–6) |
| 2 | /Y2 | 13 | 74LS273 för grupp 3 (panel 7–9) |
| 3 | /Y3 | 12 | 74LS273 för grupp 4 (panel 10–12) |
| 4 | /Y4 | 11 | 74LS273 för grupp 5 (panel 13–15) |
| 5 | /Y5 | 10 | 74LS273 för grupp 6 (panel 16–18) |
| 6 | /Y6 | 9 | 74LS259 ovanför panel 13 |
| 7 | /Y7 | 7 | 74LS259 ovanför panel 12 |

### Databuss → 74LS273 radregister

Samma sju bussledningar går till samtliga sex 74LS273.

| Bussignal / Micro-pin | 74LS273-ingång | IC-pin |
|---|---|---|
| P10 / D2 | D5 | 14 |
| P11 / D3 | D3 | 8 |
| P12 / D4 | D4 | 13 |
| P13 / D5 | D6 | 17 |
| P14 / D6 | D2 | 7 |
| P15 / D7 | D0 | 3 |
| P16 / D8 | D1 | 4 |

74LS273 **D7 (pin 18)** verkar oanvänd. **/MR (pin 1)** går till gemensam reset. Varje registers utgångar går vidare till en **ULN2003A**, som driver LED-raderna.

### Fysisk rad → registerutgång → databussbit

Dessa två mappningsarrayer finns i Micro-sketchens `updateColumnData()` och är viktiga att **inte ändra utan nya mätningar**:

```cpp
const uint8_t ROW_BUS_BITS[7]       = {5, 6, 4, 1, 2, 0, 3};
const uint8_t PHYSICAL_ROW_TO_Q[7] = {5, 3, 4, 6, 2, 0, 1};
```

Kombinationen ger följande mappning:

| Framebuffer-rad (bit) | 74LS273 Q | Bussbit P10=0…P16=6 | Arduino Micro |
|---|---|---|---|
| 0 | Q5 | 0 (P10) | D2 |
| 1 | Q3 | 1 (P11) | D3 |
| 2 | Q4 | 2 (P12) | D4 |
| 3 | Q6 | 3 (P13) | D5 |
| 4 | Q2 | 4 (P14) | D6 |
| 5 | Q0 | 5 (P15) | D7 |
| 6 | Q1 | 6 (P16) | D8 |

### 74LS273 → ULN2003A → panelrader

En ULN2003A per grupp driver sju radledningar; samma koppling upprepas på de tre panelerna inom gruppen.

| 74LS273 Q | 74LS273 pin | ULN2003A ingång | ULN2003A utgång | LED-panelanslutning enligt mätning |
|---|---|---|---|---|
| Q0 | 2 | 7 | 10 | undre 7 |
| Q1 | 5 | 6 | 11 | undre 2 |
| Q2 | 6 | 5 | 12 | undre 1 |
| Q3 | 9 | 4 | 13 | övre 1 |
| Q4 | 12 | 3 | 14 | övre 7 |
| Q5 | 15 | 2 | 15 | övre 3 = undre 5 |
| Q6 | 16 | 1 | 16 | övre 6 (**se osäkerhet nedan**) |

### 74LS259: kolumnlatchar

Båda latcharna får **D = P10 (Micro D2)**, **A0 = P11 (D3)**, **A1 = P12 (D4)**, **A2 = P13 (D5)** samt **/CLR = Micro D9**. De väljs via avkodarens /Y6 och /Y7. Koden styr 15 kolumnpositioner med två 8-bitars latchar (en utgång är oanvänd).

| Kolumnposition i Micro-koden | Latch | Q-utgång | Decoderadress |
|---|---|---|---|
| 0–7 | 74LS259 vid panel 13 | Q0–Q7 | 6 (/Y6) |
| 8–14 | 74LS259 vid panel 12 | Q0–Q6 | 7 (/Y7) |
| 15 (ej använd) | 74LS259 vid panel 12 | Q7 | 7 (/Y7) |

Kolumnutgångarna går vidare via motstånd till **STA402A**-drivarna. Den äldre mätningen anger följande fördelning (STA402A numreras 1–4 från vänster):

| Latch | Q | 74LS259 pin | STA402A nr / styrpin |
|---|---|---|---|
| /Y6 (panel 13) | Q0 | 4 | 2 / 5 |
| /Y6 | Q1 | 5 | 2 / 7 |
| /Y6 | Q2 | 6 | 2 / 9 |
| /Y6 | Q3 | 7 | 3 / 3 |
| /Y6 | Q4 | 9 | 2 / 3 |
| /Y6 | Q5 | 10 | 1 / 9 |
| /Y6 | Q6 | 11 | 1 / 7 |
| /Y6 | Q7 | 12 | 1 / 5 |
| /Y7 (panel 12) | Q0 | 4 | 4 / 3 |
| /Y7 | Q1 | 5 | 4 / 5 |
| /Y7 | Q2 | 6 | 4 / 7 |
| /Y7 | Q3 | 7 | 4 / 9 |
| /Y7 | Q4 | 9 | 3 / 7 |
| /Y7 | Q5 | 10 | 3 / 9 |
| /Y7 | Q6 | 11 | 3 / 5 |
| /Y7 | Q7 | 12 | — (oanvänd) |

## 4. LED-panelernas kolumnanslutningar — äldre kartläggning

Följande mätningar beskriver kopplingarna från STA402A-utgångarna till panelernas **fem kolumnanslutningar**. De upprepas för varje grupp om tre paneler. **Det finns en olöst konflikt kring ”övre pin 6”, se varningen direkt efter tabellen.**

| Panelposition i grupp | Panelanslutning | STA402A nr / utgångspin |
|---|---|---|
| 1:a (panel 1, 4, 7, 10, 13, 16) | övre 2 | 2 / 4 |
| 1:a | övre 4 = undre 4 | 2 / 8 |
| 1:a | övre 6* | 3 / 2 |
| 1:a | undre 3 | 2 / 6 |
| 1:a | undre 6 | 2 / 2 |
| 2:a (panel 2, 5, 8, 11, 14, 17) | övre 2 | 1 / 8 |
| 2:a | övre 4 = undre 4 | 1 / 4 |
| 2:a | övre 6* | 4 / 2 |
| 2:a | undre 3 | 1 / 6 |
| 2:a | undre 6 | 4 / 4 |
| 3:e (panel 3, 6, 9, 12, 15, 18) | övre 2 | 4 / 6 |
| 3:e | övre 4 = undre 4 | 3 / 6 |
| 3:e | övre 6* | 3 / 8 |
| 3:e | undre 3 | 4 / 8 |
| 3:e | undre 6 | 3 / 4 |

**⚠️ Olöst dokumentationskonflikt:** I en äldre kartläggning anges ”övre pin 6” som kolumnanslutning, men den senare radkartläggningen anger **samma pinne som radanslutning via ULN2003A pin 16**. En möjlig förklaring är att kolumnanslutningen egentligen är **övre pin 5**. Detta är **inte verifierat**. Ändra inte fysisk kabeldragning utifrån den osäkra raden utan att mäta igen. Den fungerande Micro-firmwaren styr den verifierade logikbussen, inte varje panelpinne direkt.

## 5. Bildruteprotokoll (Pico → Micro)

**UART:** 57600 baud, normalt 8N1, via TXB0104.

| Ordning | Storlek | Innehåll |
|---|---:|---|
| 1 | 1 byte | Start `0xAA` |
| 2 | 1 byte | Start `0x55` |
| 3 | 90 byte | Framebuffer: kolumn 0–89; bit 0–6 = rad 0–6 |
| 4 | 1 byte | XOR av alla 90 framebuffer-byte |

Totalt **93 byte per bildruta**. Arduino Micro svarar `0x06` (**ACK**) om checksumman stämmer, annars `0x15` (**NAK**). Mottagningen avbryter ett ofullständigt paket efter 200 ms. Pico använder 300 ms timeout för ACK. Bilden byts i början av en refreshcykel för att undvika delvis uppdaterade bildrutor.

Micro skannar **15 kolumnpositioner** över **6 parallella panelgrupper**. `COLUMN_US = 750` µs är hålltiden per kolumn (total cykeltid är längre eftersom registerskrivningar också tar tid). UART läses även under skanningen för att undvika buffertöverflöd.

## 6. REST-API och användning

Pico W ansluter till 2,4 GHz Wi-Fi. Utan anslutning scrollar `Connecting to WiFi...` på skylten; återanslutning sker automatiskt. SSID/lösenord ligger lokalt i `ledsign_pico/secrets.h`, som **inte** ska checkas in.

```bash
# Kontrollera API:t (byt IP till Picons aktuella adress)
curl http://192.168.10.129/api/health

# Ändra texten
curl -X POST http://192.168.10.129/api/message \
  -H 'Content-Type: application/json' \
  -d '{"text":"Hej från Småland! ÅÄÖ åäö"}'
```

IP-adressen är ett **exempel från ett lyckat test**, inte en garanterad statisk adress. API:t saknar autentisering och bör bara exponeras på ett betrott lokalt nätverk. Den nuvarande enkla JSON-parsern är avsedd för okomplicerade meddelanden, inte alla JSON-escape-sekvenser.

## 7. Repository och byggning

```text
80s-led-sign/
├── README.md
├── .gitignore
├── ledsign_pico/
│   ├── ledsign_pico.ino
│   ├── secrets.example.h
│   └── secrets.h          # lokal, Git-ignorerad
└── ledsign_micro/
    └── ledsign_micro.ino
```

- **Pico W:** välj Raspberry Pi Pico W och ladda upp `ledsign_pico/ledsign_pico.ino`.
- **Arduino Micro:** välj Arduino Micro och ladda upp `ledsign_micro/ledsign_micro.ino`.
- **Viktigt:** Arduino IDE kräver normalt att sketchens mapp och `.ino`-fil har samma namn.
- **Wi-Fi:** kopiera `secrets.example.h` till `secrets.h` i Pico-mappen och fyll i dina uppgifter. Lägg `secrets.h` i `.gitignore`.

## 8. Ursprunglig hårdvara och dokumentationsstatus

Identifierade originalkomponenter omfattar **NEC D8049C**, **74LS138**, **sex 74LS273**, **två 74LS259**, **sex ULN2003A** och **fyra STA402A**. NEC-styrningens signalledningar till den nya styrningen är avkapade; endast matning och jord återstår enligt ombyggnaden. NEC D8049C, M2764A-2FI EPROM och Sony CXK5816PN-10L SRAM är nu fysiskt borttagna; skylten har funktionstestats efter ingreppet och fungerar fortsatt. Originalets EPROM har tidigare lästs ut (8 KiB). Dagens Pico/Micro-lösning återanvänder originalskyltens register och drivare i stället för att styra 630 LED:ar individuellt.

**Verifierat i drift:** Pico ↔ Micro UART, Micro:s programmerade pinmappning, multiplexning och textvisning på den faktiska skylten.

**Baserat på äldre kontinuitetsmätningar, inte fullständigt omverifierat:** intern IC-pin-till-panel-pin-kartläggning och bandkabelns fysiska positioner. Den specifika konflikten kring panelens övre pin 5/6 återstår att kontrollera. Bevara dessa reservationer om dokumentet används för ny kabeldragning eller ombyggnad.
