# Sekcja Krzemowa

Wtyczki MIDI (VST3, macOS Apple Silicon i Intel) dla REAPER-a, które grają
z bębniarzem na żywo:

- **Sekcja Krzemowa Piano** słucha bębnów (stopa, werbel, 2 tomy, hi-hat) i gra
  popowy akompaniament fortepianu. To ona jest liderem harmonii: tu wybierasz
  tonację i progresje akordów zwrotki i refrenu.
- **Sekcja Krzemowa Bass** słucha bębnów **i** instancji Piano. Progresję
  pobiera automatycznie, trzyma się stopy i dopełnia rytm Piano.
- **Sekcja Krzemowa Gitara** słucha bębnów, Piano **i** Bass i gra popowe
  melodie i zagrywki gitary prowadzącej na instrumencie Kontakt (domyślnie
  Session Guitarist **Electric Neon** w trybie Melody): slajdy, bendy,
  hammer-ony / pull-offy, wibrato i zjazdy na końcu fraz. Szczegóły niżej
  w sekcji [Gitara](#gitara-kontakt-session-guitarist-electric-neon).

Tempo i metrum pochodzą z projektu REAPER-a (bębniarz gra do klika).
Wtyczki nie dodają opóźnienia (latencja 0 próbek).

![Piano](docs/gui-piano.png)

## Pobranie

Każdy push na `main` buduje wtyczki w GitHub Actions (macOS 14, Apple Silicon,
plik universal arm64 + x86_64). Gotowy plik `SekcjaKrzemowa-macOS.zip` znajdziesz
w zakładce **Actions → Build macOS → Artifacts** (a po otagowaniu `v*` także w
**Releases**). Instalacja: [docs/INSTALACJA.txt](docs/INSTALACJA.txt).

## REAPER krok po kroku

1. **Ścieżka „Bębny”**: wejście MIDI z perkusji elektronicznej, uzbrojona do
   nagrywania z monitoringiem, na niej Twój instrument perkusyjny.
2. **Ścieżka „Piano”**: FX: `Sekcja Krzemowa Piano` → Twój instrument piano.
3. **Ścieżka „Bass”**: FX: `Sekcja Krzemowa Bass` → Twój instrument basowy.
4. Na ścieżce Bębny dodaj **wysyłki (Sends)** do Piano i do Bass:
   audio `None`, MIDI `All → All`.
5. Włącz metronom i transport. Instrumenty wchodzą na najbliższym bicie po
   tym, jak bębniarz zacznie grać.
6. Jeśli Twoja perkusja wysyła inne numery nut niż General MIDI, ustaw je w
   panelu **Bębny i wyjście** (stopa, werbel, tom 1, tom 2, hi-hat). Aliasy GM
   (np. 35/36 stopa, 42/44/46 hi-hat, 49/57 crash) są rozpoznawane zawsze.
   Dla **GGD Modern & Massive 2** wybierz w polu **Mapa** gotową mapę
   „GGD Modern & Massive 2” (toms 42–48, hi-hat 49–57, ride liczony jak hi-hat,
   choke'i ignorowane); ręczne numery nut są wtedy wyłączone.
7. Piano i Bass muszą mieć ten sam **Link** (Zespół 1–4). Kilka zespołów w
   jednym projekcie nie przeszkadza sobie nawzajem.

Uwaga: REAPER domyślnie przetwarza ścieżki „z wyprzedzeniem” (anticipative FX).
Ścieżki zasilane z uzbrojonej ścieżki z monitoringiem powinny iść w czasie
rzeczywistym. Jeśli mimo to usłyszysz opóźnienie reakcji, uzbrój także ścieżki
Piano i Bass z monitoringiem (wejście MIDI może być dowolne lub żadne).

## Gitara (Kontakt, Session Guitarist Electric Neon)

![Gitara](docs/gui-gitara.png)

**W REAPER-ze:**

1. **Ścieżka „Gitara”**: FX: `Sekcja Krzemowa Gitara` → `Kontakt` z
   **Electric Neon (Melody)** (w Essentials: instrument z pojedynczymi nutami,
   nie pattern). Wtyczka Gitara musi stać *przed* Kontaktem.
2. Na ścieżce Bębny dodaj wysyłkę do Gitary (audio `None`, MIDI `All → All`),
   tak jak do Piano i Bass.
3. Gitara musi mieć ten sam **Link** (Zespół) co Piano i Bass. Tonacji i
   progresji nie ustawia się w Gitarze, tylko w Piano.
4. W Kontakcie przełącz Electric Neon na **Mono** (płynne przejścia legato)
   i sprawdź zakres pitch bend instrumentu. Tę samą liczbę półtonów ustaw w
   polu **Bend ±** (domyślnie 2).

**Artykulacje i keyswitche** (nazwy jak w Kontakcie, C3 = 60, więc C1 = 36):

| Co gra gitara | Jak to wysyła | Electric Neon |
| --- | --- | --- |
| zwykła nuta | keyswitch **Open** | C1 (36) |
| ciche, gęste 16-tki przy spokojnym bębniarzu | keyswitch **Mute** (palm mute) | C#1 (37) |
| slajd do następnej nuty (skok 2–7 półtonów) | **Slajd** trzymany między nutami + nuty zachodzą | B1 (47) |
| hammer-on / pull-off (skok do tercji małej) | nuty zachodzą, druga ~40 % ciszej | automatycznie |
| zjazd na końcu frazy (wysoka nuta w dół, niska w górę) | **Zjazd ↓ / ↑** w chwili końca nuty | A1 (45) / G#1 (44) |
| bend do dźwięku akordu | nuta o 2 półtony niżej + pitch bend w górę | pitch wheel |
| wibrato na długich nutach | CC1 (mod wheel) narasta od połowy nuty | mod wheel |

Każdy keyswitch można zmienić w panelu **Kontakt, bębny i wyjście**. Z innym
instrumentem wybierz **Instrument → Ogólny**: wtedy idą tylko nuty, pitch bend
i CC1, a slajdy zastępuje legato. Melodia nigdy nie schodzi do strefy
keyswitchy (pole **Najniższa**, domyślnie E3 = 52).

Uwaga: mapa keyswitchy pochodzi z instrukcji pełnej wersji Electric Neon
(Melody). Instrukcja Essentials nie podaje osobnej mapy, więc jeśli Twoja
wersja ma inne klawisze, sprawdź je na klawiaturze Kontaktu (kolorowe
klawisze) i przestaw pola we wtyczce.

**Jak gra Gitara:**

- Myśli **frazami po dwa takty**. W roli **Hook** pierwsza fraza sekcji to
  motyw, który wraca co cztery takty (tak zapamiętuje się melodię), a co drugą
  frazę gra odpowiedź. Gałka **Wariacje** decyduje, jak często motyw się zmienia.
- **Wypełnienia**: dwa takty ciszy, dwa takty gry (miejsce dla wokalu).
  **Solo**: frazy bez przerw, wyższa energia i więcej ozdobników.
- Każdą nutę przenosi na akord bieżącego taktu z gwarancją braku zgrzytów:
  tylko składniki akordu albo dźwięki skali, które nie leżą półton od żadnego
  składnika (`remapInterval`, ta sama reguła co w Piano i Bass). Test jamu
  sprawdza co 1/32 nuty, że gitara nie tworzy sekundy małej ani nony małej
  z żadną nutą Piano i Bass.
- **Miejsce** (dla Piano): wybiera frazy, które trafiają w pauzy rytmu Piano
  i rzadziej w nuty basu.
- **Ozdobniki**: ile slajdów, bendów, hammer-onów, wibrata i zjazdów.
- Gdy bębniarz gra przejście, gitara nie zaczyna nowych nut. Po przejściu
  (zmiana zwrotka ↔ refren) zaczyna motyw nowej sekcji.

**Biblioteka melodii.** `Source/Core/GuitarPhraseData.h` generuje
`tools/extract_guitar.py` z **POP909**: 909 popularnych piosenek pop, ścieżka
MELODY (linia wokalu, czyli właściwe melodie zwrotek i refrenów) i BRIDGE
(instrumentalne kontrmelodie i wstawki, najbardziej „gitarowy” materiał).
Wybrano 630 dwutaktowych fraz z jednym akordem w takcie, z pierwszeństwem dla
fraz powtarzanych w piosence (hooki refrenów), plus 12 ręcznie napisanych
idiomów popowej gitary (pentatonika, seksty, zawieszenia, ósemki „na delay”).
Opcja `--lakh` dodaje frazy z prawdziwych ścieżek gitary prowadzącej z Lakh
MIDI (filtr artystów pop). To materiał chroniony prawem autorskim, więc tylko
do użytku prywatnego.

```bash
python3 tools/extract_guitar.py --pop909 POP909-Dataset/POP909 [--lakh ~/lmd/clean_midi]
```

**Brak pętli sprzężenia zwrotnego.** Gitara tylko słucha: czyta z linku
harmonię, decyzję taktu, rytm i energię Piano oraz rytm Bass, a sama nic nie
publikuje. Piano i Bass grają tak samo z Gitarą i bez niej.

## Jak gra

| Bębniarz robi | Sekcja reaguje |
| --- | --- |
| gęstszy hi-hat, mocniejszy werbel, więcej uderzeń | w następnym takcie wybiera gęstsze, mocniejsze licki |
| gra ciszej | niższa dynamika nut od razu (bieżące uderzenia) |
| gra przed / za bitem | nuty przesuwają się tak samo (gałka **Feel**) |
| przejście na tomach w drugiej połowie taktu | Piano milknie do końca taktu, Bass gra tylko na bitach |
| przejście na tomach | od następnego taktu zmiana sekcji: zwrotka ↔ refren, akcent na „raz” |
| takt ciszy | sekcja trzyma akord |
| dłuższa cisza | sekcja czeka i wchodzi razem z bębniarzem |
| wzór stopy | Bass wybiera licki trafiające w stopę (gałka **Trzymaj stopę**; od 75 % dogrywa prymę na każdą stopę) |

Gałki: **Reaktywność** (jak szybko energia podąża za bębniarzem), **Wariacje**
(jak często zmieniać lick: co 4, 2 lub 1 takt, i ile losowości), **Dynamika**,
**Feel**, **Gęstość** (przesunięcie energii), **Rejestr** (Piano),
**Oktawa** i **Trzymaj stopę** (Bass).

## Architektura

```
 e-perkusja ──MIDI──► ścieżka Bębny ──send──┬──► [Sekcja Krzemowa Piano] ──MIDI──► instrument piano
                                            │          │  publikuje: progresję, sekcję i akord taktu,
                                            │          │  rytm swojego taktu, energię
                                            │          ▼
                                            │   pamięć współdzielona (POSIX shm, bez blokad)
                                            │          │  ▲ Bass publikuje tylko „jestem”
                                            │          ▼  │
                                            └──► [Sekcja Krzemowa Bass] ──MIDI──► instrument basowy
```

Każda instancja w każdym bloku audio:

1. Zamienia uderzenia bębnów na pozycje w takcie (ppq z transportu hosta) i
   liczy cechy taktu: gęstość hi-hatu, wzór stopy, siłę werbla, tomy, odchylenie
   od siatki (`Source/Core/DrumAnalyzer.h`).
2. Na granicy taktu zamyka poprzedni takt i decyduje o następnym: akord z
   progresji, sekcja, lick dobrany do energii bębniarza
   (`Source/Core/Engine.cpp`).
3. Lick (jeden takt w siatce 16-tek) jest przenoszony na bieżący akord
   z gwarancją braku zgrzytów: każda nuta to składnik akordu albo dźwięk skali,
   który nie leży półton od składnika (`remapInterval` w `Harmony.h`).
4. Nuty są wysyłane z dokładnością do próbki, bo przyszłe pozycje siatki znamy
   z transportu. Nie ma bufora wyprzedzenia, więc nie ma dodatkowej latencji.

Wszystko w wątku audio jest bez alokacji i bez blokad.

### Dlaczego licki z danych, a nie model ML w czasie rzeczywistym

Model generatywny (np. transformer na Lakh/Groove MIDI) dawałby więcej
różnorodności, ale kosztem nieprzewidywalnego czasu inferencji, ryzyka
„dziwnych” nut i trudnej kontroli harmonii między dwoma instrumentami.
Tutaj „wiedza” z danych jest wyekstrahowana offline do biblioteki licków
(prawdziwe popowe frazy z POP909), a w czasie rzeczywistym działa tylko wybór
i transpozycja, czyli stały, mikrosekundowy koszt. Harmonia jest
deterministyczna (wspólna progresja), więc dwa instrumenty nie mogą się
rozjechać.

### Dlaczego nie ma pętli sprzężenia zwrotnego

- Przepływ jest jednostronny: Piano → Bass. Piano nie słucha nut basu, wie
  tylko, że Bass istnieje (wtedy nie schodzi poniżej E3).
- Decyzje zapadają raz na takt i opierają się na **zamkniętym** poprzednim
  takcie, nigdy na tym, co druga instancja zagrała przed chwilą.
- Gdy Bass zdecyduje o takcie przed Piano (REAPER przetwarza ścieżki
  równolegle), przewiduje sekcję tą samą regułą z tych samych bębnów, a jeśli
  Piano opublikuje inną decyzję w pierwszej połowie taktu, Bass się poprawia.

## Biblioteka licków

`Source/Core/LickLibraryData.h` jest generowany przez
`tools/extract_licks.py` z **POP909** (909 popowych piosenek z adnotacją
akordów i bitów, licencja MIT). Z 31 049 taktów 4/4 z jednym akordem wybrano
644 licki piano i 451 licków basu, równomiernie rozłożonych po energii,
z deduplikacją i limitem licków z jednej piosenki. POP909 nie ma ścieżki basu,
więc linia basu pochodzi z najniższych dźwięków lewej ręki aranżacji
fortepianowej.

Skrypt obsługuje też **Lakh MIDI** (`clean_midi`, filtr artystów pop,
prawdziwe ścieżki basowe GM 32–39, akordy wnioskowane z pozostałych ścieżek).
Lakh zawiera materiał chroniony prawem autorskim, więc taką bibliotekę traktuj
jako prywatną i nie rozpowszechniaj zbudowanych z nią wtyczek.

```bash
pip install mido
git clone https://github.com/music-x-lab/POP909-Dataset
python3 tools/extract_licks.py --pop909 POP909-Dataset/POP909 [--lakh ~/lmd/clean_midi]
```

## Budowanie samodzielnie

Wymagania: CMake ≥ 3.22, Xcode (Command Line Tools). JUCE 8.0.4 pobiera się
samo.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DSK_COPY_PLUGINS=ON
cmake --build build --config Release --parallel
./build/sk_core_tests
./build/sk_guitar_tests
```

Opcja `-DSK_BUILD_HOST_TEST=ON` buduje dodatkowo test ładujący VST3 w hoście
JUCE i narzędzie do zrzutów ekranu edytora.

## Struktura

```
Source/Core/     rdzeń bez JUCE: Engine, GuitarEngine, DrumAnalyzer, Harmony, SharedLink, biblioteki licków i fraz
Source/Plugin/   procesor i edytor JUCE Piano/Bass (wspólny kod, rola z SK_ROLE_BASS)
Source/Guitar/   procesor i edytor JUCE Gitary
Resources/       grafiki istot krzemowych (SVG)
tools/           ekstrakcja licków i fraz gitary, generator grafik
tests/           testy rdzenia (symulacja jamu), test w hoście, zrzuty GUI
```

## Ograniczenia i dalsze kroki

- Licki są jednotaktowe w 4/4. Inne metrum działa (licki są przycinane lub
  powtarzane), ale brzmi najlepiej w 4/4.
- Jeden akord na takt.
- Planowane: tryb „podążaj za bębniarzem” (wykrywanie tempa zamiast klika).
