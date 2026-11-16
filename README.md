# elev-iot start

Startpakke for IoT-boksen din: en ESP8266 med skjerm og en MPU-6050 (akselerometer og gyro).
Boksen sender målinger til `api.dataskyen.no`, og nettsiden din henter dem derfra.

```
boksen (firmware/)  ──målinger──►  api.dataskyen.no  ◄──henter──  nettsiden din (nettside/)
                    ◄─kommandoer─                     ◄─kommandoer─
```

| Mappe | Innhold |
|---|---|
| `firmware/ELEV-IOT-MAL/` | Koden til boksen. Kopier mappen, ikke endre malen. |
| `nettside/index.html` | Eksempelside med levende graf, kloss som vipper og knapper. Bygg videre på den. |

## Kom i gang med boksen

1. **Koble til sensoren** (strømmen av mens du kobler):

   | MPU-6050 | ESP8266 |
   |---|---|
   | VCC | 3V3 |
   | GND | GND |
   | SCL | D1 |
   | SDA | D2 |

   XDA, XCL, AD0 og INT kobles ikke til.

2. **Arduino IDE:** installer kortet *esp8266 by ESP8266 Community* (Boards Manager) og bibliotekene
   **ArduinoJson** og **U8g2** (Library Manager). MPU-6050 trenger ikke noe bibliotek.
3. **Kopier** mappen `ELEV-IOT-MAL` og gi kopien og `.ino`-filen samme navn, for eksempel `ELEV-IOT-07`.
4. **Registrer boksen** på <https://api.dataskyen.no/register.php>. Du får `deviceID`, en nøkkel og `datapointID`-er.
5. **Kopier** `secrets.example.h` til `secrets.h`, og fyll inn WiFi og nøkkelen.
6. **Lim inn** `DP_`-linjene fra registreringssiden i `.ino`-filen, og sett `boxName`.
7. **Last opp.** Board: *Generic ESP8266 Module*. Serial Monitor på 115200.

Skjermen viser MAC-adressen til boksen før den er på nett. Den trenger IT for å slippe boksen inn på skolenettet.

## Kom i gang med nettsiden

Åpne `nettside/index.html` med Live Server i VS Code, skriv inn `deviceID` og trykk Lagre.
Nøkkelen trenger du bare for å sende kommandoer.

Alle adressene i API-et står på <https://api.dataskyen.no/>.

## Tre regler

- **Send og hent én gang i sekundet, ikke oftere.** Hele klassen deler serveren.
- **Nøkkelen skal ikke ligge i koden.** `secrets.h` er holdt utenfor git, og nettsiden lagrer nøkkelen i nettleseren.
- **Endre i din egen kopi**, ikke i malen.

## Temperatur og fuktighet (DHT)

Malen er klargjort for en DHT11 eller DHT22 på D4. Sett `USE_DHT 1` i `.ino`-filen, installer bibliotekene
*DHT sensor library* og *Adafruit Unified Sensor*, og sett `DP_TEMP` og `DP_FUKT`.
Lysdioden på kortet bruker samme pinne, så kommandoen `led` virker ikke samtidig med DHT.

## Egne kommandoer

Legg dem i `runCommand()` i `.ino`-filen. Fra nettsiden sender du dem med `send_command.php`.
Malen kan `led` (verdi `1` eller `0`), `tekst` (vises nederst på skjermen) og `kalibrer`.

## Kalibrering

Ingen sensor er helt nøyaktig. En boks som ligger flatt kan vise Z = 1.35 i stedet for 1.00.
Legg boksen flatt og helt i ro, og trykk Kalibrer på eksempelsiden (kommandoen `kalibrer` med verdi `1`).
Boksen måler i ett sekund, og viser etterpå X = 0, Y = 0 og Z = 1. Avviket lagres i boksen,
så det holder å gjøre én gang, etter at sensoren er festet der den skal sitte.

Står det `Kmd feil: kalibrer` på skjermen, lå ikke boksen flatt eller i ro. Prøv igjen.
Verdien `0` fjerner kalibreringen, så du ser det sensoren faktisk måler.
