encfsy
======

🌐 **Sprache**: [English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Русский](README.ru.md) | [العربية](README.ar.md) | [Deutsch](README.de.md)

---

## Über encfsy
encfsy ist eine Windows-Implementierung von EncFS, die auf **Dokany** und **Crypto++** basiert.
Es läuft **ausschließlich auf 64-Bit-Systemen**.

Das Programm verschlüsselt Dateinamen und -inhalte, während die Verzeichnisbaumstruktur erhalten bleibt.
Dies macht es ideal für die Synchronisierung verschlüsselter Daten mit Dropbox, Google Drive, rsync oder anderen Remote-Speichern: Die Dateien bleiben Ende-zu-Ende verschlüsselt, sodass Speicheradministratoren deren Inhalte nicht einsehen können.

## ⚠️ Bei bidirektionaler Cloud-Synchronisation kein `--paranoia` verwenden
Im Modus `--paranoia` (externe IV-Verkettung) führen Umbenennungen durch Sync-Tools dazu, dass der Dateinhalt **dauerhaft unlesbarer Müll** wird, selbst wenn Sie die Namen manuell zurücksetzen.
- Für bidirektionale Syncs (Dropbox/OneDrive/Google Drive usw.) **`--paranoia` deaktivieren**.
- Wenn unbedingt nötig, nur für einseitige Backups einsetzen, bei denen sich Dateinamen nie ändern.

## Cloud-Konfliktdatei-Behandlung

Bei der Verwendung von Cloud-Speicherdiensten (Dropbox, Google Drive, OneDrive) können Synchronisierungskonflikte Dateien mit speziellen Suffixen erstellen, die nicht normal entschlüsselt werden können. Die Option `--cloud-conflict` aktiviert die Erkennung und Behandlung dieser Konfliktdateien.

**Unterstützte Konfliktmuster:**
- Dropbox: `Dateiname (Konflikt-Kopie von Computer 2024-01-01).ext`
- Google Drive: `Dateiname_conf(1).ext`

**Verwendung:**
```bash
encfs.exe C:\Data M: --cloud-conflict
```

**Hinweis:** Diese Option ist standardmäßig deaktiviert, da die Konflikterkennung einen geringen Leistungseinfluss haben kann und nur bei Verwendung von Cloud-Sync-Diensten benötigt wird.

## Ungültige Dateinamen scannen

Die Option `--scan-invalid` durchsucht das verschlüsselte Verzeichnis und meldet Dateinamen, die nicht entschlüsselt werden können. Die Ergebnisse werden im JSON-Format ausgegeben.

**Verwendung:**
```bash
encfs.exe C:\encrypted --scan-invalid
encfs.exe C:\encrypted --scan-invalid --cloud-conflict  # Mit Cloud-Konflikterkennung scannen
```

**JSON-Ausgabeformat:**
```json
{
  "invalidFiles": [
    {
      "fileName": "verschlüsselterDateiname",
      "encodedParentPath": "encDir1\\encDir2",
      "decodedParentPath": "dir1\\dir2"
    }
  ],
  "totalCount": 1
}
```

## Sicherheitsfunktionen
encfsy verwendet die **Windows-Anmeldeinformationsverwaltung** für sichere Passwortverwaltung.

- Passwörter werden mit **DPAPI** (Data Protection API) verschlüsselt und an das aktuelle Benutzerkonto gebunden
- Eliminiert die Notwendigkeit, Passwörter über stdin zwischen GUI und encfs.exe zu übergeben, wodurch Abfangrisiken beseitigt werden
- Die Option „Passwort merken" speichert Passwörter für die automatische Eingabe beim nächsten Start
- Passwörter werden **separat für jedes verschlüsselte Verzeichnis (rootDir) gespeichert**

### Wo Passwörter gespeichert werden
Gespeicherte Passwörter können in Systemsteuerung → Anmeldeinformationsverwaltung → Windows-Anmeldeinformationen eingesehen werden.
Sie erscheinen mit Namen wie `EncFSy:c:\path\to\encrypted`.

## GUI-Verwendung
Verwenden Sie **encfsw.exe**, um Volumes einfach ohne Befehlszeile ein- und auszuhängen.

1. Wählen Sie das verschlüsselte Verzeichnis (rootDir)
2. Wählen Sie einen Laufwerksbuchstaben zum Einhängen
3. Geben Sie Ihr Passwort ein (aktivieren Sie „Remember Password" zum Speichern)
4. Klicken Sie auf „Mount"

„Show Advanced Options" bietet Zugriff auf dieselben detaillierten Einstellungen wie die Befehlszeilenversion.

## Verwendung der Anmeldeinformationsverwaltung über die Befehlszeile
Wenn Sie mit aktivierter Option „Remember Password" in der GUI einhängen, wird das Passwort in der Windows-Anmeldeinformationsverwaltung gespeichert.
Sie können dann von der Befehlszeile aus ohne Passworteingabe einhängen, indem Sie die Option `--use-credential` verwenden.

```bash
# 1. Zuerst über GUI mit aktivierter Option „Remember Password" einhängen
#    → Passwort wird in der Anmeldeinformationsverwaltung gespeichert

# 2. Anschließend kann von der Befehlszeile ohne Passwortabfrage eingehängt werden
encfs.exe C:\Data M: --use-credential
```

## Dateinamenlängenbeschränkung
encfsy verwendet die moderne *Long-Path*-API, sodass die traditionelle 260-Zeichen-**MAX_PATH**-Beschränkung für vollständige Pfade **nicht gilt**.

NTFS beschränkt jedoch weiterhin jede Pfadkomponente (Ordner- oder Dateiname) auf **255 UTF-16-Zeichen**.
Da die Verschlüsselung Namen um etwa 30 % verlängert, halten Sie **jeden Dateinamen unter 175 Zeichen**, um innerhalb dieser Komponentenbeschränkung zu bleiben und die Kompatibilität mit Tools zu gewährleisten, die keine langen Pfade unterstützen.

## Verwendung

```
Verwendung: encfs.exe [Optionen] <rootDir> <mountPoint>

Argumente:
  rootDir      (z.B. C:\test)               Zu verschlüsselndes und einzuhängendes Verzeichnis
  mountPoint   (z.B. M: oder C:\mount\dokan) Einhängepunkt - Laufwerksbuchstabe (z.B. M:\)
                                             oder leerer NTFS-Ordner

Optionen:
  -u <mountPoint>                              Angegebenes Volume aushängen
  -l                                           Aktuell eingehängte Dokan-Volumes auflisten
  -v                                           Debug-Ausgabe an Debugger senden
  -s                                           Debug-Ausgabe an stderr senden
  -i <ms>              (Standard: 120000)      Zeitüberschreitung (ms) bis zum Abbruch der Operation
                                               und Aushängen des Volumes
  --use-credential                             Passwort aus Windows-Anmeldeinformationsverwaltung lesen
                                               (Passwort wird gespeichert gehalten)
                                               Hinweis: Passwort muss zuerst über GUI mit aktivierter
                                               Option „Remember Password" gespeichert werden
  --use-credential-once                        Passwort aus Windows-Anmeldeinformationsverwaltung lesen
                                               (nach dem Lesen löschen, einmalige Verwendung)
  --scan-invalid                               Verschlüsseltes Verzeichnis scannen und ungültige
                                               Dateinamen melden. Ausgabe im JSON-Format
  --dokan-debug                                Dokan-Debug-Ausgabe aktivieren
  --dokan-network <UNC>                        UNC-Pfad für Netzwerk-Volume (z.B. \\host\myfs)
  --dokan-removable                            Volume als Wechselmedium anzeigen
  --dokan-write-protect                        Dateisystem schreibgeschützt einhängen
  --dokan-mount-manager                        Volume beim Windows-Bereitstellungs-Manager registrieren
                                               (aktiviert Papierkorb-Unterstützung usw.)
  --dokan-current-session                      Volume nur in aktueller Sitzung sichtbar machen
  --dokan-filelock-user-mode                   LockFile/UnlockFile im Benutzermodus behandeln;
                                               andernfalls verwaltet Dokan sie automatisch
  --dokan-enable-unmount-network-drive         Aushängen von Netzlaufwerken über Explorer erlauben
  --dokan-dispatch-driver-logs                 Kernel-Treiber-Logs an Userland weiterleiten (langsam)
  --dokan-allow-ipc-batching                   IPC-Batching für langsame Dateisysteme aktivieren
                                               (z.B. Remote-Speicher)
  --public                                     Aufrufenden Benutzer beim Öffnen von Handles in
                                               CreateFile imitieren. Erfordert Administratorrechte
  --allocation-unit-size <Bytes>               Vom Volume gemeldete Zuordnungseinheitsgröße
  --sector-size <Bytes>                        Vom Volume gemeldete Sektorgröße
  --volume-name <Name>                         Im Explorer angezeigter Volume-Name (Standard: EncFS)
  --volume-serial <hex>                        Volume-Seriennummer in Hex (Standard: vom Basis)
  --paranoia                                   AES-256-Verschlüsselung, umbenannte IVs und
                                               externe IV-Verkettung aktivieren
  --alt-stream                                 NTFS-alternative Datenströme aktivieren
  --case-insensitive                           Dateinamenabgleich ohne Groß-/Kleinschreibung durchführen
  --cloud-conflict                             Cloud-Konfliktdatei-Behandlung aktivieren
                                               (Dropbox, Google Drive, OneDrive). Standard: deaktiviert
  --reverse                                    Umkehrmodus: Klartext-rootDir verschlüsselt
                                               am mountPoint anzeigen

Beispiele:
  encfs.exe C:\Users M:                                    # C:\Users als Laufwerk M:\ einhängen
  encfs.exe C:\Users C:\mount\dokan                        # In NTFS-Ordner C:\mount\dokan einhängen
  encfs.exe C:\Users M: --dokan-network \\myfs\share       # Als Netzlaufwerk mit UNC \\myfs\share
  encfs.exe C:\Data M: --volume-name "Sicheres Laufwerk"   # Mit benutzerdefiniertem Volume-Namen
  encfs.exe C:\Data M: --use-credential                    # Gespeichertes Passwort aus der Verwaltung verwenden
  encfs.exe C:\Data M: --cloud-conflict                    # Mit Cloud-Konflikt-Unterstützung einhängen
  encfs.exe C:\encrypted --scan-invalid                    # Ungültige Dateinamen scannen (JSON-Ausgabe)

Zum Aushängen drücken Sie Ctrl+C in dieser Konsole oder führen Sie aus:
  encfs.exe -u <mountPoint>
```

## Installation
1. Installieren Sie **Dokany** (≥ 2.0) — Download von den [offiziellen Releases](https://github.com/dokan-dev/dokany/releases)
2. Laden Sie den neuesten **encfsy-Installer** von der [Releases-Seite](https://github.com/mimidesunya/encfsy/releases) herunter und folgen Sie dem Setup-Assistenten

## Lizenz
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## Autor
[Mimi](https://github.com/mimidesunya) ｜ [X @mimidesunya](https://twitter.com/mimidesunya)
