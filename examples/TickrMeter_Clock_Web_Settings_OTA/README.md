# TickrMeter laikrodis: web, OTA ir deep sleep

Skirta patikrintam TickrMeter Rev. 1.6 su ESP32-WROOM-32E ir
GxEPD2_290_C90c 296 × 128 trispalviu ekranu. GPIO išdėstymas išlaikytas.

2026-10-11 įrenginio savininkas patvirtino: **„viskas veikia“**.
GPIO32 matuoja akumuliatorių, GPIO33 – USB maitinimo įtampą (apie 5 V,
perskaičiavus daliklį). Daliklių rezistoriai nominaliai apie 10 kΩ + 10 kΩ.

## Veikimas

- Po RESET, maitinimo įjungimo arba programinio perkrovimo AP, web ir OTA
  veikia **5 minutes**. AP: `TickrMeter-CUSTOM`, slaptažodis `12345678`.
  Atverkite <http://192.168.4.1/>. Puslapis veikia be interneto ir išorinių šriftų.
- Nustatymų išsaugojimas neperkrauna laikrodžio ir nepratęsia lango.
  Namų Wi-Fi duomenys taikomi iškart. Po sėkmingo OTA įvyksta perkrovimas.
- Pasibaigus langui išjungiami AP, DNS, HTTP ir Wi-Fi; prasideda deep sleep.
  Laikrodis pabunda ties kiekvienos minutės pradžia, patikrina žadintuvą,
  atnaujina ekraną ir užmiega likusiai minutės daliai. Šis pabudimas neatidaro web.
- Laikas palaikomas ESP32 RTC, su Lietuvos vasaros/žiemos laiku.
  NTP sinchronizuojamas paleidžiant ir kas valandą. Nesėkmingą sinchronizavimą
  bandoma pakartoti per kitą pabudimą. Be tinklo RTC gali palaipsniui nukrypti.
  Atjungus maitinimą RTC laikas neišlieka: reikia NTP.
- Žadintuvo diena ir paskutinė parodyta minutė išlieka RTC atmintyje per miegą.
  Žadintuvas veikia ir paleidus laikrodį nustatytą žadintuvo minutę.
- Pradėtas OTA leidžiamas užbaigti ir po 5 min. ribos. Nutrūkus įkėlimui
  atnaujinimas atšaukiamas; laukimo limitas be duomenų yra 90 s.

## Ekranas ir šriftai

Dideli HH:MM skaitmenys centruoti, data, savaitės diena ir žadintuvas turi
atskiras vietas. Antraštė keičiama web puslapyje; tikrinamas jos tikras plotis.
Naudojami bibliotekos FreeSans šriftai palaiko ASCII, todėl antraštėje
rašykite `KUPISKIS`, ne `KUPIŠKIS`. Web sąsaja naudoja UTF-8 ir lietuvių kalbą.

`W` viršuje reiškia atidarytą web langą; `R` – įprastą RTC režimą.
Wi-Fi piktograma rodo radijo ryšį atnaujinimo metu; miegant radijas išjungtas.
Baterijos ikona ir įtampa (pvz., `3.85 V`) rodomos viršuje kairėje.
Prieš kiekvieną ekrano atnaujinimą GPIO4 įjungiamas į HIGH, palaukiama
150 ms, skaitomi GPIO32 ir GPIO33 ADC1 (16 RAW ir 16 atskirų kalibruotų mV
konversijų vidurkiai), tada GPIO4 iškart išjungiamas į LOW. Per deep sleep
GPIO4 laikomas LOW. Šį išjungimą vartotojas patvirtino 2026-10-11.
Nominalus 10 kΩ / 10 kΩ daliklio koeficientas yra 2.000; galutinė įtampos
paklaida multimetru dar neišmatuota. Nevalidžiam ADC rodmeniui rodoma `--.-- V`.
Web meniu **Baterija ir kalibravimas** galima pasirinkti GPIO32 arba GPIO33.
Kiekvienas kanalas turi atskirą NVS išsaugotą kalibravimo koeficientą.
Meniu rodo nominalią išmatuotą įtampą be pataisos ir kalibruotą įtampą.
Paspauskite **Pamatuoti iš naujo**, įveskite tuo metu multimetru matuojamą
tikrą įtampą ir spauskite **Išsaugoti šaltinį / kalibruoti**. Koeficientas
perskaičiuojamas kaip `tikra įtampa / įtampa be pataisos`; pakartotinis
kalibravimas ankstesnės pataisos nepadaugina. Senesnis nei 60 s ar pasikeitęs
matavimas atmetamas: kalibruojant reikia pamatuoti dar kartą. Tuščias tikros
įtampos laukas leidžia keisti kanalą ir ribas nekeičiant jo kalibravimo.
Atkūrimo mygtukas grąžina tik pasirinkto GPIO koeficientą į 1.000.

Ikona turi keturias padalas: 1–25 %, 26–50 %, 51–75 %, 76–100 %.
0 % rodoma tuščia ikona, nevalidžiam matavimui – `?` ir `--.-- V`.
Procentai apytiksliai tiesiniai tarp reguliuojamų 0 % ir 100 % įtampos ribų
(numatyta 3.00 V ir 4.20 V); tikslus cheminis SOC ir įkrovimo būsena nenustatomi.
Akumuliatoriaus įkrovai pasirinkite **GPIO32**. GPIO33 matuoja USB maitinimą;
jo 5 V rodmuo nėra akumuliatoriaus įkrova ar patvirtinimas, kad jis kraunamas.
Programa leidžia pasirinkti abu kanalus, automatinio perjungimo nėra.
Antraštei palikta 138 px;
senos per ilgos išsaugotos antraštės ekrane trumpinamos su `...`.

**C90c tvarkyklė neturi greito dalinio atnaujinimo.** Pilnas atnaujinimas gali
trukti apie 27 s, todėl naujos minutės rodmuo tampa matomas su ekrano vėlavimu.
Tai taip pat riboja energijos taupymą: procesorius nemiega ekrano atnaujinimo
metu. Realų vartojimą ir GPIO laikymą būtina patikrinti plokštėje; baterijos
veikimo trukmė dar neišmatuota. Web užklausos aptarnaujamos BUSY laukimo metu.

## Kompiliavimas ir įrašymas

1. Arduino IDE pasirinkite **ESP32 Dev Module** ir ESP32 Arduino core
   (kompiliavimas tikrintas su **3.1.1**).
2. Įdiekite GxEPD2 ir Adafruit GFX. Atverkite šio aplanko `.ino`,
   palikite `ClockLogic.h` tame pačiame aplanke.
3. Pasirinkite **4 MB flash** ir skaidinių schemą su dviem OTA programos
   skaidiniais, pvz. **Default 4MB with spiffs**. `Huge APP / No OTA` netinka.
4. Pirmą kartą įrašykite per UART/USB. Namų Wi-Fi duomenys kode tušti;
   senieji `clockcfg` NVS nustatymai, jei yra, išlieka.
5. Vėlesniam OTA naudokite tik programos `TickrMeter_Clock_Web_Settings_OTA.ino.bin`.
   `merged.bin`, `bootloader.bin` ir `partitions.bin` nėra OTA programos failai.

AP slaptažodį galima pakeisti `OTA_AP_PASS` konstantoje. Web sąsaja neturi
atskiro prisijungimo: lango metu ją gali pasiekti AP arba namų LAN naudotojai.

## Patikra tikrame įrenginyje

1. Paspausti RESET, prisijungti prie AP, patikrinti būseną ir minutės atskaitą.
2. Išsaugoti nustatymus, patikrinti RGB, garsą, 3 s žadintuvą ir nutildymą.
   Atverti būseną ekrano atnaujinimo metu – serveris turi atsakyti.
3. Patikrinti neteisingą žadintuvo laiką, per ilgą antraštę ir slaptažodį;
   serveris turi atmesti klaidingus duomenis nepakeisdamas nustatymų.
4. Palaukti 5 min.: AP turi dingti ir neatsirasti per minutinius pabudimus.
   Ekranas turi keistis kas minutę; RESET vėl turi atidaryti 5 min. langą.
5. Nustatyti žadintuvą artimiausiai minutei ir patikrinti, kad jis suskamba
   vieną kartą bei veikia uždarius web langą.
6. Patikrinti OTA teisingu `.bin`, neteisingu failu ir nutraukus perdavimą.
   Tik sėkmingas įkėlimas turi sukelti perkrovimą. Pradėti įkėlimą prieš
   5 min. ribą ir patikrinti, kad jis nenutrūksta dėl miego.
7. Atjungti namų Wi-Fi: galiojantis RTC laikas turi išlikti per miegą.
   Visiškai atjungus maitinimą be NTP turi būti aiškus `--:--` pranešimas.

Kompiliavimo metu `ClockLogic.h` statinės patikros tikrina 5 min. ribą,
`millis()` persisukimą, mikrosekundžių miego trukmę ir žadintuvo metų ribą.
Kompiliavimas nepakeičia fizinės GPIO, ekrano, OTA ir energijos vartojimo patikros.
