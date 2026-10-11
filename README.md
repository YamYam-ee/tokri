<img src="./resources/com.ysuraj.Tokri.png" width="96" height="96" alt="Tokri">

# Tokri – Eriks version

En korg på skrivbordet för att dra och släppa filer, text, länkar och bilder –
byggd för att samla ihop dokument, ordna dem och skicka vidare.

Det här är en vidareutveckling av [Tokri](https://github.com/ysuraj1999/tokri)
av Suraj Yadav, med ett antal funktioner för vardagligt dokumentarbete på
Windows. Originalets README finns i [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md).

**[📖 Användarguide med bilder](https://yamyam-ee.github.io/tokri/)** – så använder du alla funktioner.

## Ladda ner

**[⬇ Senaste versionen (MSI)](../../releases/latest)** – dubbelklicka på
`Tokri-x.y.z.msi`.

- Installeras för din användare (ingen administratör behövs) i
  `%LOCALAPPDATA%\Programs\Tokri-egen`
- Startmenygenväg och autostart vid inloggning
- Avinstallera via *Inställningar → Appar*
- Programmet är inte digitalt signerat, så Windows SmartScreen kan varna:
  klicka **Mer info → Kör ändå**

## Funktioner

**Korgen**
- Kompakt listvy med en rad per fil; fönstret växer med listan upp till skärmkanten
- Filer som läggs in tillsammans sorteras efter namn i nummerordning
  (1, 2, 10 …); separata släpp hamnar i den ordning de lades in
- Dra rader för att sortera manuellt – ett streck visar var de landar
  (Ctrl/Shift-klick för flera)
- Räknare: *12 filer · 5 avbockade*, och en röd **Rensa**-knapp

**Leverans**
- Att dra ut en fil kopierar den – den ligger kvar i korgen
- Utdragna filer bockas av och raden blir grön; klicka på bocken (eller
  konturen av den) för att bocka av/ta bort bocken

**Flikar**
- Olika samlingar i olika flikar, som går att döpa (dubbelklicka) och flytta
- Släpp filer på **+** för att lägga dem i en ny flik
- Håll en fil över en flik under en dragning för att byta till den

**Förhandsvisning**
- Ögat på varje rad eller **mellanslag**
- PDF och bilder: zooma med scrollhjulet där muspekaren är, dra för att flytta
- Word, Excel, Outlook-mejl m.m. via Windows egna förhandsvisare
- Stora genomskinliga pilar för föregående/nästa; klick i bilden stänger

**PDF**
- Markera flera PDF:er → högerklick → **Slå ihop** – de sätts ihop i
  listordning till en ny fil längst ner (originalen hamnar i papperskorgen).
  Förlustfritt via [qpdf](https://github.com/qpdf/qpdf).

**Övrigt**
- Dra in bilagor och mejl direkt från Outlook (klassisk)

## Klona och bygga

```bash
git clone https://github.com/YamYam-ee/tokri.git
```

Krav (Windows):
- Visual Studio 2022 Build Tools med C++
- [Qt 6.8](https://www.qt.io/) för MSVC 2022 x64 med modulen **Qt PDF**
  (t.ex. `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtpdf`)
- CMake och Ninja
- [qpdf](https://github.com/qpdf/qpdf/releases) (msvc64-zip) för PDF-sammanslagning
- För MSI: [WiX Toolset 5](https://wixtoolset.org/)
  (`dotnet tool install --global wix --version 5.0.2` och
  `wix extension add -g WixToolset.Util.wixext/5.0.2`)

Bygg och paketera från en *x64 Native Tools*-prompt:

```powershell
.\installer\build-msi.ps1 -Version 1.0.0 -QtDir C:\Qt\6.8.3\msvc2022_64 -QpdfDir C:\path\to\qpdf-12.4.2-msvc64
```

Bara bygga:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
```

## Licens

MIT, se [LICENSE.md](LICENSE.md). Original © Suraj Yadav.
qpdf ingår i installationen under Apache-2.0-licensen.
