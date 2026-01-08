# Minecraft-Bedrock-auto-Bot
Ein kleines Desktop-Tool (kein Client-Hack, keine Injection), das über Tastatur- und Maus-Eingaben das automatische Abbauen und Neu-Pflanzen von Kartoffeln auf einem 9x9 Feld in Minecraft Bedrock Edition ermöglicht. Das Feld besitzt in der Mitte ein Wasserloch (5,5), das der Bot umgeht.

## Voraussetzungen
* Python 3.10+
* Tkinter-Unterstützung für Python (unter Linux z. B. `python3-tk` nachinstallieren)
* Abhängigkeiten installieren:
  ```bash
  pip install -r requirements.txt
  ```

## Nutzung
1. Starte Minecraft, stelle dich links unten am Feld auf (Position 0,1), schaue geradeaus und halte Kartoffeln in der Hand.
2. Führe den Bot aus:
   ```bash
   python bot.py
   ```
3. Trage bei Bedarf Zeiten an:
   * Countdown vor Start (Standard 3 s)
   * Haltedauer zum Abbauen
   * Schritt-Dauer je Block
   * kurze Pause nach dem Pflanzen
4. Klicke **Start**. Ein Countdown läuft, dann wird automatisch:
   * nach unten geschaut,
   * jeder Acker-Block (außer Wasser bei 5,5) abgebaut,
   * sofort wieder eine Kartoffel gepflanzt,
   * blockweise in Schlangenlinien über das Feld gelaufen, inklusive Rückweg zur Startposition.
5. Mit der Taste **M** (oder dem **Stopp**-Button) wird der Bot sofort unterbrochen.
