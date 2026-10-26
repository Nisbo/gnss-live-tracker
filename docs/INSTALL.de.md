# GNSS Live Tracker · Installation und Ersteinrichtung

Für den **Heltec Wireless Tracker V2 mit ESP32-S3 und 8 MiB Flash**. Auf der Platine muss V2 stehen. Diese Firmware ist nicht für Tracker V1.x oder beliebige andere ESP32-Boards bestimmt.

Dies ist der **Release-Kandidat 0.30.3**. USB-Erstinstallation und das Zurückschreiben vollständiger Aufzeichnungen müssen noch praktisch abgenommen werden. Der Stand ist in den [Release-Hinweisen](RELEASE-NOTES.md) dokumentiert.

## Die richtige Datei wählen

| Situation | Datei und Vorgehen | Vorhandene Daten |
| --- | --- | --- |
| Erstinstallation oder Rückkehr von einer anderen Firmware | `gnss-live-tracker-v0.30.3.factory.bin`, per USB über ESPHome Web | Alle Einstellungen und Aufzeichnungen auf dem Gerät werden ersetzt |
| GNSS Live Tracker läuft bereits | `gnss-live-tracker-v0.30.3.ota.bin`, über den Firmware-Tab des Trackers | Einstellungen und Aufzeichnungen auf dem Gerät bleiben erhalten |

Die Factory-Datei gehört nicht in das OTA-Formular. Wenn bereits wichtige Daten vorhanden sind, vor einer Installation ein Backup herunterladen.

## 1. Erstinstallation per USB

Du brauchst die Factory-BIN aus dem Release-Paket, ein USB-**Datenkabel** und einen Computer mit einem Browser, der Web Serial unterstützt. Chrome oder Edge am Computer sind einfache Möglichkeiten. Die anschließende WLAN-Einrichtung geht auch am Handy.

1. Den V2 am Computer anschließen. Serielle Monitore und andere Programme schließen, die seinen USB-Port verwenden.
2. **https://web.esphome.io/** öffnen. **Connect** wählen, den zum Tracker gehörenden USB-Port auswählen und den Browserzugriff auf diesen Port erlauben.
3. **Install** wählen, unsere heruntergeladene **`.factory.bin`** auswählen und die Installation starten. Die Dateiauswahl verwenden, nicht **Prepare for first use**: Letzteres installiert eine ESPHome-Vorbereitungsfirmware.
4. Die Stromversorgung bis zum Abschluss angeschlossen lassen. Das vollständige Image umfasst 8 MiB; die Übertragung kann entsprechend dauern. Den Neustart abwarten, gegebenenfalls einmal **RST** drücken.
5. Auf dem Display sollten Tracker-/GNSS-Informationen erscheinen. Danach mit dem Einrichtungs-WLAN fortfahren.

Fehlt der USB-Port oder scheitert die Verbindung: Datenkabel prüfen, anderen USB-Port versuchen und Programme mit geöffnetem Port schließen. Für den Programmiermodus **USER/BOOT gedrückt halten, RST kurz drücken und USER/BOOT loslassen**. Den Port danach erneut auswählen; sein Name kann sich ändern. Allein der Programmiermodus installiert oder löscht noch nichts.

ESPHome Web dient hier als Browser-Flasher. Unsere Firmware benötigt weder ESPHome noch Home Assistant. Wir liefern dafür ein vollständiges Erstinstallationsimage. Der genaue Ablauf im Installer ist noch Bestandteil des ausstehenden Hardwaretests.

## 2. Mit dem Einrichtungs-WLAN verbinden

Ohne gespeicherte WLAN-Profile startet sofort **`Tracker-V2-Setup`**, zunächst offen und ohne Passwort.

1. Computer oder Handy mit diesem WLAN verbinden.
2. Meldet das Handy „Kein Internet“, die Verbindung trotzdem beibehalten. Falls nötig das automatische Wechseln in ein anderes WLAN vorübergehend abschalten.
3. Im Browser ausdrücklich **`http://192.168.4.1`** öffnen. Es gibt kein automatisch aufspringendes Anmeldefenster und kein HTTPS-Zertifikat zu bestätigen.
4. Die Webansicht startet auf Englisch. Oben kann **Deutsch** ausgewählt werden. Websprache und Sprache auf dem Gerätedisplay sind getrennte Einstellungen.

Solange ein offener Hotspot aktiv ist, kann jeder in Funkreichweite darauf zugreifen. Name und optionales Passwort lassen sich unter **WLAN / Wi-Fi** ändern. Die Warnung bleibt bei offen konfiguriertem Hotspot auch sichtbar, wenn er gerade ausgeschaltet ist.

## 3. Heim-WLAN einrichten

1. Den Tab **WLAN / Wi-Fi** öffnen. Verfügbare WLANs suchen und auswählen; eine versteckte SSID lässt sich direkt eingeben.
2. Name und Passwort des **2,4-GHz-WLANs** eintragen. Mit den Formularaktionen speichern und verbinden. Reine 5-GHz-Netze oder Unternehmensnetze mit zusätzlicher Anmeldung werden nicht unterstützt.
3. Computer oder Handy wieder mit dem Heimnetz verbinden, falls das Einrichtungs-WLAN verschwindet.
4. Die IP des Trackers auf dessen **Netzwerk-Displayseite** oder in der Geräteliste des Routers ablesen. Im Browser `http://` gefolgt von dieser IP öffnen. Eine DHCP-Reservierung im Router kann die Adresse dauerhaft gleich halten.

Bis zu acht Profile lassen sich speichern, einsehen, bearbeiten und löschen. Ein Profil kann gespeichert werden, ohne direkt zu verbinden. Die Verbindung ist nicht an einen bestimmten Access Point gebunden: Mehrere APs mit derselben SSID sind möglich. Fällt das aktuelle WLAN aus, versucht der Tracker andere gespeicherte Netze. Nach 60 Sekunden ohne WLAN-IP öffnet er den konfigurierten Hotspot und versucht parallel weiter, ein gespeichertes Netz zu erreichen.

Mit **USER für 3 Sekunden** lässt sich der Hotspot manuell einschalten, ohne WLAN-Profile zu löschen. Seine Adresse ist `http://192.168.4.1`; der eingestellte Name und das eingestellte Passwort können inzwischen von den Anfangswerten abweichen.

## 4. Die erste GNSS-Position

Den Tracker so platzieren, dass die GNSS-Antenne möglichst freie Sicht zum Himmel hat. Der erste Fix kann mehrere Minuten dauern. Empfang im Gebäude hängt stark vom Standort ab und ist nicht garantiert.

Unter **Live** stehen Fix-Status, Koordinaten, gemeldete Satelliten und die im Fix gelisteten Satelliten. Gemeldete Satelliten allein belegen noch keinen gültigen Fix. Ein positiver C/N₀-Wert bestätigt ein aktuell verfolgtes Signal. Unter **Himmel / Sky** werden Richtung, Höhe am Himmel und Signalstärke dargestellt.

**Für GNSS, Display, Aufzeichnung auf dem Tracker und lokale Webansicht braucht man weder TTN noch ein Gateway oder MQTT.** Die Positionskarte benötigt beim Laden Internet am Browser. Die 3D-Erde berechnet Positionen aus Bahndaten, die im Browser geladen oder importiert werden; das sind keine zusätzlichen Messdaten des Empfängers.

## 5. Optional: Positionen an TTN senden

Dafür werden ein TTN-Konto und ein erreichbares, mit dem passenden Netzwerk verbundenes Gateway benötigt. Ein registriertes Gerät allein schafft keine Funkabdeckung.

Für die praktisch erprobte EU868-Einstellung:

1. In der TTN-Konsole eine **Application** anlegen oder öffnen. Unter **End devices → Register end device** die manuelle Einrichtung wählen.
2. **Europe 863–870 MHz**, den empfohlenen Plan mit **SF9 für RX2**, **LoRaWAN 1.0.2**, **Regional Parameters 1.0.2 Revision B**, **OTAA** und **Klasse A** auswählen.
3. Ohne zugewiesene JoinEUI für dieses programmierbare Gerät kann für diese TTN-Einrichtung **`0000000000000000`** verwendet werden: 16 Nullen. Eine vorhandene zugewiesene JoinEUI stattdessen übernehmen. Die **DevEUI** bei Bedarf über **Generate** in TTN erzeugen, den **AppKey** ebenfalls dort generieren. Das sind die eigenen Zugangsdaten dieses Geräts, keine gemeinsamen Heltec-Werte.
4. Im **LoRaWAN**-Tab des Trackers die passende Region wählen und **DevEUI**, **JoinEUI/AppEUI** und **AppKey** in die gleichnamigen Felder kopieren. Speichern. Geänderte Zugangsdaten lösen einen Neustart mit neuer Anmeldung aus. Der AppKey lässt sich über den Einblenden-Button anzeigen.
5. In TTN unter **Payload formatters → Uplink → Custom JavaScript formatter** den vollständigen Inhalt der Datei **`ttn-decoder.js`** dieses Releases einfügen und speichern. Die Datei kann auch vom Tracker heruntergeladen werden. **Ein Decoder unterstützt Basis, Standard, Systemdetails und die kompakten regionalen Varianten.** Je nach Paket fehlen unterschiedliche optionale Felder.
6. **Live data** in TTN geöffnet lassen. Zuerst nach dem Join, dann nach einer Nachricht mit dekodierter Position/Status schauen. Standardmäßig kommt alle **5 Minuten** eine Positionsmeldung; eine Startmeldung und der erste Fix können zusätzlich gemeldet werden. Ohne Fix wird weiterhin Status gesendet, daraus ergibt sich aber keine gültige aktuelle Position.

Vorgaben: **EU868, SF7, ADR aus, Paket Standard, Positionsintervall 5 Minuten**. SF beschreibt die Modulation und ist keine Sendeleistung. Mit ADR können das Netzwerk und die Wiederherstellung nach ausbleibenden Downlinks Datenrate/Leistung verändern; dabei die Sendezeitschätzung beachten. Die TTN-Sandbox hat eine tägliche Sendezeitvorgabe zusätzlich zu den Funkpausen. Die Webansicht erklärt und schätzt den Verbrauch; sie garantiert kein verfügbares Tagesbudget.

Alle anderen auswählbaren Regionen sind **experimentell**, ohne praktische Join-, Uplink- und Downlink-Tests. Dafür die regionalen Hinweise im LoRaWAN-Tab und die [Regionsdokumentation](REGIONS.md) verwenden. Die EU868-Einrichtung gilt nicht unverändert für jede Region.

## 6. Aufzeichnung, Display und optional MQTT

Unter **Track** wird die Positionsaufzeichnung eingestellt, unter **Live** der Satelliten-Diagrammverlauf. Beide bieten **Keine Aufzeichnung**, **Nur im Browser** und **Auf dem Tracker**. Browseraufzeichnung braucht eine geöffnete, laufende Seite. Die Aufzeichnung auf dem Tracker läuft unabhängig davon weiter. Bei vollem Ringspeicher werden die ältesten Einträge überschrieben. Positionen brauchen einen gültigen Fix; die Satellitenstatistik kann ohne Fix weiterlaufen.

Zuerst wird der ausgewählte Diagrammzeitraum geladen, danach ältere Daten im Hintergrund. Ein geprüfter Browser-Cache beschleunigt spätere Aufrufe. Privates Surfen, Bereinigung oder Speicherknappheit können Browserdaten entfernen; der Cache ersetzt kein heruntergeladenes Backup.

USER: Bei ausgeschaltetem Display weckt ein kurzer Druck es auf. Bei eingeschaltetem Display schaltet ein einfacher Druck zur nächsten aktivierten Seite. Ein Doppeldruck ruft die konfigurierte Start-/Übersichtsseite auf. Drei Sekunden halten öffnet den Hotspot. **RST** startet das Gerät neu. Seitenreihenfolge, Uhr, Wechselintervall, Helligkeit und Timeout stehen unter **Display**.

Für MQTT unter **MQTT** Brokeradresse und Port eintragen. Zugangsdaten sind optional, wenn der Broker anonyme Verbindungen erlaubt. Home-Assistant-Erkennung ist optional. MQTT nutzt TCP ohne TLS und benötigt TTN nicht. Netzwerkausfälle stoppen die Aufzeichnung auf dem Tracker nicht.

## 7. Aktualisieren und sichern

Vor größeren Änderungen unter **Backup** die gewünschten Aufzeichnungen auswählen und eine `.gltb`-Datei herunterladen. Privat aufbewahren: Sie enthält Zugangsdaten und gegebenenfalls genaue Positionen, unverschlüsselt. Reine Browser-Verläufe, Browser-Einstellungen und die Firmware sind nicht enthalten.

Für spätere Updates unter **Firmware** ausschließlich die **`.ota.bin`** des Releases auswählen und installieren. Strom während Übertragung, Neustart und Bestätigung angeschlossen lassen. Geräteeinstellungen und Aufzeichnungen bleiben erhalten. Anschließend die Webansicht neu laden. Bei Dateiauswahl per URL lädt der Browser die Datei; die Quelle muss Browserdownloads erlauben. Eine lokal ausgewählte Datei ist der einfachste Weg.

Zum Wiederherstellen zuerst dieselbe oder eine kompatible neuere Firmware installieren, unter **Backup** die `.gltb` auswählen und Einstellungen/Datenbereiche prüfen. **Auswahl ohne Schreiben prüfen** führt noch keine Wiederherstellung aus. **Auswahl wiederherstellen** erst verwenden, wenn die gewählten Einstellungen und Daten ersetzt werden sollen. Aufzeichnungen werden vollständig ersetzt, nicht zusammengeführt. Danach startet der Tracker neu; wiederhergestellte WLAN-Zugangsdaten gelten dann. Bei Unterbrechung dieselbe Datei erneut wählen und wie angezeigt fortsetzen. LoRaWAN-Sitzungen werden nicht zurückgespielt; gegebenenfalls erfolgt ein neuer Join. Nicht gleichzeitig zwei Geräte mit derselben wiederhergestellten LoRaWAN-Identität betreiben.

Das tatsächliche Zurückschreiben kompletter Aufzeichnungsbereiche muss bei diesem Kandidaten noch praktisch geprüft werden. Dazu die [Release-Hinweise](RELEASE-NOTES.md) beachten.

## Fehlerhilfe und Wiederherstellung per USB

| Problem | Prüfen |
| --- | --- |
| Kein USB-Gerät | Datenkabel, USB-Port, geöffneter serieller Monitor; USER/BOOT + RST für Programmiermodus |
| Einrichtungsseite öffnet nicht | Im Tracker-WLAN bleiben; `http://192.168.4.1` direkt eingeben; kein automatisches Anmeldefenster erwarten |
| Webansicht im Heimnetz nicht erreichbar | Erreichbares gemeinsames Netz und aktuelle IP auf Display/Router prüfen; USER 3 Sekunden für Hotspot |
| Keine Position | Fix-Status, freie Sicht der GNSS-Antenne, Anlaufzeit; Satellitenanzahl allein reicht nicht |
| Nichts bei TTN | Gateway/Abdeckung, Region/FSB, Zugangsdaten und Live data; „gesendet“ am Tracker bestätigt keinen TTN-Empfang |
| Diagramm anfangs unvollständig | Ladehinweis lesen; ausgewählter Zeitraum zuerst, ältere Daten im Hintergrund |
| OTA-Datei abgelehnt | `.ota.bin` für V2 verwenden, keine `.factory.bin` und keine andere Board-Firmware |
| Firmware startet nicht mehr | USB-Programmiermodus, korrektes Factory-Image neu installieren, anschließend privates Backup wiederherstellen |

Der ROM-USB-Bootloader des ESP32 bleibt bei einem Fehler der Anwendungsfirmware normalerweise erreichbar. Das ist keine Garantie bei Hardware-, Strom- oder USB-Problemen. Factory-Wiederherstellung ersetzt alle Daten; deshalb sichern, solange das Gerät noch läuft. Kommandozeilen-Wiederherstellung und Build stehen in [BUILD.md](BUILD.md). Die aktuelle V2-Konfiguration setzt keine Secure-Boot-/Verschlüsselungs-eFuses.

Offizielle Referenzen: [ESPHome Web](https://esphome.io/guides/faq/), [TTN manuell mit OTAA einrichten](https://www.thethingsindustries.com/docs/hardware/devices/adding-devices/manual/otaa/), [Datenschutz und Zugriffsmodell](SECURITY.md).
