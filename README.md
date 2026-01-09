# Minecraft-Bedrock-auto-Bot
Ein kleines Desktop-Tool (kein Client-Hack, keine Injection), das über Tastatur- und Maus-Eingaben das automatische Abbauen und Neu-Pflanzen von Kartoffeln auf einem 9x9 Feld in Minecraft Bedrock Edition ermöglicht. Das Feld besitzt in der Mitte ein Wasserloch (5,5), das der Bot umgeht.

## Voraussetzungen
* Python 3.10+
* Tkinter-Unterstützung für Python (unter Linux z. B. `python3-tk` nachinstallieren)
* Abhängigkeiten installieren:
  ```bash
  pip install -r requirements.txt
  ```
* Falls ein SyntaxError erscheint: Prüfe mit `python --version`, dass wirklich Python 3.10+ verwendet wird.

## Startposition / Starting Position
Stelle dich links unten am Feld auf Position (0,1), schaue geradeaus auf das Feld und halte Kartoffeln in der Hand.

## Windows 11: Komplett-Kit (Download, Installation, Deinstallation)
1. [Python 3.10+ herunterladen](https://www.python.org/downloads/) und bei der Installation **"Add Python to PATH"** anhaken.
2. Dieses Projekt als ZIP herunterladen: Auf GitHub oben rechts auf **Code → Download ZIP** klicken und die Datei entpacken (ein Ordner genügt).
3. `bot.py` per Doppelklick (mit zugeordnetem Python 3.10+) oder im Terminal mit `py -3.10 bot.py`/`python bot.py` starten – das Fenster zeigt sofort eine Schritt-für-Schritt-Anleitung.
4. Im Fenster auf **Installieren** klicken: Die benötigten Pakete werden automatisch installiert.
5. Minecraft starten, gemäß Abschnitt [Startposition](#startposition--starting-position) aufstellen und das Fenster im Vordergrund lassen.
6. Im Bot-Fenster **Start** drücken. Der Bot läuft, bis du irgendeine Taste drückst oder den **Stopp**-Button nutzt.
7. Deinstallation: Im Bot-Fenster **Deinstallieren** drücken, danach den entpackten Ordner löschen. Damit bleiben keine Reste zurück.

## Nutzung
1. Starte Minecraft und stelle dich wie in [Startposition](#startposition--starting-position) beschrieben auf.
2. Führe den Bot aus:
   ```bash
   python bot.py
   ```
3. Klicke bei Bedarf im Fenster auf **Installieren**, falls die Abhängigkeiten noch fehlen.
4. Trage bei Bedarf Zeiten an:
   * Countdown vor Start (Standard 3 s)
   * Haltedauer zum Abbauen
   * Schritt-Dauer je Block
   * kurze Pause nach dem Pflanzen
5. Klicke **Start**. Ein Countdown läuft, dann wird automatisch:
   * nach unten geschaut,
   * jeder Acker-Block (außer Wasser bei 5,5) abgebaut,
   * sofort wieder eine Kartoffel gepflanzt,
   * blockweise in Schlangenlinien über das Feld gelaufen, inklusive Rückweg zur Startposition.
6. Eine **beliebige Taste** (oder der **Stopp**-Button) unterbricht den Bot sofort.
