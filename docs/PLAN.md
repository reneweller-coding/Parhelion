# Parhelion: Plan für den Trance-Generator

Stand 29.09.2026, Entwurf nach den ersten Entscheidungen des Nutzers (Abschnitt 16). Name **Parhelion**
(Entscheidung 16.1), Repo `G:\Tools\VRAudio\TranceGenerator` (eigenes Projekt, nicht in Noctuary integriert),
Namensraum `parh::`, Präfix `PARH_`, Werkzeuge `parh_render`, `parh_selftest`, Set-Datei `.parhset`. Code-Kommentare im
Doxygen-Format wie bei den Geschwistern; GUI und Handbuch auf Englisch, Plan und Journal auf Deutsch.

Der Name: Eine Nebensonne (Parhelion) entsteht, wenn Sonnenlicht durch waagerecht schwebende Eisplättchen fällt: links und
rechts der Sonne stehen zwei helle, oft farbige Lichtflecken. Das ist fast wörtlich der Klang, auf dem Trance steht: der
Supersaw des JP-8000, ein Oszillator in der Mitte und sechs verstimmte daneben, die mit wachsendem Detune heller werden
(Dok. 5). Die Geschwister heißen Noctuary, Phosphene, Ephemeris und Totality; Parhelion gehört in dieselbe Familie der
Himmels- und Lichtwörter. Eine erste Suche fand kein Audio-Produkt dieses Namens; vor dem Release wird das gründlicher
geprüft (Risiko 8, die Lehre aus Umbra).

**Grundlagen dieses Entwurfs.** Das Recherchedokument des Nutzers (`docs/research/Trance-Analyse-2026-09-29.pdf`, im
Folgenden **Dok.** mit Kapitelnummer: 1 Kernaussagen, 2 Genre-Landschaft, 3 Rhythmus, Tempo und Groove, 4 Harmonik und
Melodik, 5 Sound Design, 6 Arrangement und Form, 7 Mixing und Mastering, 8 Drei Paradigmen, 9 SOTA-Literatur,
10 Blueprint; das Dokument verweist selbst so auf 5, 9 und 10), der Code und die Pläne von Noctuary
(`G:\Tools\VRAudio\AmbientSynth`, 7a48fdd), Phosphene (`PsytranceGenerator`, 76f7100), Ephemeris
(`BerlinSchoolGenerator`, d047d79) und Totality (`TechnoGenerator`, 4d3c0d2), eine Messung des lokalen Trance-MIDI-Korpus
(2.9) und ein Blick auf das lokale Referenzaudio (13.4). Zahlen tragen die Kennzeichnung der Geschwister: **[Q]** direkt
aus einer im Dokument zitierten Quelle, **[A]** daraus abgeleitet, **[I]** inferiert und zu kalibrieren, **[M]** hier
gemessen. Was dieser Plan neu vorschlägt, ist [I], bis es gemessen ist.

## Stand der Umsetzung

Die Entscheidungen stehen (16.1, 16.2). **Phasen 0 bis 6 gebaut (29.09.2026); als nächstes 7, die Quest.**

- **Phase 0, das Gerüst.** Modulkopie mit Herkunftsnotiz im Dateikopf: aus Totality (4d3c0d2) Vec, Dsp, Adaa, Halfband,
  Oversample, Clock, WavWriter, Loudness, Midi, Cue, der Parameterspeicher, Score, Deck, Engine, Kick, SubBass, der
  Mono-Synth, das Kit mit PercKernel, die Schaltungsfilter, Ducker, Plate, Reverb, TapeEcho, Dynamics, GestureEngine,
  Test-Gerüst und Renderer; aus Phosphene (76f7100) Poly und PolyKernel (Supersaw nach Szabo, VA, FM, Wavetable,
  zweiter Oszillator, Modulationsmatrix), WaveTable und WaveTableFile, Modulation, Disperser, TempoDelay, TranceGate,
  Util. Alle Dateien LF. Neu: die Parametertabellen compose, pump, sends und die Poly-Instanzen mit Namen (lead,
  counter, pluck, arp, pad, stab), die Partitur mit Sektionen, Layer-Matrix je 8-Takt-Block und der Ghost-Kick als
  eigene Spur (MIDI-Kanal 16), die Cue-Marken aus Sektionen und Layer-Wechseln, `parh_render` mit Lautheit je Sektion.
- **Phase 1, ein Loop, der pumpt.** Das Deck: Kick (auf die Tonart gestimmt), Sinus-Sub und Mid-Bass auf den
  Offbeat-Achteln, 303 (noch ohne Einsatz), zwölf Kit-Rollen für Trance (CH, OH, Clap, Snare, Ride, Crash, Shaker,
  Tambourine, Conga, Tom, Rim, Noise), sechs Poly-Stimmen mit Trance-Gate (Phosphenes Muster plus drei 16tel-Masken,
  x.xx.x.xx.x.x.x. und Verwandte), der **Pump** (Ghost-Kick triggert je Stimme einen Ducker in dB nach
  pump.attack/hold/release, dazu Sub, Mid-Bass und die Returns), drei Räume (Room-FDN, Dattorro-Plate, Hall-FDN) mit
  Hochpass an den Send-Eingängen und geduckten Returns, ein Synth-Bus-Fader (mix.synth_level) für das
  Energie-Drehbuch, Track-Bus mit Glue und Trim, der Master aus Totality. Dazu die **Studie** (compose/Study.h):
  104 Takte in der Form von Dok. 6 mit Harmonik i–VI–III–VII, Pad-Voicing mit Terz oder Quinte unten, Pluck im
  3-3-3-3-2-2-Rhythmus, Arp im zweiten Drop-Teil, einer Lead-Attrappe (Motiv mit Sprung, Abstieg pentatonisch,
  A A' B A''), Snare-Roll im Build, leerem letzten Schlag vor dem Drop und Automation: Duck im Breakdown auf null, zwei
  Takte vor dem Drop zurück; Pad-Filter öffnet über den Breakdown; Gate im Drop; Hall lang im Breakdown, kurz im Drop.
- **Gemessen** (Seed 7, A-Moll, 138 BPM): Drop −9,3 LUFS Kurzzeit-Maximum, Breakdown −15,7: **6,4 LU Abstand** (Dok.
  6: 4 bis 8); Gesamt −11,6 LUFS integriert, True Peak −1,0 dBTP; Stimmen und Räume unter 150 Hz mindestens 30 dB unter
  sich selbst, Kick und Sub dort zu Hause; Pump-Tiefe auf 0,000 dB genau; Blockgrößen 1, 37, 512 bitgleich auch über
  das Gate im Drop; Vektorpfade AVX2, NEON-Shim und skalar bitgleich für Kit, Supersaw/Poly-Lanes und neun
  Filtermodelle mit Modulationsmatrix. ctest 15/15. Render 8- bis 11-mal Echtzeit (12,6 % eines Kerns).
- **Zwei Fehler auf dem Weg**, beide mit Test abgesichert: Totalitys Plate *addiert* in den Ausgang (ungeleert lief er
  über und riss den Master auf NaN), und das Gate rechnete seine Phase ab Spannenanfang statt ab absolutem Sample
  (nicht bitgleich bei anderen Blockgrößen, sobald eine Note mitten in einer Rasterzelle lag).
- **Offen aus Phase 1:** die Startpegel sind [I] (Pad −17, Lead −9, Mid-Bass −1, Sub −4 dB, Hall-Return 0 dB); sie
  gleicht seit Phase 2 der Leveler je Track aus. Hörfiles: `out/hoeren_p1`.

**Referenzmessung (29.09.2026).** 30 Aufnahmen über YouTube (`Tools/fetch_refs.py` sucht die Extended-/Original-
Fassung über Titel und Länge, `Tools/ref_lock.txt` hält die gewählten Uploads fest; Audio in `%TEMP%\parhelion_refs`),
gemessen mit `Tools/analyze_ref.py` (Statistiken in `Tools/ref_stats.json`). Befunde [M], die das Dokument korrigieren:

| Größe | Uplifting | Progressive | Dream House | Acid | Deep | Dok. |
|---|---|---|---|---|---|---|
| Tempo (Median) | 138,1 | 135,5 | 137,5 | 134,1 | 131,1 | Deep 90–120 |
| Länge (min) | 8,6 | 8,2 | 6,9 | 7,5 | 8,7 | – |
| Breakdown-Anteil (Takte ohne Tiefband) | 14 % | 13 % | 0 % | 10 % | 15 % | 20–100 % |
| Haupt-Breakdown (Takte) | 39 | 28 | – | 24 | 39 | 64–96 |
| Breakdown → Drop (lauteste 3 s) | 2,1 LU | 3,3 | – | 2,9 | 3,2 | 4–8 LU |
| Intro ohne Kick (Takte) | 0 | 25 | 38 | 1 | 7 | – |
| lauteste 20 s | −7,8 | −8,2 | −10,0 | −9,6 | −8,6 | −7 bis −11 |

Dream House behält den Bass durchgehend (Children, Fable: kein Takt ohne Tiefband außer dem Piano-Intro von 60 bis 70
Takten); Deep/Chill-Trance läuft mit Club-Tempo. Die Messung der Lücke zählt die ersten 3 s eines Breakdowns nicht mit,
weil ffmpegs Kurzzeitwert die 3 s *davor* mittelt (der erste Lauf hatte so den Drop als lauteste Stelle des Breakdowns
gelesen und die Lücke um 1 bis 3 LU unterschätzt). Die Profile (compose/Style.cpp) tragen diese Werte.

**Phase 2, Form und Energie (29.09.2026).**
- **Planer** (compose/Planner.h): fünf Formgrammatiken (Anthem, Dream, Acid mit Kick-Pausen, Plateau, Drift), acht
  Kandidaten je Track, der nächste an Länge und Breakdown-Anteil gewinnt; Gesamtlänge auf 32 Takte über Outro oder ersten
  Groove (nie das Intro); Breakdown mit Intro, Tease, Peak; Build mit Kick, Roll, Riser; Layer-Matrix je 8 Takte mit
  Regel 1 (jeder Block ändert etwas); Mini-Breaks; leerer letzter Schlag vor jedem Drop.
- **Harmonik** (compose/Harmony.h): Tonart nach Profil (Grundtöne D bis A bevorzugt), die fünf Progressionen, Dur über
  die parallelen Funktionen, harmonischer Rhythmus je Sektion, VII in den zwei Takten vor jedem Drop und Auflösung auf i,
  Acid statisch mit Quart-/Quintwechsel alle 16 Takte, Cinematic-Terzrückung nach dem zweiten Breakdown.
- **Komponist** (compose/Composer.h): alle Stimmen aus Plan und Harmonie (Bassfiguren Offbeat, Rolling, Gallop, Walking,
  Drone, 303-Sequenz mit Accent und Slide; Hats 16tel oder Achtel; Percussion als Shaker oder Conga mit Tambourine;
  Snare-Roll von Vierteln bis 32teln), Melodik-Regeln (compose/Melody.h: Lead-Motiv mit Sprung und Abstieg in A A' B A'',
  Pluck-Rhythmen, Arp, Stab, Counter), Effekte (Phosphenes Sfx: Riser, Reverse-Crash, Impact, Downlifter, Sub-Drop,
  Sweep), Automation (gefilterte Zellen, Pad-Filter im Breakdown, Hall je Sektion, Pump aus/an mit Rampe, Hochpass im
  Build, Delay-Throw vor dem Breakdown, Gate im Drop, 303-Filterwellen), Neuwürfeln je Einheit (`--reroll melody`),
  `.parhset`.
- **Leveler** (Leveler.h, nach Totality): Balance jeder Stimme gegen die Kick (Fenster [I]), Breakdown gegen Drop auf
  den gemessenen Abstand des Profils (Korrektur auf Synths, Räume, Effekte in allen Breakdowns), jeder Build mindestens
  1 LU unter dem Drop, der Drop auf das Lautheitsziel (bis ±6 dB).
- **Tests:** Planer über 5 Stile × 8 Seeds, Harmonik, Determinismus und Neuwürfeln, Leveler; ctest 19/19.
  Hörfiles: `out/hoeren_p2` (je Stil Seed 7, mit MIDI).

**Phase 3, die Melodie (29.09.2026).**
- **Korpus Stufe A** (compose/Corpus.h, `Tools/corpus/build_corpus.py`): aus den EMP-Packs (500 Melodien, 100 Pianos,
  108 Acid-Linien, 200 Basslines; 64742, 5566, 9604 und 18277 Töne der Oberstimme [M]) je Rolle Zählungen über diatonische
  Stufen relativ zur Molltonika (−7 bis +14; Dur zählt über die Parallele), Ordnung 0 bis 2, Einsätze je 16tel, Längen,
  Accent und Slide. Das Modell interpoliert Ordnung 2 → 1 → 0 nach Witten-Bell; der Sampler zieht exakt unter Constraints
  je Position (Pachet und Roy 2011: Rückwärtssummen, Vorwärtsziehen), der Selbsttest vergleicht acht Linien mit ihren
  enumerierten Wahrscheinlichkeiten (Abweichung 0.004).
- **Befund der Packs** [M]: die EMP-Melodien sind dichte 16tel-Riffs (90 % der 16tel mit Einsatz, Längen fast nur ein
  16tel), die Pianos Achtel, die Acid-Linien kreisen um Grundton und Oktave und tragen fast keine Velocity-Akzente (8 von
  516 Schlägen auf der Eins) und wenige Slides. Also: Rhythmus des Anthem-Motivs aus Regeln (Dok. 4), Rhythmus von Riff und
  Piano aus dem Korpus, Accent und Slide der 303 aus Regeln (Slides nur verteilt nach dem Korpus).
- **Lead** (compose/Melody.h): Hymne (lange Töne, Kontur als Gewicht: Sprung von Quarte bis Oktave nach dem ersten Ton,
  dann stufig abwärts, kein dritter gleicher Ton) oder Riff (halbtaktige Zelle, viermal, die zweite und vierte mit neuem
  Ende); harte Constraints Tonvorrat (Pentatonik als fünf der sieben Stufen), Lage E4 bis etwa D6, Akkordton auf jedem
  Schlag und am Ende; im harmonisch-Moll der Leitton nur mit der Dur-Dominante (die Tonleiter je Takt nimmt fehlende
  Akkordtöne auf). Acht Kandidaten für A, vier für B, bewertet nach Umfang, Tonvielfalt, Stufenanteil, Sprung am Anfang,
  Abstieg zum Ende. **Fassungen**: gefilterte Zellen spielen nur A alle vier Takte (Tease, Fragment in Break und Build),
  offene die Phrase A A' B A'' (A' behält die passenden Töne, A'' A's Kopf und endet auf der Tonika, wo der Akkord sie
  hat), die zweite Hälfte des Haupt-Drops eine Oktave höher, falls das Motiv dort unter A6 bleibt. Dream House: das Piano
  mit dem Piano-Korpus, legato, ohne Oktavsprung.
- **Counter** antwortet in den Haltetönen des Leads eine Oktave darüber (Akkordtöne abwärts in Achteln, der letzte
  gehalten), über einem Riff ein gehaltener Terz- oder Quintton. **303** (writeAcid): ein Takt aus dem Acid-Korpus (erster
  Ton auf dem Grundton), alle vier Takte zwei Töne neu gezogen und ein Akzent verschoben, die Linie folgt dem Akkordgrundton.
  Nebenbei behoben: Planer und Komponist zogen die Bassfigur getrennt, und Acid hatte in 30 % der Seeds keine 303; jetzt
  gibt der Plan die Figur vor, und das Acid-Profil besetzt die 303 immer.
- **Memorisierung** (compose/Memo.h, `Tools/corpus/memorisation.py`): Fenster aus zwei Takten (Einsätze und Intervalle,
  also transpositionsinvariant; mindestens fünf Töne, drei Tonhöhen) aus jeder Spur jeder der 5462 lesbaren
  Transkriptionen an jedem Schlag: 144250 Fenster [M], als Bloom-Filter ihrer Hashes im Code (256 KiB, 8 Hashes, im Test
  0.09 % falsche "vielleicht"). Der Lead wird nach dem Schreiben Takt für Takt geprüft und bei einem Treffer neu gezogen
  (bis 16 Versuche). Ergebnis über 100 Tracks (5 Stile × 20 Seeds): **Lead 0 Treffer in 5822 Fenstern**, 303, Pluck, Bass
  und Counter ebenfalls 0; das Arp 5.2 % (388 von 7459) -- gebrochene Dreiklänge auf und ab, wie sie jede Arp-Spur der
  Transkriptionen enthält; das Arp ist Regelwerk ohne Korpus und kann kein Motiv mitbringen, darum dort Bericht statt
  Sperre. Zum Vergleich die EMP-Packs selbst [M]: 2.5 % ihrer Melodie-Fenster und 1.9 % ihrer Piano-Fenster sind mit
  Transkriptionsfenstern identisch, überwiegend ganze 16tel-Riffs aus 30 bis 32 Tönen. Die Packs teilen also Riffs mit
  bekannten Tracks; umso wichtiger, dass aus ihnen nur Zählungen der Ordnung 2 übernommen werden und die Sperre auf dem
  Ergebnis sitzt.
- **Tests:** testCorpus (Normierung, Constraints, Exaktheit), testMemo (Hash wie das Werkzeug, auch transponiert;
  Falsch-Positiv-Rate), testMelody (6 Seeds × Uplifting und Dream House: Akkordton auf jedem Schlag, Tonvorrat, Lage E3
  bis C7, keine Transkriptions-Fenster); ctest 22/22. Hörfiles: `out/hoeren_p3`.

**Phase 4a, das Piano (29.09.2026).** Nach 5.8 gebaut (synth/PianoDesign.h, synth/Piano.h), Modul `piano`, Part und
Stem `piano`, MIDI-Kanal 11, im Leveler eine eigene Spur (Fenster −9 bis −3 dB gegen die Kick).
- **Datenblatt**: je Taste Länge (Blankdraht-Gesetz L ~ f^−0.9 ab 5.2 cm bei C8, gesättigt an der längsten Saite des
  Gehäuses: Flügel 1.95 m, Stutzflügel 1.50 m, Klavier 1.22 m), Blankdraht über F2, umsponnen darunter (Chor 1, 2, 3),
  Spannung aus f0, Kerndurchmesser, B nach Fletcher [M: A0 1.0e−4, C4 4.4e−4, C8 1.8e−2, wie Conklin und Young]; die
  Stimmung aus B gespreizt (über A4 Grundton auf den zweiten Partialton der Unteroktave, darunter vierter bzw. sechster
  auf den zweiten bzw. dritten der Oberoktave). Vier Instrumente: Grand, Baby Grand, Upright, Soft (das "leicht dumpfe"
  Piano von Dok. 5: Klavier, weicher Filz, tieferer Anschlag, früher Abfall des Bodens).
- **Resonanzboden**: Rayleigh-Ritz über 18 × 18 Sinusprodukte, Energie über den Grundriss des Gehäuses integriert
  (gerippte orthotrope Fichtenplatte, Rippen verschmiert), Stege als gekrümmte Balken, Zarge als steife Lagerung;
  Cholesky und QL (tred2/tql2). Selbsttest gegen die geschlossene Lösung der orthotropen Rechteckplatte: 0.000 %.
  Flügelboden [M]: erste Mode 44 Hz, 44 Moden bis 1.2 kHz; darüber die Punktmobilität der gerippten Platte
  (Y_inf = 1/(8 sqrt(D m'')) = 9e−4 s/kg) und eine Bank von 36 logarithmisch verteilten Resonatoren. Abstrahlung als
  Beschleunigung an drei Hörpunkten (Bass, Mitte, Diskant), die Breite mischt sie.
- **Saiten**: je Partialton der Chor als D + Rang-eins-Matrix, Eigenwerte nach Durand-Kerner, Eigenvektoren
  geschlossen; die horizontale Polarisation als zweiter Chor mit einem Viertel der Admittanz. Wo ein Partialton auf eine
  Bodenresonanz trifft, gilt die schwache Kopplung nicht mehr: der Anteil der Brückenverluste ist durch die halbe
  Abklingrate der Bodenmode begrenzt (der Teilton kann Energie nicht schneller abgeben, als der Boden sie abführt),
  ebenso die Verschiebung der Frequenz. Diskretisierung mit Halteglied nullter Ordnung; der Hammer (Stulov, Masse und
  Steifigkeit nach Chaigne und Askenfelt) auf 4 bzw. 8 Unterschritten während des Kontakts; Spannungsmodulation nach
  Kirchhoff-Carrier je 32 Samples; Längsmoden auf dem Quadrat der Brückenkraft (die lokale Dehnung am Steg; die
  räumliche Auswahl von Bank und Sujbert Gl. 22 weggelassen); Dämpfer mit Rampe und Halbpedal; Mitschwingen einseitig
  über die Beschleunigung des Bodens an 16 Stegpunkten, abgestrahlt über eine zweite Kopie des Bodens.
- **Befund zum Mitschwingen** [M]: die einseitige Kopplung (Bank 2010) erzeugt an exakten Koinzidenzen (Oktaven und
  Quinten der gespreizten Stimmung) Energie, weil die Rückwirkung der mitschwingenden Saiten auf den Steg fehlt: physikalisch
  gerechnet lagen sie 19 dB **über** dem Ton. Ein gekoppeltes System würde die Energie des Tons unter ihnen teilen; der
  Antrieb ist deshalb auf 0.03 skaliert (zusammen etwa 12 dB unter dem losgelassenen Ton mit Pedal), `piano.sympathetic`
  skaliert von dort. Eine gekoppelte Lösung (Saiten aller freien Tasten in der Chor-Matrix) bleibt als Verbesserung offen.
- **Messungen** (`parh_pianoprobe --measure`, Selbsttest) [M]: Partialtöne 4 bis 12 von C2 innerhalb 2.6 Cent des
  steifen Saitenmodells, der zwölfte 15.8 Cent über dem harmonischen; C4 zweistufig (−19.5 dB/s früh, −4.8 dB/s spät);
  Anschlag 0.2 → 1.0: 22 dB lauter, heller (Schwerpunkt C4 683 → 1192 Hz); Tonhöhenabfall nach Fortissimo A1 0.7 Cent;
  Dämpfer 67 dB in 0.5 s (mit Pedal 4 dB); Phantompartialtöne C2 −52 dB (0.35) bzw. −32 dB (1.0) des Tons, also
  quadratisch. Kosten: ein gehaltenes C4 0.5 %, C2 0.7 % eines Kerns, der Boden allein 0.3 %, Pedal unten (87
  mitschwingende Saiten) 3.4 %; sechs Töne 3.2 % (AVX2, FTZ). Der Vergleich mit Einzelton-Aufnahmen steht noch aus:
  bisher gegen Literaturwerte (Inharmonizität nach Fletcher/Conklin, Abklingraten nach Weinreich); dafür wären
  Aufnahmen zu laden.
- **Im Arrangement**: Dream House schreibt das Motiv für das Piano (Part::Piano, mezzoforte), meist das Soft-Instrument;
  das Pedal unten, bei jedem Akkordwechsel kurz gehoben (Legato-Pedal als Automation von `piano.pedal`).
- **Determinismus**: Entscheidungen (Spannung, Dämpfer, Kontaktende, Stimmende, Mitschwingen an/aus) auf einem
  absoluten 32-Sample-Raster; das Rauschen des Dämpfers je Stimme mit eigenem Strom (ein gemeinsamer verschränkte sich je
  nach Blockgrenzen — gefunden durch den neuen Blockgrößen-Test mit Piano). Skalar, AVX2 und NEON bitgleich.
- **Tests**: testPiano (10 Prüfungen), testPianoBlocks (Dream House bei Blockgröße 1, 37, 512), vectest testPiano;
  ctest 24/24. Hörfiles: `out/hoeren_p4a` (Einzeltöne je Instrument, Dream House Seed 7 und 3 mit Stems).

**Phase 4b, das Orchester (29.09.2026).** Nach 5.9 gebaut, Module `strings`, `choir`, `brass`, `timpani`, je Part,
Stem, Balance-Spur (Fenster [I]) und MIDI-Spur; Messwerkzeug `parh_orchprobe` (`--measure`, `--others`, `--demo`).
- **Streicher** (synth/Strings.h): je Spieler eine modale Saite (24 Partialtöne) unter einem Bogen mit hyperbolischer
  Reibungskennlinie (Smith 1986, Woodhouse); je Sample erst die freien Moden, dann die Kraft: Haften, wenn die
  Haftreibung die nötige Kraft (v_Bogen − v_h)/Y aufbringt, sonst Gleiten mit der einen positiven Wurzel einer
  quadratischen Gleichung — geschlossen, ohne Iteration, ohne Mehrdeutigkeit (Demoucron 2008: modale Saite unter dem
  Bogen in Echtzeit). Sechs Spieler je Note (Quest drei) mit eigener Intonation, eigenem Einsatz und Vibrato; Familie
  nach Lage (Kontrabass, Cello, Bratsche, Violine), höchste leere Saite unter dem Ton, gegriffen; Korpus je Familie
  mit den Signaturmoden A0, CBR, B1−, B1+ (Bissinger 2008) skaliert, 44 statistischen Moden mit Stegberg und einem
  Breitbandanteil. Befund [M]: mit einem Drittel der Schelleng-Obergrenze blieben die tiefen Cellotöne in einem
  Oberflächenklang mit fehlendem vierten Partialton; mit der Hälfte Helmholtz-Bewegung in allen Familien. Einzelspieler
  innerhalb weniger Cent (plus seiner Intonation); Kosten 2.7 % eines Kerns für vier Noten mit sechs Spielern (AVX2).
- **Chor** (synth/Choir.h): LF-Quelle nach Fant, Liljencrants und Lin mit Rd-Parametrisierung (Fant 1995), in
  normierter Zeit für 64 Rd-Werte gelöst (ε per Newton, α per Bisektion auf die geschlossene Periode; Selbsttest: die
  Fläche verschwindet auf 1e−6), in der Periode als rotierender Zeiger und abklingende Exponentielle gerechnet; Jitter,
  Shimmer, Vibrato, Wandern, Hauchgeräusch; fünf Formanten parallel nach den Sängertabellen der Formantsynthese (Bass,
  Tenor, Alt, Sopran auf a, o, u), zwei Trakte je Note (±3 %) für Streuung und Stereo. Tonhöhe ±6 Cent, 30 Sänger
  1.2 % eines Kerns.
- **Blech** (synth/Brass.h): Lippe als nach außen schlagendes Ein-Massen-Ventil (Adachi und Sato 1996, eindimensional)
  mit Bernoulli-Fluss in geschlossener Form, Bohrung als Wellenleiter mit Schalltrichter-Tiefpass, nicht invertierend
  (die ganze Obertonreihe wie beim Kegel). Befunde [M, Simulation]: Lippenfrequenz 1.02 über dem Ton und die
  Streuknoten-Reduktion des Synthesis ToolKit rasteten nicht ein; Lippe bei 0.8 des Tons, Q 7, zwei Unterschritte je
  Sample rasten über den Umfang 10 bis 45 Cent zu hoch ein; das "Ohr des Spielers" (Periode an den Nulldurchgängen,
  Verzögerung der Bohrung nachgeführt) bringt sie auf ±4 Cent. Mehr Atemdruck für höhere Töne (f^0.3); das Schmettern
  als Aufsteilung auf der abgestrahlten Welle (in der Schleife schaukelte es sich auf). Heller mit mehr Druck [M:
  Schwerpunkt B♭2 517 → 613 Hz].
- **Pauken** (synth/Timpani.h): 12 Moden nach Rossings Messungen (luftbelastet, 1 : 1.5 : 1.98 : 2.44 : 2.9), darüber
  20 Moden der idealen Membran (Nullstellen von J_m, ×1.12), Formen J_m(j r/a) am Schlagpunkt, Schlägel als
  Filzkontakt mit Potenzgesetz auf Unterschritten; vier Kessel. (1,1) auf 0.2 Cent, (2,1)/(1,1) = 1.500. Der
  Anschlag macht den Klang nur wenig heller (Schwerpunkt +4 bis +8 %) — Hörrunde.
- **Im Arrangement** (Cinematic): Uplifting mit Wahrscheinlichkeit 0.5, Dream House 0.25, Deep 0.15. Streicher-Akkorde
  ab dem Tease des Haupt-Breakdowns, am Peak die Violinen mit der Melodie, der Chor am Peak und in der zweiten Hälfte
  des Haupt-Drops; an jedem Drop ein Braam (Grundton tief, Quinte, Oktave) und die Pauke, in den letzten zwei Takten
  davor ein Wirbel (16tel, dann 32tel, anschwellend).
- **Tests**: testOrchestra (Tonhöhe aller Instrumente, LF-Fläche, Blech heller mit Druck, Paukenmoden),
  testOrchestraBlocks (Uplifting mit Orchester bei 1, 37, 512 bitgleich — fand eine Spitzenmessung, die nur das letzte
  Segment einer Zelle sah), vectest testStrings (Skalar/AVX2/NEON bitgleich); ctest 26/26. Im Mix [M, Uplifting Seed 1]:
  Streicher −8, Blech −6, Pauken −10 dB gegen die Kick, der Breakdown 1.8 LU unter dem Drop (zuerst lagen die
  Streicher 8 dB **über** der Kick und der Breakdown über dem Drop: die Pegel der Instrumente um 16, 6 und 6 dB
  gesenkt). Hörfiles: `out/hoeren_p4b` (Orchester-Demo, Uplifting Seed 1 mit Stems).

**Phase 4c, die Sub-Genres (29.09.2026).** Nach 5.3, 5.7, 6.4 und 13.4 gebaut.
- **Acid**: die 303 trägt Bass und Melodie; ihre vier Kurven (Cutoff, Resonanz, Hüllkurvenhub, Decay) steigen über
  Phrasen von 16, 32 oder 64 Takten und springen an deren Ende zurück, an jeder Sektionsgrenze geschnitten; wie hoch sie
  steigen, folgt der Energiekurve (die Höhepunkte sind die Filterpeaks, Dok. 6).
- **Deep**: das Pad ist das Instrument — Filter und Panorama driften in Wellen von 8 oder 16 Takten; die Granularwolke
  (fx/Cloud aus Totality, dort aus Noctuary) körnt Pad, Pluck und Piano in die Platte, −15 dB (bei −8 dB fiel die
  Korrelation auf 0.4 [M]); die Drift-Form mit längerem Groove, Plateau und Haupt-Drop (die Referenzen laufen neun
  Minuten).
- **Dream House**: das Motiv dem Piano (4a), ein Hauch der Wolke (−16 dB), die weiche Kick.
- **Progressive**: die Plateau-Form, rollender und gehender Bass, mehr Luft.
- **Cinematic**: das Orchester in Uplifting (4b).
- **Kalibrierung** (Tools/calibrate.py): rendert je Stil drei Seeds (parallel, `--jobs`), misst sie mit genau der
  Analyse der Referenzen (analyze_ref.measure) und legt 19 Maße neben Minimum, Median und Maximum der Referenzen des
  Stils (Tempo, Länge, Lautheit, LRA, lautestes 20-s-Fenster, Breakdown-Anteil und -Länge, Abstand, sechs Bänder,
  Schwerpunkt, Seitenanteil über 200 Hz, Korrelation, Pump, Offbeat-Anteil des Basses). Die Seite unter 120 Hz ist
  bewusst nicht dabei: Parhelions Tiefe ist mono (master.mono_below), die −14 bis −37 dB der Referenzen dort kommen
  zum Teil aus ihren verlustbehafteten Transcodes. Fünf Runden [M, alle Spuren eines Stils im Bereich]:
  Runde 1 (zwei Seeds, noch mit der Seite unter 120 Hz, 20 Maße): Uplifting 6, Progressive 12, Dream House 7,
  Acid 15, Deep 5; Runde 2: 8, 12, 7, 15, 10; Runde 3 (19 Maße): 10, 14, 8, 15, 11; Runde 4 (drei Seeds, nach dem
  Umbau der Ströme, 5): 14, 10, 7, 14, 13; Runde 5: 14, 15, 9, 14, 15. Der Prüfstein von 14 (ein Track je Stil im
  Korridor): der beste Track je Stil trifft 19 (Uplifting Seed 1), 18 (Progressive, Acid), 17 (Deep) und 16 (Dream
  House) der 19 Maße.
  Die Korrekturen, jede gemessen: der Leveler misst den lautesten 20-s-Ausschnitt des ganzen Haupt-Drops (vorher dessen
  Anfang) und den Breakdown-Abstand nach seiner letzten Korrektur noch einmal (er meldete den Stand davor); die Profile
  bekommen `hatsDb` und `tiltDb` (Luft: Progressive lag in allen Seeds unter dem hellsten Schwerpunkt der Referenzen) und
  neue Gewichte der Bassfiguren (reine Offbeat-Bässe messen 0.54 bis 0.74 Offbeat-Anteil, die Referenzen 0.2 bis 0.46);
  **die Fenster der Balance über einer weichen Kick** (LevelMark::windowDb, 6 dB mal kickSoft): Deeps Pad, Pluck und
  Arp lagen 3 bis 5 dB über ihren Fenstern und wurden gesenkt, weil eine weiche Kick weniger Spitze für ihr Gewicht hat;
  danach trug ihr Körper 60 % der Drop-Leistung zwischen 60 und 150 Hz (Referenzen 23 bis 43 %). Dazu mix.hats_level −1,
  master.tilt 1.5, das Pad-Voicing ab E3, Deep ab 128 BPM, die Längen der Breakdowns (Progressive 24 oder 32 statt 16,
  Deep höchstens 40).
  Offen nach Runde 5 [M]: die Tiefmitten (150 bis 400 Hz) in Dream House und Progressive zu dünn (0.06 bis 0.09 gegen
  0.10 bis 0.14 und mehr), Deep zwischen 60 und 150 Hz schwerer als jede Referenz (0.49 bis 0.53 gegen höchstens 0.43)
  und mit kleinerem Lautheitsumfang (LRA 4.5 bis 5.3 gegen 7.6 bis 12), Dream House über 200 Hz breiter; der
  Offbeat-Anteil des Basses streut mit seiner Figur (0.10 bis 0.74, die Referenzen 0.20 bis 0.49). Weiter an den
  Standardknöpfen zu drehen lohnt nicht: mit den Klangbänken (5b) wählt der Komponist je Track und Synth ein Preset, das
  jede Stimme verändert; die Kalibrierung wird danach wiederholt (5b, Nachkalibrierung in 8).
- **Tests**: testSubGenres (die 303 mit ihren Kurven, Deeps Wolke und Drift, Dream Houses Piano, das Orchester in
  einigen Uplifting-Tracks, der Morph zwischen zwei Profilen).

**Phase 5, Set und Ausgaben (29.09.2026).** Nach 6.8, 6.9, 8 und 13.5 gebaut.
- **Das Set** (compose/Set.h): eine Länge in Minuten, fünf Dramaturgien (Warm-up, Peak, Closing, Sunrise, Journey) als
  Energiebogen mit Tempoverlauf (höchstens 2 BPM je Track), ein Stil oder die Stil-Reise (Wander: die Leiter Deep,
  Dream House, Progressive, Uplifting, Acid dem Bogen nach), die Tonartenreise auf dem Camelot-Rad (dieselbe, eine
  Quinte, die Parallele — aus Dur öfter, Trance lebt in Moll —, der Energy-Boost um einen oder zwei Halbtöne). Die
  Tracks eines Sets sind mischbar: Intro und Outro 32 Takte und mehr, mit Beat, nie beatless, ohne Ambient-Teile.
- **Der Blend**: der nächste Track beginnt so, dass sein Intro über dem Outro des laufenden liegt; ein Bass-Swap (der
  Isolator des DJ-Mixers) 16 Takte vor dem Groove des kommenden, nie vor seinem Bass; Fader- und Filtergesten der Hand;
  die Hook des nächsten Tracks als Tease auf Deck C.
- **Ausgaben** (Export.h, parh_render): die Cues (Bass, Breakdowns, Drops, Haupt-Drop, Outro; im Set jeder Track und
  sein Bass-Swap) im Cue-Chunk des WAV, als JSON und als rekordbox-Sammlung (Beatgrid, Memory Cues, Hot Cues), die
  DJ-Loops (Intro, Haupt-Drop, Outro, je acht Takte nahtlos), MIDI, Stems, der Plan als JSON; die OSC-Cues (Cue.h)
  sendet das Plugin (Phase 6).
- **Sperren und Würfeln** (6.9): jede Einheit auf ihrem Strom — `form`, `matrix`, `energy`, `harmony`, `motif`, `lead`,
  `bass`, `acid`, `arp`, `pluck`, `piano`, `orchestra`, `drums`, `fx`, `sounds`, `section<n>` —, im Set `track<n>`,
  `track<n>.<unit>` und `set`; in der `.parhset` gespeichert. Der Streuung der Anschläge nimmt ein Hash je Note den
  Strom (sonst verschöbe eine neu gewürfelte Stimme die Anschläge aller späteren). Die Matrix bekam dabei ihre Streuung
  je Sektion (wann Arp, Stab, Percussion, Ride und Counter einsetzen, die Reihenfolge des Intros, der Mini-Break) und
  die Energie ihre (±0.5, der Haupt-Drop bleibt 10; Pads Öffnung und die 303 folgen ihr); keine Streuung berührt Kick
  oder Bass, so bleibt die Form beim Würfeln von Matrix und Sektion stehen. Dazu das **Vakuum vor dem Drop** in
  Phosphenes vier Fassungen (die letzte Zählzeit, die letzten zwei, der ganze Takt, die Kick allein auf 4): vorher war es
  immer die letzte Zählzeit, und der Haupt-Drop nach dem Build hob sich nur 2.9 dB [M, Uplifting Seed 7].
- **Evaluation** (Tools/eval_report.py, 13.5): je Track die Neuheitskurve (Foote 2000) gegen die geplanten Grenzen
  (Precision, Recall, F1 auf ±1 Takt), der Anteil der gefundenen Grenzen auf 8-Takt-Linien, die gehörten Drops (+3 dB),
  der Abstand Breakdown → Drop, die Tonart des Audios (Krumhansl-Kessler) gegen den Plan, der Korridor der Referenzen.
  Erster Lauf [M, Uplifting Seed 7, vor den vier Vakuen]: F1 0.60, 92 % der gefundenen Grenzen auf 8-Takt-Linien,
  Abstand 2.1 LU, Tonart eine Quinte daneben (≈), Korridor 17 von 19.
- **Der Prüfstein, das Zwei-Stunden-Set** [M, Seed 7, Sunrise, Stil-Reise; `docs/eval/set_seed7_sunrise.md`]: 22 Tracks,
  124 Minuten, Energie 0.45 → 0.98 und 134 → 138 BPM, Progressive → Dream House → Uplifting, sieben Tracks mit dem
  Orchester, sieben Teases auf Deck C, die Tonarten schrittweise um das Camelot-Rad (Parallele 11A → 11B, 9B → 9A, ein
  Energy Boost 4A → 11A); −10.6 LUFS integriert, LRA 7.4 LU, True Peak −1.0 dBTP, Korrelation 0.92; komponiert und
  gepegelt in 36 Minuten, gerendert in 21 (sechsfache Echtzeit). Sektionsgrenzen F1 0.53 und 63 % auf 8-Takt-Linien (im
  Set setzen Blends und Teases eigene Neuheitsspitzen), die Tonart 13 von 22 wie geplant oder parallel, 5 eine Quinte
  daneben. **Zwei Befunde, beide behoben:** (1) Kick, Kit und Effekte blieben in der Tonart des Knopfs (A): der Komponist
  schrieb die Tonart des Tracks nie auf die Knöpfe, nach denen das Deck stimmt — vier Dream-House-Tracks hörte die
  Auswertung deshalb in a-Moll; jetzt setzt jeder Track compose.key und compose.scale an seinem Anfang (Test). (2) Das
  Kriterium "Drop gehört: +3 dB über die zwei Takte davor" (13 von 32) war an keiner Referenz gemessen; an seine Stelle
  treten `drop_rise` und `drop_rise_low` in analyze_ref.py, auf Referenzen und eigenen Tracks auf dieselbe Weise: der
  größte Anstieg zweier Takte über die zwei davor in den 24 Takten nach dem längsten Breakdown. (Eine erste Messung an
  `drop_at` der Referenzen, dem Anfang ihrer lautesten 32 Takte, fand +1.2 dB im Median — falsch gemessen: das Fenster
  beginnt oft mitten im Drop.) Die Referenzen: breit +1.2 bis +12.6 dB (Uplifting im Median +2.7), tief +3 bis +40 dB
  (Uplifting +10). Runde 5 daneben: breit im Bereich (Uplifting +2.3 bis +3.5, Progressive +2.9 bis +3.9, Deep +4.1 bis
  +5.5), das Tiefband aber steigt in Progressive, Deep und einem Uplifting-Track um +36 bis +51 dB — unsere Breakdowns sind
  unter 150 Hz leer, die der Referenzen tragen dort Pad-Körper, Hall und Sub-Schwellungen; Dream House (der Bass bleibt im
  Breakdown) steigt tief zu wenig. Beides geht in die Nachkalibrierung.
- **Tests**: testSet (Tracks und Decks, Tempo, Camelot, Intro über Outro mit einem Bass-Swap, die Stil-Reise, der
  erste Bass-Swap bei Blockgrößen 37 und 512 bitgleich), testComposer (18 Würfelfälle bitgleich außerhalb der Einheit bis
  zur Anschlagstärke, Form beim Würfeln von Matrix und Sektion unverändert, die vier Vakuen, im Vakuum nichts außer der
  Kick auf 4), testLeveler; ctest 28/28.

**Phase 5b, Klangbänke und Modulation (29.09.2026).** Auf Wunsch des Nutzers vor die GUI gezogen (12, 14).
- **Modulation für jede Stimme** (synth/Modulation.h): der Block von Ephemeris und Phosphene — Mod-Hüllkurve, vier LFOs,
  Matrix mit acht Slots — jetzt auch für Bass und 303 (Totalitys Mono-Synth, alle 16 Samples seines eigenen Zählers),
  Piano (je Stimme: Tonhöhe als exakte Drehung der Pole mit der Spannung, Hammerhärte beim Anschlag, die Kraft auf den
  Steg, die Stegregion als Panorama — ohne Anschlagpunkt: die Hammerlinie ist fest, und der Entwurf kennt die Moden nur
  dort, an einem Knoten mit null Anregung), Streicher (Bogendruck, -geschwindigkeit und -position: die Kraft mit
  Schellengs Grenze über v/β mitgeführt, die Modenformen am Bogenpunkt neu; Vibrato-Tiefe und -Rate, Tonhöhe exakt
  gedreht, Moden nahe Nyquist stumm), Chor (Vokal, Formantverschiebung als Traktlänge, Spannung als Rd, Hauch, Vibrato),
  Blech (Atemdruck ¼ bis 2×, Schmettern, Lippe und Rohr gemeinsam gebogen, ±2 Halbtöne), Pauken (Härte und Schlagpunkt
  beim Schlag, Pedal ±5 Halbtöne und Abklingen als neu gestimmte Moden). Neue Quellen für alle: das Modrad und der
  Kanaldruck (perform.wheel, perform.pressure) und die Energie der Partitur. Poly bekommt Detune als Ziel. Alles auf dem
  absoluten Sampletakt jeder Engine; ohne belegten Slot rechnet jede Stimme bitgleich wie zuvor. Nebenbei: Poly setzte
  bei einem Sprung nur die Hüllkurven zurück, die freien LFOs liefen rückwärts weiter — jetzt vom Anfang.
- **Die Bänke** (Presets.h, PresetBank.cpp, `Tools/presets/bank_spec.py` → PresetBankData.inl): 18 Bänke zu 1024 —
  Kick, Sub, Kit-Lane (je Rolle), Bass, 303, Lead, Counter, Pluck, Arp, Pad, Stab, Piano, Streicher, Chor, Blech,
  Pauken, FX, Wolke —, je 16 Gruppen auf dem Raster aus acht Adjektiven und acht Nomen, jede Gruppe mit Bereichen und
  Achsen, Filtermodellen (Listen), Modulationsrezepten und Gewichten in den fünf Stilen; der Pegelausgleich je Preset
  gemessen (TRIMS). Die Wahl des Komponisten (Einheit `sounds`, compose.pick_sounds): die Gruppen nach ihrem Gewicht im
  Stilvektor des Profils, **hoch drei** (mit den einfachen Gewichten war das Lead eines Uplifting-Tracks nur jedes achte
  Mal ein Anthem-Supersaw), dann eines ihrer Presets; eine Kit-Lane nur aus Gruppen ihrer Rolle. Die Wahl als SoundPick
  und Knopfsätze am Trackanfang (vor den Stilregeln, die gewinnen), im Set mit jedem Track verschoben, im MIDI als
  Name und Programmwechsel (Bank = Preset / 128, Programm = Preset % 128; Kick und Kit nur der Name, der Drumkanal),
  im Plan-JSON und in `parh_render`s Bericht ("sounds: lead Euphoric Octaves / Brilliant Glory, ...").
- **Nachkalibrierung mit den Presets**: die Pegelausgleiche aller 18 432 Presets gemessen [M] (testPresetBank mit PARH_BANK_TRIMS, 24 Minuten
  auf 16 Kernen): alle endlich und hörbar, keiner weiter als 21 dB vom Standardklang; im Median brauchen die Sub-Presets
  −6.7 dB, die der 303 und des Counters +5 dB. Die Zeile (dunkel bis hell) wählt der Komponist um die Helligkeit des
  Stils (Uplifting 0.65 .. Deep 0.4 der acht Zeilen, eine Glocke von anderthalb Zeilen): gleichverteilt kam ein
  Progressive-Track auf einen Schwerpunkt von 220 Hz (Referenzen 420 bis 800). Dazu der Befund der Drop-Messung: im
  Breakdown ohne Sub senkt das Pad seinen Hochpass auf 70 Hz und spielt seinen Grundton eine Oktave tiefer (die
  Tiefmitten eines Progressive-Tracks 0.126, im Bereich; der Tiefband-Anstieg in den Drop +32 dB, noch über den +20 der
  Referenzen).
- **Runde 6 der Kalibrierung, mit den Presets** [M, drei Seeds je Stil, 21 Maße mit `drop_rise` und `drop_rise_low`]:
  Uplifting 14, Progressive 13, Dream House 8, Acid 14, Deep 11 von 21 auf allen Spuren im Bereich
  (`out/calib_run6.txt`). Besser: Uplifting steigt jetzt auch tief im Bereich in den Drop (+11.7 bis +14.1 dB, Referenzen
  +4.5 bis +16.1), breit +3.5 bis +3.7 (+1.3 bis +8.5). Offen für die Nachkalibrierung in 8: Progressive steigt tief in
  zwei von drei Seeds zu stark (+33 und +43 dB gegen höchstens +20: der Breakdown bleibt unter 150 Hz leer), Dream House
  hat zu dünne Tiefmitten (0.07 bis 0.13 gegen 0.14 bis 0.31) und steigt tief zu wenig (+9 gegen +11 bis +17), Deeps
  Lautheitsumfang bleibt klein (LRA 4.9 bis 5.7 gegen 7.6 bis 12), der Lautheitsumfang aller Stile liegt eher unten.
- **Tests**: testModulation (40 von 40 Zielen verändern den Klang; Uplifting mit Orchester und Dream House mit Piano,
  jede Matrix belegt, bei 1, 37 und 512 bitgleich), testPresetBank (18 × 1024, eindeutige Namen, jeder Knopf existiert,
  jede der 14 Rollen hat Gruppen, eine Stichprobe von 144 Presets endlich, hörbar, innerhalb von 24 dB des Standardklangs;
  mit PARH_BANK_TRIMS alle 18 432 gemessen, auf 16 Kernen), testComposer (die Tonart auf den Knöpfen); ctest 30/30.

**Phase 6, das Plugin (29.09.2026).** Nach 9 und 14 gebaut; die Oberfläche aus Totality (4d3c0d2, Herkunftsnotiz in
jedem Kopf; umbenannt Totality → Parhelion, TOT_ → PARH_, tot_ → parh_, der Plugin-Code Parh), jede Seite für
Parhelion neu belegt.
- **Zwölf Seiten**: Set (Compose, Set, DJ-Effekte), Arrange, Low End (Kick, Sub, Bass, 303, Pump), Drums (zwölf Lanes und
  die Busse), Synths (die sechs Poly-Stimmen mit Namen), Keys (Piano, Wolke), Orchestra (Streicher, Chor, Blech, Pauken),
  Effects (FX, Sends), Mixer (Meter, Busse, Glue, Master, Pump, drei Kanäle), Perform, Export, Style. Jede Stimme mit
  ihrem Modulationsblock als Gruppen (Mod-Hüllkurve, LFO 1 bis 4, Matrix je zwei Slots: vier Slots in einer Box brachen
  das Ziel eines Slots von seiner Quelle weg). Farben der Nacht (Palette in EditorTheme), das Logo als Nebensonne
  (`Deploy/make_icon.py`).
- **Das laufende Preset je Synth** (Wunsch des Nutzers): jede Stimme hat ihre Presetleiste (die 16 Gruppen als Untermenüs
  zu 64, Pfeile, "this track: Radiant Searchlight (Trance Gate Lead)"), die Arrange-Seite nennt in zwei Zeilen die
  Presets aller Synths des Tracks unter dem Abspielkopf; im Set die des Decks, dessen Knöpfe gelten, vor der ersten Note
  die des Tracks unter dem Kopf. **Nutzer-Presets** (nach Phosphene): "Save as user preset..." schreibt alle Knöpfe, die
  ein Preset setzt, als `knob=value` mit dem Schlüssel im Modul nach Documents/Parhelion/Presets/<synth> (die Kit-Lanes
  teilen "perc"), das Untermenü User lädt sie als ganzen Klang (`userPresetText`, `userPresetKnobs` in Core). Alle, nicht
  nur die vom Standard abweichenden: die Lanes haben eigene Standardwerte, und testUserPreset fand so 9 von 185 Knöpfen
  falsch, als ein Klang von Lane 3 nach Lane 8 ging.
- **Arrange**: die Layer-Matrix als Zeilen je Layer (offen hell, gefiltert matt), darüber die Energiekurve (im Set je
  Track und Deck-Farbe: eine durchgehende Linie kreuzte die Tracks des anderen Decks), die Zeile mit Stil, Form, Tempo,
  Tonart und Camelot, Progression, Bassfigur, Haupt-Drop, Orchester und Tonartwechsel; die fünfzehn Würfel-Knöpfe, im
  Set dazu der ganze Track und der Plan. Bewertung + und − mit Form und Preset-Gruppen (Preferences nach Core
  verschoben, `compose.use_ratings`).
- **Perform**: sieben Mutes (Kick, Bass, Hats, Perc, Lead, Synths, Pads; Tasten C3 bis F#3), Masterfilter (CC 74),
  Echo-Wurf (CC 11), das Modrad (CC 1, Quelle jeder Matrix), Kanaldruck; **Breakdown now** und **Drop now**: der
  Planer schreibt den laufenden Track ab der nächsten 8-Takt-Linie um (die übernächste, wo die nächste unter sechs
  Sekunden entfernt ist, damit der Komponist fertig wird): `planTrackRewritten` schneidet den Plan dort und setzt einen
  Breakdown (32 Takte, als zweiter nach dem Haupt-Drop 16) mit Build und Drop oder den Drop sofort (mit seinem Vakuum),
  das Ganze wieder ein Vielfaches von 32; geladen wird er, wo die Engine steht, mit den Pegelkorrekturen des alten.
  Selbsttest testRewrite: die Form ab der Linie und jede Note davor wie zuvor (2 Seeds × beide Arten). Ein Set wird
  nicht umgeschrieben (sein nächster Blend müsste mitwandern).
- **Style**: das Profil der Knöpfe auf sechs Achsen (Tempo, Drum-Dichte, Lead, Breakdown-Anteil, Hall im Breakdown,
  Palette nach `styleBrightness`) mit den fünf Profilen als Marken, daneben die Mediane der Referenzen je Stil.
- **Handbuch** (`Tools/manual`, nach Totality): `parh_render --dump-params` schreibt alle 1670 Parameter als JSON,
  `make_shots.py` fotografiert die zwölf Seiten im Standalone (Seed 4242 im Haupt-Drop, ganzes Fenster), die Prosa in
  `chapters.txt` (englisch, 16 Kapitel; jedes Modul muss einem Kapitel gehören, sonst druckt der Generator nicht),
  `make_manual.py` schreibt `docs/manual/Parhelion-Manual.html` und das PDF (Edge, kopflos). Dabei korrigiert: Texte aus
  Totality auf Export- und Mixer-Seite (Kick-outs, 4- und 8-Takt-Loops, Lautheitsziel), die Stilseite (Knöpfe in voller
  Höhe, Tabellenkopf).
- **Build**: `PARH_BUILD_PLUGIN` jetzt an (wie Totality), ein Baum `build` für Werkzeuge, Tests und Plugin.
- **Prüfsteine** [M]: pluginval Strenge 10 bestanden (alle Gruppen, auch Fuzz); `parh_vst3test` 32 von 32 (der Host lädt
  das VST3, Transport, Tempo, Sprung, Zustand byteweise zurück, zweite Instanz, Editor; CC 74 greift den Filter, das
  Modrad das Rad, Kanaldruck den Druck); testUserPreset (ein Klang von Lane 3 nach Lane 8, vom Lead zum Arp,
  185 von 185 Knöpfen genau); Selbsttest 125, ctest 34/34.

## 0. Kurzfassung

Ein Instrument, das aus einem Seed, einem Stilprofil und einer Set-Dramaturgie Trance komponiert und in Echtzeit
synthetisiert: fünf Stilprofile nach Dok. 2 (Dream House, Acid, Progressive, Cinematic/Uplifting, Deep/Ambient), zwischen
denen interpoliert wird, einzelne Tracks von 5 bis 10 Minuten und ganze DJ-Sets. Standalone und VST3 für Windows (JUCE),
nativ auf der Quest 2, MIDI-Export aller Stimmen und Automationen, Stems, DJ-Loops, Cue-Marken, Offline-Render als
Determinismus-Orakel.

Die drei Entscheidungen, die alles andere bestimmen:

1. **Trance ist ein Energie-Drehbuch, also kommt die Form zuerst** (Dok. 1: "Wer das automatisiert generieren will, muss
   dieses Drehbuch modellieren, nicht nur den Sound"). Phosphene schreibt Motive in Sektionen, Totality nimmt Loops weg;
   Parhelion **schichtet**. Ein Planer schreibt je Track drei Dinge in die Partitur, bevor eine Note entsteht: den
   **Sektionsplan** (Grammatik über Intro, Groove, Break, Breakdown, Build, Drop, Outro mit Längen aus {8, 16, 32, 64}),
   die **Layer-Matrix** (Element × 8-Takt-Block → aus, gefiltert, an) und die **Energiekurve** (0 bis 10 je 8-Takt-Block).
   Alle 8 Takte tritt ein Element hinzu oder verschwindet; der Breakdown ist kein Füllmaterial, sondern ein Mini-Song mit
   eigener Dramaturgie (Intro, Tease, volle Melodie, Rebuild); der letzte Takt vor dem Drop ist leer. Die Constraints
   von Dok. 6 sind harte Regeln des Planers, und was hörbar sein muss (Breakdown 4 bis 8 LU unter dem Drop), wird auf dem
   Audio gemessen, nicht nur geplant.
2. **Die Melodie ist die Identität eines Tracks** (anders als in Totality). Die Harmonik ist bewusst arm (drei
   Kernprogressionen, Dok. 4); was einen Track ausmacht, ist sein Motiv aus 2 bis 4 Takten, das im Breakdown angeteast,
   dann voll gespielt und im Drop in voller Oktavverdopplung wiederkommt, nie im Drop neu eingeführt. Regeln zuerst (die
   Leadregeln von Dok. 4, Phosphenes Phrasenbau A A' B A''), gelernte Statistik als zweite Stufe: Anders als Dok. 9 annimmt
   ("ein Trance-spezifisches MIDI-Korpus existiert nicht öffentlich"), liegt lokal eines vor (2.9): 1358 rollenbeschriftete
   Loops der EMP-Packs und 5452 Fan-Transkriptionen. Die Transkriptionen bekannter Tracks liefern nur Aggregate und sind
   die Referenz der Memorisierungsprüfung, damit kein bekanntes Motiv aus dem Generator kommt.
3. **Sidechain und Tiefband sind fest verdrahtet** (Dok. 7: "Trance-Mixing ist zu 80 % Low-End-Politik und Sidechain").
   Ein Ghost-Kick-Trigger auf allen Vierteln steuert je Bus eine parametrische Duck-Kurve (Tiefe, Attack, Hold, Release),
   auch im Breakdown ohne Kick, dort mit automatisierter Tiefe. Das ist genau Phosphenes und Totalitys ereignisgesteuerter
   `Ducker`. Die Frequenz-Pockets (Kick 45 bis 65 Hz, Sub 35 bis 100, Mid-Bass 80 bis 400, alles andere ab 150 bis 250 Hz,
   Hall-Returns ab 250 bis 300 Hz) sind die feste Kette jedes Kanalzugs; die Kick wird auf die Tonart gestimmt.

Reihenfolge der Arbeit: zuerst ein Loop, der **pumpt** (Kick, Offbeat-Bass mit Sub, Ghost-Kick-Duck, Hats, ein
Supersaw-Akkord mit Trance-Gate), dann der Planer mit Layer-Matrix und Energiekurve bis zu einem ganzen Track aus Kick,
Bass, Pad und Pluck, dann die Melodie, dann die Stimmen der Sub-Genres (303, Piano, Streicher und Chor), dann Set, GUI
und Quest (Abschnitt 14).

## 1. Ziel, Rahmen, Nicht-Ziele

**Ziel.** Auf Knopfdruck ein Track oder Set, das ein Kenner als stilistisch glaubwürdig hört: ein Breakdown, der
berührt, ein Drop, der auflöst, ein Motiv, das man nachsingen kann, ein Low-End, das auf einer Anlage trägt, und Intro
und Outro im 32-Takt-Raster, die ein DJ mischen kann. Jede Einheit (Form, Harmonie, Motiv, Sektion, Stimme, Klang) einzeln
sperrbar und neu würfelbar; alles reproduzierbar aus Seed, Profil und Sperren.

**Rahmen** (wie bei den Geschwistern).
- Plattformen: Windows x64 (AVX2), Quest 2 (arm64, NEON). Linux als Nebenprodukt des frameworkfreien Kerns.
- Sample-Rate 44,1/48/96 kHz, Blöcke 1 bis 2048; Quest 48 kHz, 256er Blöcke (Oboe).
- Kern ohne Framework, C++20, keine Allokation im Audio-Thread, kein Fast-Math; der Offline-Render ist das Orakel;
  Blockgrößen 1, 37 und 512 bitgleich; ein Takt allein gerendert gleich demselben Takt in der Folge.

**Nicht-Ziele** (Abschnitt 16).
- Kein Klon einzelner Künstler oder Tracks. Profile tragen beschreibende Namen; Künstler und Tracks stehen nur in der
  Dokumentation als Hörreferenz. Kein generiertes Motiv darf einem transkribierten bekannten Motiv gleichen (6.6).
- Kein Psytrance (das ist Phosphene), kein Hard Trance über 145 BPM, keine Vocals mit Text. Chor als Vokalfläche ("aah",
  "ooh") gehört zu Cinematic dazu.
- **Keine neuronale Audio-Erzeugung**, auch nicht als spätere Offline-Stufe (Dok. 8, Paradigma B; Entscheidung 16.1):
  über Stunden weder steuerbar noch deterministisch, auf der Quest nicht lauffähig. Dok. 10 empfiehlt sie für Pads,
  Orchester, Atmosphären und FX; Parhelion synthetisiert all das selbst.
- **Keine Samples**, wie bei Phosphene und Totality: auch das Piano von Dream House, Streicher, Chor, Brass und Pauke
  entstehen synthetisch (5.8, 5.9; Entscheidung 16.1).
- Kein Cloud-Modell in der Echtzeitschleife.

## 2. Was Trance ausmacht (die musikalische Spezifikation)

Das Recherchedokument ist die Spezifikation. Dieser Abschnitt fasst zusammen, was der Generator daraus macht; für Belege
verweist er auf Dok. Die Produktionszahlen des Dokuments (Sidechain-Tiefen, Hüllkurven, LUFS) stammen nach dessen eigener
Quellenangabe aus Produzenten-Tutorials, "Praxis-Konventionen, keine gemessenen Korpuswerte" (Dok., Quellen): sie sind
Startwerte und werden an Referenzen gemessen (13.4).

### 2.1 Fünf Sub-Genres (Dok. 2, 10)

| Profil | Tempo | Harmonischer Rhythmus | Lead | Bass | Breakdown-Anteil | Hall Break/Drop | SC-Tiefe Bass | Lautheit | Hörreferenz |
|---|---|---|---|---|---|---|---|---|---|
| **Dream House** | 128 bis 135 | 1 Akkord/Takt | Piano plus Pad-Oktave | Offbeat-Achtel | 30 bis 40 % | 3 s / 1,2 s | 8 dB | −8 LUFS | Robert Miles *Children*, *Fable*; Zhi-Vago; Dreamland |
| **Acid** | 135 bis 145 | statisch, Wechsel alle 16 Takte | TB-303, geschichtet | 303-Sequenz | 10 bis 20 % | 1,5 s / 0,8 s | 6 dB | −7 LUFS | Hardfloor *Acperience 1*, *Lost in the Silver Box*; Union Jack; Emmanuel Top; Jam & Spoon |
| **Progressive** | 124 bis 132 | 1 Akkord/2 bis 4 Takte | Pluck/Arp, dezenter Lead | synkopiert, Achtel | 20 bis 30 % | 2,5 s / 1,2 s | 8 dB | −8 LUFS | Sasha *Xpander*, BT, Markus Schulz, L.S.G. |
| **Cinematic/Uplifting** | 136 bis 142 | 1 Akkord/Takt, Kadenzen | Supersaw plus Streicher/Chor | Offbeat oder rolling 16tel | 40 bis 50 % | 6 s / 1,5 s | 10 dB | −8 LUFS | Andy Blueman *Time to Rest*, *Florescence*; Sasha *Xpander* (orchestral); Hybrid *Wide Angle* |
| **Deep/Ambient** | 90 bis 120 (auch ohne Beat) | 1 Akkord/2 bis 4 Takte, Drone | Pad, langsames Arpeggio | lang, Sub-Drone | 60 bis 100 % | 8 bis 12 s / – | 3 dB | −11 LUFS | Chicane *Offshore*, *Saltwater*; Delerium *Silence* (Chillout); Energy 52 *Café del Mar* (Ambient-Mix) |

Alle Zahlen [Q] aus Dok. 2, 3 und 10 (dort als Praxiswerte), zu kalibrieren. Dok. 2 hält fest, dass Dream House, Acid und
Progressive historisch belegte Stile sind, Cinematic und Deep/Ambient dagegen eher Playlist-Etiketten; die Grenzen sind
fließend. Der Generator bildet sie deshalb, wie Dok. 1 vorschlägt, als **Presets über einer gemeinsamen
Trance-Grammatik** ab: ein Profil ist ein Vektor von Gewichten und Bereichen (6.7), zwischen denen interpoliert wird.

### 2.2 Was sie verbindet und was sie trennt

**Verbindet** (Dok. 2): Moll-Tonalität, 4/4, 8-Takt-Phrasen, ein Arpeggio- oder Pluck-Layer als Motor, langsam modulierte
Pads, ein Spannungsbogen über Filteröffnung und Layerzahl statt über Harmoniewechsel.

**Trennt** (Dok. 2), fast vollständig in sechs Parametern: Tempo, Drum-Dichte, Lead-Archetyp, Breakdown-Anteil, Hall- und
Delay-Länge, Klangpalette (elektronisch, orchestral, organisch). Diese sechs sind die Hauptachsen der Profile und
erscheinen als Regler im Style-Tab.

**Abgrenzung zu Phosphene** (Psytrance, das nächste Geschwister): Psytrance rollt mit 16teln K-B-B-B bei 138 bis 150 BPM,
steht auf phrygischen und doppelt-harmonischen Farben und lebt von psychedelischen Effekten; Trance hat den Offbeat-Bass,
natürliches Moll, den emotionalen Breakdown mit Melodie und das Pumpen des Sidechains. Beide teilen den Supersaw und die
303, deshalb wandern viele Module aus Phosphene fast unverändert herüber (Abschnitt 4).

### 2.3 Tempo und Raster (Dok. 3)

- 16 Steps je Takt; bei 138 BPM Viertel 435 ms, 16tel 109 ms, Takt 1,739 s [A].
- Global 90 bis 145 BPM; Profilbereiche in 2.1. Deep/Ambient kennt eine Variante ohne Beat (dann ist das Tempo nur das
  Raster der Phrasen und der Delays).
- **Gerade quantisiert, kein Swing**: Humanisierung nur über Velocity und Akzent, nie über Timing (Dok. 3). Das ist eine
  harte Regel und der Unterschied zu Totality, dessen Rack Swing und Mikro-Timing hat.
- Alle Sektionslängen Vielfache von 8, meist 16 oder 32 Takte; Sektionswechsel auf 16- oder 32-Takt-Linien (Dok. 6).

### 2.4 Der Groove (Dok. 3)

Ein festes Raster: Kick auf jeder Zählzeit, Bass dazwischen, Hats auf den Offbeats; die Variation liegt in Dichte und
Klang, nicht im Pattern. Deshalb braucht Parhelion kein Pattern-Rack wie Totality, sondern je Lane ein kleines Vokabular
von Mustern mit Wahrscheinlichkeiten und die Regeln, wann sie wechseln.

| Element | Muster (Dok. 3) | Anmerkung |
|---|---|---|
| Kick | Viertel | fehlt in Breakdown, Mini-Break (1 Takt) und im leeren Takt vor dem Drop |
| Bass | Offbeat-Achtel ("dun-dun-dun"), rolling 16tel-Offbeat (K-B-B-B), Gallop (Achtel plus zwei 16tel), sustained/walking mit Synkopen auf "und" von 2 und 4, 303-Sequenz | zwei bis drei Linien parallel: Sinus-Sub, Mid-Bass mit Charakter, gelegentlich eine Achtellinie eine Oktave höher |
| Closed Hat | 16tel oder Achtel mit Akzentmuster | |
| Open Hat | Offbeat-Achtel (das "tsss") | |
| Clap/Snare | 2 und 4 | |
| Ride | Achtel oder Viertel im Drop | |
| Percussion | Shaker, Conga, Tambourine als Loops | alle 8 Takte hinzu oder weg |
| Übergänge | Snare-Roll, der sich von Vierteln über Achtel und 16tel zu 32teln verdichtet; Crash auf Takt 1 jeder 8er- oder 16er-Phrase; Reverse-Cymbal in den letzten ein bis zwei Takten vor einem Wechsel; ein bis zwei Zählzeiten Stille unmittelbar vor dem Drop | |

Je Profil (Dok. 3, Tabelle): Deep/Ambient weiche Kick, oft nur Sub, Shaker statt Hats; Progressive runde Kick, groovige
Synkopen, Percussion-Layer; Dream House trockene, kurze Kick, simpler Offbeat, Offbeat-Open-Hat; Cinematic/Uplifting
harte, gelayerte Kick, 16tel-Hats, Ride im Drop; Acid 909-Kick, 909-Hats, 303 als Bass.

### 2.5 Harmonik und Melodik (Dok. 4)

**Tonart.** Moll dominiert: 84,8 % von 604 Beatport-EDM-Tracks, häufigste Tonart F-Moll [Q] (Knees et al., ISMIR 2015).
Grundtöne zwischen D und A, damit Bass und Kick im gleichen Fenster liegen; A-Moll und F-Moll als Arbeitspferde (Dok. 4).
Harmonisch Moll punktuell: die übermäßige Sekunde zwischen b6 und 7 als "Trance-Intervall". Keine Modulation vor dem
zweiten Breakdown; in Cinematic danach häufig eine kleine Terz aufwärts ("Epic"-Effekt).

**Die Kernprogressionen** (in A-Moll, Dok. 4):

| Progression | Akkorde | Charakter | Einsatz |
|---|---|---|---|
| i–VI–III–VII | Am–F–C–G | Uplifting-Standard, offen, aufsteigend | Breakdown mit Melodie, Drop |
| i–VII–VI–VII | Am–G–F–G | kreisend, hypnotisch | Intro-Groove, Progressive, Acid |
| VI–VII–i (−i/−VII) | F–G–Am | kadenzartig, "lift" | letzte 8 Takte vor dem Drop, Dream House |
| i–iv–VI–VII | Am–Dm–F–G | melancholischer | Piano-Themen, Dream House, Deep |
| i–VI–VII–v | Am–F–G–Em | mehr Spannung durch Moll-Dominante | Cinematic |

**Satz.** Die Progression liegt als Pad und/oder rhythmischer Pluck ("Chord Stab"); die Basslinie folgt den Grundtönen und
signalisiert den Wechsel, während das Pad fast statisch "schwebt". Voicing-Regeln: Pads ab etwa 200 Hz (A3 als tiefste
Padnote in A-Moll), tiefste Stimme Terz oder Quinte, nie der Grundton (der gehört dem Bass); die None eine Oktave höher;
sus2, sus4 und Power-Chords als Zwischenfarben; offene Lagen für Pads, enge für Plucks; im Build der bVII über die letzten
zwei Takte gehalten, Auflösung nach i auf Zählzeit 1 des Drops.

**Rhythmisierte Akkorde.** Kurze Noten in einem 8- oder 16-Step-Muster, das sich wiederholt, vorhersehbare Intervalle mit
ein bis zwei Überraschungsnoten; der Pluck spielt oft nur Grundton und Quinte oder gebrochene Dreiklänge.

**Arpeggios.** 16tel (up, up-down, random) über den Akkordtönen plus Oktave, oft mit Gate-Muster; in Deep/Ambient
langsamer (Achtel, Triolen) und in Hall gebadet, eher Textur.

**Lead.** Zwei bis drei Oktaven gleichzeitig (Supersaw plus Oktave); Motiv von 2 bis 4 Takten, in 8-Takt-Phrasen mit
Variation der letzten zwei Takte wiederholt; Tonvorrat Moll-pentatonisch oder äolisch; große Intervalle (Sexte, Oktave) an
Phrasenanfängen, stufenweise Abwärtsbewegung zum Phrasenende. Dream-House-Piano-Themen folgen demselben Bauplan, legato,
mit Sustain-Pedal und weniger Oktavverdopplung.

**Je Profil** (Dok. 4): Dream House i–VI–VII oder i–iv–VI–VII, ein Akkord je Takt, das Piano trägt; Acid ein Akkord über
16 bis 32 Takte, die "Harmonik" ist die 303-Sequenz, chromatisch oder mit Slides durch Nicht-Skalentöne, Wechsel um Quarte
oder Quinte alle 16 Takte; Progressive zwei Akkorde über 8 Takte, Dorisch kommt vor; Cinematic klassische Kadenzen
(i–VI–III–VII plus V7 aus harmonisch Moll im Breakdown), Streicher-Kontrapunkt, Chor-Stimmführung, die Terz-Modulation;
Deep/Ambient add9- und maj7-Voicings, ein Akkord je 2 bis 4 Takte, oft ohne klare Kadenz, Drones auf i.

**Übernommen aus Phosphene** (Vorschlag 16.2): die Regel des Nutzers vom 25.09.2026 "keine kleine None in der Fläche" (kein
gehaltener Pad- oder Drone-Ton einen Halbton über dem Bassgrundton oder dem eigenen Akkordgrundton, kein gehaltener
Leitton unter der Tonika), der Bass als Orgelpunkt im Acid-Profil, die Registerwache zwischen Lead, Counter, Stab und Arp.

### 2.6 Klang: acht Archetypen (Dok. 5)

Jeder Archetyp ist eine bekannte Syntheserezeptur, die sich als Preset in einem eigenen Synth abbilden lässt; je Archetyp
tragen wenige Regler die Varianz (Dok. 5, letzter Absatz), alles andere ist fest. Diese Regler sind die "Klangknöpfe", die
der Komponist je Track zieht; die übrigen Parameter kommen aus den Presets (1024 je Synth in 16 Gruppen, wie die
Geschwister).

| Archetyp | Rezeptur (Dok. 5) | Freie Regler |
|---|---|---|
| **Kick** | gelayert: Sub-Sinus-Sweep 150 → 50 Hz in ~30 ms, Body 100 bis 180 Hz (909-artig), Click 2 bis 5 kHz; ~2:1 Kompression, langsamer Attack; auf die Tonart gestimmt, Kick- und Bassgrundton nie einen Halbton auseinander | Tuning, Body, Click, Decay |
| **Sub** | monophoner Sinus, Hochpass ~35 Hz, Tiefpass 100 bis 120 Hz | Pegel |
| **Mid-Bass** | Sägezahn (oder Säge plus Rechteck), 24-dB-Tiefpass mit kurzer Filterhüllkurve, kein Unison, mono, leichte Sättigung; 100 bis 400 Hz; Progressive und Deep: längere Decays, Reese-artige Detune-Bässe, gefilterte Rechtecke | Decay, Cutoff, Sättigung |
| **Supersaw-Lead** | Osc A: 7 Sägen, Detune ~0,25 bis 0,30 (Serum-Skala), Blend ~0,8; Osc B: 3 Stimmen, Detune ~0,15, eine Oktave tiefer; optional Sub-Saw −12 ohne Unison; Sättigung nach der Unison-Summe; 2 bis 4 dB Dip bei 200 bis 400 Hz; Hochpass, bis der Lead solo dünn klingt; Detune über 0,4 lässt das Fundament im Mix verschwinden | Stimmen, Detune, Oktav-Blend, Cutoff, Hallgröße |
| **Pluck** | Säge plus Rechteck oder Dreieck, Amp 0 ms Attack, 200 bis 300 ms Decay, kein Sustain, 150 bis 250 ms Release; Filterhüllkurve öffnet kurz; Body-Layer (mittig, leicht verstimmt) plus Top-Layer (hell, breit); Ping-Pong-Delay punktiertes 16tel oder Achtel, 20 bis 30 % Feedback, 15 bis 25 % Wet; kurzer Stereo-Hall 0,8 bis 1,2 s, Pre-Delay 20 bis 40 ms | Decay, Filter-Env-Menge, Delay-Teilung |
| **Pad, Streicher** | mehrere verstimmte Sägen oder Wavetables, Attack 0,5 bis 2 s, Tiefpass öffnet per Automation über den Breakdown, Chorus/Ensemble, langer Hall; Cinematic: Streicher; Deep: granular, langsame LFOs auf Filter und Pan; im Drop 3 bis 5 dB Sidechain, nie mehr | Attack, Cutoff-Start, Cutoff-Ziel, LFO-Rate |
| **Trance-Gate** | Step-Sequencer auf der Lautstärke eines gehaltenen Pads, 16 Steps, Achtel- oder 16tel-Muster wie x.xx.x.xx.x.x.x.; Attack und Release des Gates bestimmen die Härte | Muster, Härte |
| **TB-303** | monophon, Säge oder Rechteck, 18-dB-Diodenleiter mit Resonanz, eine Decay-Hüllkurve für Amp und Filter, 16-Step-Sequenzer mit Accent und Slide; Verzerrung (DS-1-artig); geschichtet (Sub-303 ohne Resonanz, Lead-303 mit Slides, FX-303 mit Selbstoszillation); vier Automationskurven (Cutoff, Resonanz, Env-Mod, Decay), die über 16 bis 64 Takte steigen und an Phrasengrenzen zurückspringen | Cutoff, Resonanz, Env-Mod, Decay, Accent, Verzerrung |
| **Dream-House-Piano** | leicht dumpfes Piano (kein moderner Konzertflügel, Kurzweil K2000), Achtel- oder punktiertes Delay mit 25 bis 35 % Feedback, Hall 2 bis 4 s, oft ein Oktav-Layer aus Synth-Pad; legato mit Sustain-Pedal; die Kick darunter trocken und kurz | Helligkeit, Delay, Hall |
| **FX** | White-Noise-Riser (Hochpass-Sweep über 8 bis 16 Takte), Downlifter nach dem Drop, Reverse-Crash, Impact auf Zählzeit 1, gefilterte Delay-Throws auf der letzten Note vor dem Breakdown, Sub-Drop vor dem Drop; Cinematic: Braams, Pauken, Chor-Swells; Deep: Field Recordings, Vinyl-Rauschen, granulare Texturen | Länge, Farbe |

### 2.7 Form (Dok. 6)

**Referenz-Arrangement Uplifting/Cinematic** (Dok. 6, konsolidiert aus Myloops), 138 BPM:

| Takte | Sektion | Länge | Was passiert | Energie |
|---|---|---|---|---|
| 1–64 | DJ-Intro | 64 | Kick allein, ab 9 Hats, ab 17 Bass, ab 25 Open Hat, ab 33 Percussion, ab 49 gefilterter Pluck/Stab (hochpassgefiltert); keine Melodie | 2 → 4 |
| 65–96 | Erster Groove | 32 | voller Bass, Pluck ungefiltert, Pad-Andeutung, Mini-Break bei Takt 80 (Kick raus für einen Takt) | 5 |
| 97–128 | Erster Breakdown (kurz) | 32 | Kick und Bass raus, Pad plus Melodie-Fragment, Filter öffnet; Snare-Roll 125–128 | 3 → 6 |
| 129–160 | Drop 1 | 32 | alles zurück, Lead in erster Version, Mini-Break um Takt 144 | 8 |
| 161–256 | Haupt-Breakdown | 64–96 | eigene Dramaturgie: Intro (nur Pad), Motiv-Tease, volle Melodie mit Streichern/Chor, Rebuild mit Kick-Einsatz, Snare-Roll, Riser; letzte 1 bis 2 Zählzeiten Stille | 1 → 7 |
| 257–288 (–320) | Haupt-Drop | 32–64 | Lead in voller Oktavverdopplung, Ride, Pumpen, Variation nach 16 Takten (Lead eine Oktave höher oder Gegenstimme) | 10 |
| 289–320 | Zweiter Breakdown (optional) | 16–32 | kürzer, greift das Thema auf oder bringt eine Variante | 5 |
| 321–352 | Finaler Drop | 32 | Wiederholung mit zusätzlichem Layer | 9 |
| 353–416 | Outro | 64 | Umkehrung des Intros; gefilterter Lead geistert 16 Takte nach, dann reines Rhythmusgerüst; kein Fade, kein harter Schnitt | 6 → 2 |

**Dramaturgie-Regeln als Constraints** (Dok. 6, wörtlich übernommen als harte Regeln des Planers):
1. Alle 8 Takte tritt ein Element hinzu oder verschwindet; alle 16 oder 32 Takte wechselt die Sektion.
2. Der emotionale Höhepunkt liegt im Breakdown, der physische im Drop; der Breakdown ist ein 32-Takt-Mini-Song mit Intro,
   Tease, Peak und Rebuild.
3. Lautheitsabstand Breakdown → Drop 4 bis 8 LU kurzzeitig; ist der Breakdown lauter als der Drop, ist die Energiekurve
   falsch.
4. Spannung auf drei Zeitskalen: Takt (Percussion, Rolls), Phrase (Akkordwechsel, Bass-Einsatz), Sektion (Filter-Cutoff,
   Hallgröße).
5. Der letzte Takt vor dem Drop ist leer oder fast leer, nicht laut: Kontrast schlägt Lautstärke.
6. Neue Melodien werden im Breakdown eingeführt, nie im Drop; der Drop bringt Gehörtes in voller Instrumentierung.
7. Intro und Outro enthalten keine melodische Verpflichtung; sie sind Werkzeug für den DJ.

**Varianten je Profil** (Dok. 6, Tabelle): Dream House Intro 16 bis 32 (Piano-Motiv oft schon angedeutet), Breakdown 30 bis
40 % als Piano-Solo über Pad, moderater Drop (Kick zurück, Piano bleibt Hauptstimme), 6 bis 7 min (Radio 3:30); Acid Intro
32 bis 64 Takte 909-Groove, Breakdown 10 bis 20 % als kurze Kick-Pausen statt Melodie-Break, Höhepunkte sind Filterpeaks
der 303, 6 bis 9 min; Progressive Intro 64, sehr allmählich, 20 bis 30 % in mehreren kleinen Breaks, mehrere Plateaus
statt eines Peaks, 8 bis 12 min; Cinematic Intro 32 bis 64, oft orchestraler Cold-Open ohne Beat, 40 bis 50 % Breakdown
als orchestrale Miniatur, maximaler Kontrast, 7 bis 9 min (Epic Mix 10+); Deep/Ambient fließend, oft ohne Kick-Einsatz,
60 bis 100 % Breakdown ("die Struktur ist der Breakdown"), weicher oder fehlender Drop, 5 bis 10 min.

### 2.8 Mix und Master (Dok. 7)

| Element | Bereich | Regel |
|---|---|---|
| Kick | Grundton 45 bis 65 Hz, Körper 100 bis 180, Click 2 bis 5 kHz | besitzt Downbeat und Sub-Thump; kleine Dips bei 55 und 110 Hz schaffen Platz für Bass-Obertöne |
| Sub-Bass | 35 bis 100 Hz, mono, Sinus | Hochpass ~35 Hz, Tiefpass 100 bis 120 Hz; 10 bis 12 dB Sidechain |
| Mid-Bass | 80/100 bis 250/400 Hz | Charakter und Hörbarkeit auf kleinen Lautsprechern; 6 bis 10 dB Sidechain |
| Pluck, Pad, Lead, FX | ab 150 bis 250 Hz | Hochpass auf allem, was nicht Kick oder Bass ist; Supersaw-Dip 200 bis 400 Hz; Pads ab ~200 Hz voicen |
| Hall- und Delay-Returns | ab 250 bis 300 Hz | Hochpass am Send-Eingang, sonst "nasse Decke" im Breakdown |

**Sidechain** (Dok. 7): Ghost-Kick-Trigger auf allen Vierteln, damit die Pump-Form auch im Breakdown steuerbar und für
alle Busse identisch ist; Bass 6 bis 12 dB, schnellster Attack, Release 80 bis 150 ms (bei rollenden 16teln absichtlich
"überschießend"); Pads 3 bis 5 dB, Lead-Bus 1 bis 2 dB, Hall- und Delay-Returns 3 bis 5 dB; im Breakdown die Tiefe per
Automation auf null, zwei Takte vor dem Drop wieder hoch. Die richtige Abstraktion ist eine parametrische Duck-Kurve
(Tiefe, Attack, Hold, Release-Form) je Bus: das ist der `Ducker` der Geschwister.

**Raum**: zwei parallele Hall-Sends, eine kurze Platte (1,2 bis 1,8 s, Pre-Delay ~20 ms) für Kleber, eine große Halle
(4 bis 8 s, Pre-Delay 50 bis 80 ms) für Emotion; Decay sektionsabhängig (Breakdown 2 bis 4 s, Drop 0,8 bis 1,5 s), die
Return-Pegel zwischen Sektionen automatisiert. Delays punktierte Achtel oder 16tel als Ping-Pong (Feedback 20 bis 55 %),
dazu Delay-Throws als Übergangseffekt.

**Kompression und Bus**: subtil, weil die Dynamik Breakdown/Drop Teil der Wirkung ist. Kick ~2:1, langsamer Attack; Bass
zweistufig (glätten, dann Sidechain); Drum-Bus leichter Glue; Master seriell mit ~0,5 dB GR, dann eine zweite sanfte
Stufe.

**Lautheit**: Release-Master −7 bis −9 LUFS integriert, −1,0 dBTP; Breakdown → Drop 4 bis 8 LU kurzzeitiger Abstand;
Deep/Ambient −10 bis −12 LUFS mit längeren Hallräumen (6 bis 12 s) und weniger Sidechain; Acid mehr Sättigung auf dem
Master und ein trockeneres Bild; Cinematic braucht Multiband-Kontrolle, weil Orchester und Supersaw dieselben 1 bis 4 kHz
besetzen; Dream House lässt das Piano dominieren und hält die Kick trockener und leiser als Uplifting.

### 2.9 Das lokale Korpus (gemessen 29.09.2026)

Unter `M:\Midi\VORTEX ULTIMATE TRANCE BUNDLE` liegt ein gekauftes Trance-MIDI-Bündel, das Phosphene erwähnt, aber nie
benutzt hat (Phosphenes `build_corpus.py` liest nur die drei Psy-Packs). Gelesen mit Phosphenes MIDI-Leser
(`Tools/corpus/build_corpus.py`, `read_midi_tracks`):

| Pack | Dateien | Takte (Median) | Inhalt |
|---|---|---|---|
| EMP Trance Melodies | 500 | 8 | einstimmige Leads |
| EMP Trance Chords | 200 | 8 | Akkordfolgen |
| EMP Trance Pads | 250 | 8 | Pad-Voicings |
| EMP Trance Pianos | 100 | 8 | Piano-Figuren |
| EMP Trance Basslines | 200 | 8 | Basslinien |
| EMP Trance Acid | 108 | 8 | 303-Linien |
| 5500 Trance Midi Famous Artist | 5488 (5452 lesbar) | 8 (p90 33) | Fan-Transkriptionen bekannter Tracks (nonstop2k-Stil); 28 % mit drei oder mehr Spuren, 14 % mit Drums, nur 6 % mit 64 Takten oder mehr |

Befunde [M]: Das Korpus besteht fast ganz aus 8-Takt-Loops und Hauptthemen, nicht aus ganzen Arrangements (etwa 330
Dateien mit 64 Takten oder mehr): **Patterns lernen, Form regeln**, wie bei Phosphene. Ein Krumhansl-Profil über die
Tonhöhenklassen trennt Moll nicht sauber von der parallelen Dur-Tonart (i–VI–III–VII in A-Moll hat dieselben Töne wie
vi–IV–I–V in C-Dur): es meldet 61 % Moll in den Transkriptionen und 40 bis 52 % in den EMP-Packs, häufigster Grundton
A-Moll (741 von 5452). Die richtige Messung liest die Tonart am Bassgrundton und am Schlussakkord (Phase 3). Die
5500 Transkriptionen sind Nachschriften fremder Kompositionen: sie dienen nur Aggregaten (harmonischer Rhythmus,
Akkordfolgen, Phrasenlängen, Intervallverteilungen) und als Referenz der Memorisierungsprüfung (6.6), nie als
Tonhöhenmodell (Vorschlag 16.2).

Das lokale Referenzaudio (Album-Tag "Trance Collection" unter `C:\Users\Rene\Desktop\Kandidaten\Pop - Kopie`, 35 Titel)
ist überwiegend Eurodance und Radio-Edits der Neunziger (Culture Beat, Snap!, La Bouche, Blank & Jones ...); für die fünf
Profile taugen daraus nur Robert Miles *Children* (Dream House), Kai Tracid (6 Titel, Hard Trance/Acid-Nähe) und
Resistance D. Die Referenzliste braucht deshalb Ergänzungen (13.4, Vorschlag 16.2).

### 2.10 Stand der Technik (Dok. 8, 9)

Dok. 9 ist die Übersicht und wird hier nicht wiederholt. Die Kurzfassung: Es gibt kein Paper, das Trance-Subgenres als
Zielraum eines Generators behandelt (Dok. 9, "Forschungslücke"); die Produktionsregeln stehen in Blogs und Tutorials.
Für Form und Patterns ist der regelbasierte Weg (Paradigma A) nach Dok. 8 der robusteste, weil das Genre so formelhaft
ist; Eigenfeldt und Pasquier (2013, Markov-Ketten und evolutionäre Algorithmen für EDM-Wiederholungsstruktur) sind die
direkte Vorlage des Planers. Neuronale Audio-Modelle (Stable Audio 3, ACE-Step 1.5, Magenta RealTime 2) liefern Klang
schnell, aber weder Bar-Genauigkeit noch Tonart noch Low-End sicher (Dok. 8, 10) und laufen nicht auf der Quest.

Eigener Stand, der hier zählt: Phosphene (Supersaw nach Szabo, 303 auf der Diodenleiter, Trance-Gate, Melodik mit
Constraint-Markov und Korpus, Form mit Breakdowns und Drops, Risers und Impacts, Korpus- und Trainingswerkzeuge, eigene
Transformer-Inferenz), Totality (neuestes Gerüst, Decks, DJ-Mixer und Set-Komponist, Leveler mit Balance gegen die Kick,
Rekordbox-Export, Bewertungen, Referenzmessung), Ephemeris (Schaltungsfilter, Gesten-Engine, Streichermaschine, Chor aus
Formanten, Platte, Bandecho) und Noctuary (Faltung, Granularwolke, Modenkörper, Mid/Side, spektrales Ducking).

### 2.11 Das Dokument, geprüft

| Punkt | Urteil | Was Parhelion daraus macht |
|---|---|---|
| Referenz-Arrangement "ca. 8 Minuten bei 138 BPM" (Dok. 6) | **Rechnung stimmt nicht**: 416 Takte zu 1,739 s sind 12:03 min; 8 Minuten sind 276 Takte | die Tabelle ist die Reihenfolge und das Verhältnis der Sektionen; die Längen wählt der Planer aus dem Zeitfenster des Profils (Uplifting 7 bis 9 min ≈ 240 bis 312 Takte) |
| Haupt-Breakdown 64 bis 96 Takte mit "8 Intro, 8 Tease, 8–16 Melodie, 8 Rebuild" (Dok. 6) | innere Dramaturgie summiert sich auf 32 bis 40 Takte | die vier Teile skalieren mit der Länge (je 8 oder 16); 32 bis 64 als Bereich, 96 nur im Epic-Mix |
| Dream House 128 bis 135 BPM (Dok. 2) | zu messen | *Children* liegt lokal vor und wird gemessen (13.4) |
| Supersaw-Detune "0,25 bis 0,30 (Serum-Skala)" | Skala nicht übertragbar | auf Szabos JP-8000-Kurve abbilden, die Phosphenes Supersaw schon spielt: an der Spreizung in Cent messen, nicht am Knopfwert [I] |
| "Ein Trance-spezifisches MIDI-Korpus existiert nicht öffentlich" (Dok. 10, offene Punkte) | stimmt öffentlich; lokal liegt eines vor | 2.9, 6.6 |
| Pipeline-Stufe 5: Downbeat-Tracking, Drop-Detektion, Struktur-Segmentierung (Dok. 10) | für eigene Synthese nicht nötig: die Partitur kennt jeden Downbeat | nur als Prüfung des Hörbaren: Neuheitskurve (Foote) auf dem Render gegen die geplanten Sektionsgrenzen, Kurzzeit-Lautheit Breakdown gegen Drop, Tonart auf dem Audio gegen die geplante (13.5) |
| Paradigma B/C für Pads, Orchester, FX (Dok. 8, 10) | nicht (Entscheidung 16.1) | eigene Synthese (5.7 bis 5.10) |
| Sidechain-, Hüllkurven- und LUFS-Werte | Praxiswerte aus Tutorials, laut Dok. selbst nicht gemessen | Startwerte, an Referenzen gemessen (13.4); Pump-Tiefe je Band als eigene Messgröße |

### 2.12 Stand der Technik je Baustein (Vorgabe des Nutzers, 29.09.2026)

"Wir wollen da gerne das SOTA-Modell haben, ebenso wie in allen anderen Bereichen." Jeder Klangerzeuger folgt dem besten
echtzeitfähigen Modell der Literatur, und wo ein Geschwister es schon gebaut und gemessen hat, wird es übernommen. Die
Tabelle nennt je Baustein das Modell und seine Quelle; wo Parhelion es neu baut, steht die Recherche im Abschnitt des
Erzeugers.

| Baustein | Modell | Quelle | Herkunft |
|---|---|---|---|
| Kick | geschlossene Phase, Resonator nach der 808-Schaltung, 909-Topologie | Werner, Abel und Smith, DAFx 2014; Reid 2002 | Totality |
| Supersaw | Detune-Polynom und Mix-Kurven des JP-8000, gemessen | Szabo 2010 | Phosphene |
| Oszillatoren | PolyBLEP, mipmap-Tafeln, ADAA an Nichtlinearitäten | Välimäki; Parker et al. 2016 | Phosphene, Ephemeris |
| Filter | Schaltungsmodelle (Moog, SEM, Prophet, Juno, Diode, Korg35 ...) mit Newton-Schritten | Huovilainen 2004; Zavalishin 2012; D'Angelo und Välimäki | Ephemeris |
| 303 | Diodenleiter, Accent-Sweep, Slide nach den Schaltungsanalysen | Open303-Analysen, Stinchcombe | Phosphene |
| Piano | gekoppelte modale Saiten, Stulov-Hammer, Kirchhoff-Carrier und Längsmoden, Resonanzboden aus der Plattenphysik als Parallelfilter | Pianoteq-Patent; Bank et al. 2010; Bank und Chabassier 2019; Ege et al. 2013; Russo, Ducceschi und Bilbao 2026 | neu (5.8) |
| Streicher | gestrichene Saite mit nichtlinearer Reibung, modaler Korpus | Desvages und Bilbao 2016; Smith; Serafin | neu (5.9) |
| Brass | Lippenventil plus Rohr mit Schallstück | Adachi und Sato 1996; Harrison, Bilbao et al. 2015 | neu (5.9) |
| Pauke | luftbelastete modale Membran | Rhaouti, Chaigne und Joly 1999; Papiotis und Papaioannou | neu (5.9) |
| Chor | LF-Glottisquelle, Klatt-Formanten, Ensemble | Fant et al. 1985; Klatt 1980 | Ephemeris, verbessert |
| Percussion | modale, Rausch-, Metall- und FM-Engines; 909-Metalltabelle | Werner et al. (808-Becken) | Totality |
| Hall | Platte nach Dattorro, FDN, Faltung mit erzeugten Antworten | Dattorro 1997; Jot; Välimäki et al. 2012 | Ephemeris, Noctuary |
| Kompressor, Limiter | Feed-forward nach Giannoulis, True-Peak nach BS.1770 | Giannoulis, Massberg und Reiss 2012 | Totality |
| Sidechain | ereignisgesteuert, sample-genau | eigene Arbeit (Phosphene) | Totality |
| Melodik | Constraint-Markov variabler Ordnung, später kleiner Transformer | Pachet und Roy 2011; Begleiter et al. 2004 | Phosphene |

Neuronale Klangerzeuger (DDSP, Diffusion) sind der Stand der Technik der Audio-Modelle, aber ausgeschlossen (16.1); wo sie
Parameter aus Aufnahmen schätzen helfen, dürfen sie Werkzeug der Kalibrierung sein.

## 3. Architektur

```
 Stilprofil(e) + Seed + Set-Dramaturgie + Sperren
            │
            ▼
   ┌───────────────────────────────┐  Partitur: Sektionen, Layer-Matrix,         ┌────────────────────────────────────┐
   │ Composer-Thread               │  Energiekurve, Noten je Stimme,             │ Audio-Thread                       │
   │ Set → Track → Sektion →       │  Automationskurven, Ghost-Kick, Marken,     │ Deck A │ Deck B │ (Deck C)          │
   │ 8-Takt-Block → Takt → Step    │  Cues; 16+ Takte voraus                     │  je Deck: Kick, Sub, Mid-Bass, 303,│
   │ Planer (Grammatik, Matrix,    │ ────────── lock-free Queue ──────────────▶ │  Pluck, Lead, Arp, Pad/Streicher,  │
   │ Energie), Harmonie, Melodik,  │                                             │  Piano, Chor/Orchester, Kit, FX    │
   │ Kandidaten + Prüfung          │ ◀── Position, Live-Eingriffe (Breakdown     │  → Kanalzüge (HP, Pockets, Duck,   │
   └───────────────────────────────┘     jetzt, Drop jetzt, Filter, Throw) ───── │    Gate) → Sends (Platte, Halle,   │
            │                                                                    │    Ping-Pong) → Track-Bus          │
            ▼                                                                    │ DJ-Mixer → Master (Glue, Clip, TP) │
   MIDI (SMF 1), Stems, DJ-Loops, Cue-Marken, .parhset, OSC, Rekordbox-XML      └────────────────────────────────────┘
                                                                                 Offline-Render (Orakel), Leveler
```

**Der Unterschied zu den Geschwistern.** Phosphene schreibt Motive in Sektionen einer Psy-Grammatik, Ephemeris lässt ein
Sequenzer-Rack voraus laufen, Totality ein Pattern-Rack mit Block-Operationen. Parhelion lässt einen **Planer** voraus
laufen, der für jeden Track Sektionsplan, Layer-Matrix und Energiekurve festlegt; die Stimmen schreiben ihre Noten in
diesen Rahmen, und alle Automation (Filter, Hallgröße, Sidechain-Tiefe, Return-Pegel, Register) ist eine Funktion der
Energiekurve und der Sektion. Die Partitur enthält die Layer-Matrix selbst, damit Sperren und Neuwürfeln auf der Ebene
von Sektion, Block und Stimme arbeiten und die GUI sie zeigen kann.

**Decks** wie in Totality: ein Deck ist ein vollständiges Instrument für einen Track (alle Stimmen, Kanalzüge, Sends,
Track-Bus); ein Set braucht zwei, während eines Blends beide aktiv; ein drittes für Borgen (Tease der nächsten Hookline,
6.8). Außerhalb von Blends rechnet nur ein Deck.

**Schichten im Kern (`Core/`):**
- `parh/Vec.h`, `parh/Dsp.h`, `parh/Adaa.h`, `parh/Halfband.h`, `parh/Oversample.h`: SIMD, Grundbausteine, Oversampling.
- `parh/Clock.h`, `parh/Score.h`, `parh/Params.h`, `parh/Presets.h`, `parh/Midi.h`, `parh/SetFile.h`, `parh/Cue.h`,
  `parh/Loudness.h`, `parh/Leveler.h`, `parh/Deck.h`, `parh/Engine.h`, `parh/Export.h`.
- `parh/compose/*`: Planer (Form, Layer-Matrix, Energie), Harmonie, Melodik (Lead, Counter, Arp, Pluck, Piano, Bass,
  Acid), Stilprofile, Set, Korpus, Kandidaten und Prüfung, Gesten-Engine.
- `parh/synth/*`: Kick, SubBass, Synth (Mid-Bass, 303), Poly (Supersaw, VA, FM, Wavetable), Kit, Piano, Strings, Choir,
  Brass, Sfx, Filter, Oszillatoren, Modulation.
- `parh/fx/*`: Platte, Halle (FDN, auf dem Desktop optional Faltung), Tempo-Delay (Ping-Pong), Chorus/Ensemble, Phaser,
  Granularwolke, Sättigung, Dynamik.
- `parh/mix/*`: Kanalzug, Ducker, Multiband-Ducker, Trance-Gate, Bus, Deck, Isolator, Master.

**Threads und Determinismus** wie bei den Geschwistern: Audio allokiert nie; der Komponist arbeitet in Häppchen mit der
Frist "Queue nie unter 16 Takten"; ein RNG je Modul mit `fork()` aus dem Seed; der Ghost-Kick ist ein Ereignis der
Partitur, kein Pegeldetektor.

## 4. Wiederverwendung

**Modulkopie, kein Link**, wie bisher; jede kopierte Datei nennt im Kopf Herkunft und Stand (Commit). Das **Gerüst kommt
aus Totality**, weil es die neueste Fassung der gemeinsamen Architektur ist (Unterordner, Decks, Set, Leveler mit Balance,
Export, Bewertungen, Presets als Programmwechsel, Test-Gerüst, Release-Pipeline); die **melodischen Stimmen und alles
Trance-Nahe aus Phosphene**; Streicher, Chor und Gesten aus Ephemeris; Räume und Texturen aus Noctuary.

| Modul | Herkunft | Einsatz hier | Anpassung |
|---|---|---|---|
| `Vec.h`, `Dsp.h`, `Adaa.h`, `Halfband.h`, `Oversample.h`, `Clock.h`, `WavWriter.h`, `Loudness.h` | Totality 4d3c0d2 | überall | Namensraum |
| `Params.h`, `Presets.h` (1024 je Synth, 16 Gruppen, Programmwechsel), `Score.h`, `Midi.h`, `SetFile.h`, `Cue.h`, `Export.h` (Rekordbox-XML), `Preferences.h` | Totality | Parameter, Partitur, Export | Module `kick`, `bass`, `acid`, `lead`, `pluck`, `arp`, `pad`, `piano`, `strings`, `choir`, `kit`, `fx`, `sends`, `deck`, `djmix`, `master`; Sektionen, Layer-Matrix und Energiekurve in der Partitur; `.parhset`; OSC `/parh/...` |
| `Deck`, `Engine`, `Leveler` (Lautheit und Balance jeder Stimme gegen die Kick), `compose/Set` (Dramaturgien, Blends, Bass-Swap, Decks, Isolator, Hand des DJs) | Totality | Decks, Set, Pegel | Stimmen neu; Balance-Fenster nach Dok. 7; Prüfung Breakdown → Drop 4 bis 8 LU; Blends über Intro/Outro der Trance-Form (6.8) |
| `compose/Style` (Profile mit Morph), `compose/GestureEngine` (Minimum Jerk, zwei Hände), `compose/Corridor` (Kandidaten gegen einen Korridor) | Totality | Profile, Automationskurven, Kandidaten | fünf Profile nach 2.1; Korridor misst die Constraints von Dok. 6 statt Hypnose |
| `synth/Kick` (Sweep, Resonator, 909, 909-Topschicht, geschlossene Phase), `synth/SubBass` (phasenstarr zur Kick), `synth/Synth` (Mono-Synth mit Schaltungsfiltern, Accent, Slide) | Totality | Kick (5.1), Sub, Mid-Bass und 303-Rolle (5.2, 5.3) | Kick-Schichten Sub/Body/Click nach Dok. 5, Stimmung auf den Grundton; Offbeat-, Rolling-, Gallop- und Walking-Muster |
| `synth/Kit`, `PercKernel` (12 Lanes, fünf Engines, 909-Metalltabelle) | Totality (aus Phosphene) | das Kit (5.4) | Rollen: CH, OH, Ride, Clap, Snare, Crash, Shaker, Tambourine, Conga, Tom, Rim, Reverse-Crash; Snare-Roll-Generator aus Phosphenes `Rhythm` |
| `synth/Filters.h` (Moog, SEM, Prophet, Juno, Diode, Xpander, Korg35, Polivoks, Wasp) | Totality/Phosphene (aus Ephemeris) | alle Stimmen | keine |
| `mix/Ducker` (Tiefe, Attack, Hold, Release-Form, ereignisgesteuert) und Multiband-Ducker | Totality (aus Phosphene) | Ghost-Kick-Sidechain je Bus (7.3) | Tiefe als Automation der Energiekurve |
| `fx/Plate`, `fx/Reverb`, `fx/Dynamics`, `fx/Cloud` | Totality (aus Ephemeris, Noctuary) | Platte, Halle, Bus, Master, Granularwolke (Deep) | Decay sektionsabhängig; Returns hochpassgefiltert und geduckt |
| `Poly`, `PolyKernel` (Supersaw nach Szabo, VA, FM, Wavetable mit 464 Tafeln; acht Stimmen zu sieben Unison-Oszillatoren; dynamisches Detune; thermische Drift), `WaveTable`, `WaveTableFile` | Phosphene 76f7100 | Lead, Counter, Pluck, Arp, Stab, Pad (5.5 bis 5.7) | Osc B eine Oktave tiefer mit drei Stimmen, Sub-Saw, Sättigung nach der Unison-Summe, Dip 200 bis 400 Hz; Schaltungsfilter wie in Phosphene 1.2 |
| `Acid`, `DiodeLadder`, `Disperser` | Phosphene | die 303 (5.3) | drei Rollen (Sub, Lead, FX mit Selbstoszillation); vier Automationskurven mit Rücksprung an Phrasengrenzen; Squelch aus |
| `TranceGate` | Phosphene | Gate auf Pad, Streichern, Akkorden (5.7) | freie 16-Step-Masken statt sechs fester Muster |
| `TempoDelay` (Ping-Pong), `PsyFx` (Phaser, Flanger) | Phosphene | Pluck- und Lead-Delays, Delay-Throws | Throw als Ereignis der Form |
| `Sfx`, `FormSfx` (Riser, Downlifter, Impact, Sweep, Reverse Swell, Reverse Crash, Sub Drop) | Phosphene | FX (5.10) | Rausch-Riser als Hochpass-Sweep über 8 bis 16 Takte; Braam, Pauke, Chor-Swell neu |
| `Melody`, `MelodyLead`, `MelodyHarmony`, `MelodyParts`, `Harmony` (Constraint-Markov nach Pachet und Roy, Phrase A A' B A'', Counter, Stab, Voicing mit kleinster Bewegung, Registerwache) | Phosphene | Melodik (6.5, 6.6) | Tonvorrat äolisch/pentatonisch statt phrygisch; Tease-, Voll- und Oktavfassung des Motivs; Kernprogressionen statt Pendel |
| `Form`, `Rhythm` (Sektionen, Pre-Drop-Vakuum, Energiebogen, Instrumentierungsmatrix, Snare-Roll, Fills) | Phosphene | Planer (6.2 bis 6.4) | Trance-Grammatik, Layer-Matrix je 8-Takt-Block, Breakdown-Dramaturgie |
| `Corpus`, `Tools/corpus/*` (Rollen, Tonart, Top-Line, Memorisierung), `Tools/train/*`, `Model`, `ModelKernel` (int8-Transformer in C++) | Phosphene | Korpus Stufe A, später B (6.6) | VORTEX statt Psy-Packs; Tonart über den Bass; Memorisierung gegen die 5452 Transkriptionen |
| `Audibility`, `ComposerLevels`, `Rating`, `Quality`, `Probe` | Phosphene | Hörbarkeit, Qualitätsstufen, Bewertungen | mit Totalitys Leveler abgleichen, nicht doppelt |
| `Vocal` (Formant-Stimme) | Phosphene | "aah"-Flächen, Formant-Schuss im Pre-Drop | nur ohne Text |
| `synth/StringMachine` (Divide-down, Registrierung, Ensemble, Phaser) | Ephemeris d047d79 | Streicherflächen der Neunziger und Zweitausender (5.9) | Sektionsstreicher als neue Registrierung (5.9) |
| `synth/TapeKeys` (Chor aus Glottisquelle und Klatt-Formanten, Streicher, Flöte) | Ephemeris | Chor (5.9) | ohne Bandmaschine (Wow, Bandende) als eigene Stimme |
| `fx/Bbd`, `fx/TapeEcho`, `fx/Rooms` | Ephemeris | Chorus/Ensemble der Pads, Dub-Delay im Progressive | keine |
| `Convolution.h` (partitioniert, bis eine Minute IR) | Noctuary 7a48fdd | lange Hallen 6 bis 12 s (Cinematic, Deep) auf dem Desktop | auf dem Quest FDN |
| `Body.h` (zwölf Moden, gestimmt) | Noctuary | Resonanzboden des Pianos, Pauken-Körper | keine |
| `Effects.h`: `MidSide`, `Unmask`, `EarlyRoom`, `Diffuser` | Noctuary | Mono-Bass, spektrales Ducking der Hallfahnen gegen den Lead, Raumeindruck | `Unmask` mit dem Lead-Bus als Seitenkette |
| `Plugin/`, `Quest/`, `Deploy/`, `Tests/` (TestSupport, vectest, vst3test, hosttest, bench), `Tools/manual`, `Tools/presets`, `UpdateCheck` | Totality | Gerüste | Projektname, Pfade, Tabs |
| `Tools/analyze_ref.py`, `fetch_refs.py`, `eval_report.py` | Totality | Referenzmessung (13.4), Evaluation (13.5) | Trance-Größen (13.4) |
| `Tools/ref_kick.py`, `ref_bass.py`, `ref_width.py`, `ref_band_balance.py`, `ref_arrange.py`, `ref_style.py`, `mix_audit.py`, `metrics.py` | Phosphene | Referenzmessung | Tempobereich 90 bis 145; Offbeat-Bass statt K-B-B-B |

**Neu zu bauen**: der Planer mit Layer-Matrix, Energiekurve und Breakdown-Dramaturgie; das Piano (5.8); Sektionsstreicher,
Brass/Braam und Pauke (5.9); die Ghost-Kick-Automation der Duck-Tiefe; die Prüfung der Form auf dem Audio (13.5); die
Trance-Größen der Referenzmessung (Pump-Tiefe je Band, Breakdown-Drop-Abstand).

**Nicht übernommen**: aus Totality das Pattern-Rack mit Anker/Motion, Slipping, Polymeter und Ghost-Ketten (Trance ist
gerade und fest gerastert), Rumble, Ping, Dub-Chord-Kette, Hypnose-Korridor; aus Phosphene der Psy-Korpus und die
Psy-Gewichte, die phrygische Modalmischung, Zaps und Blips, der Stutter, die Field-Spur (`FieldPlayer`: Field Recordings
wären Samples, 16.1; Deep/Ambient bekommt synthetische Texturen); aus Ephemeris das Sequenzer-Rack; aus Noctuary
`ClusterBrain`, `Cosmos`, `Memory`, die Preset-Bibliothek.

## 5. Die Klangerzeuger

Jeder Erzeuger hat einen skalaren Referenzpfad und, wo es sich lohnt, einen Lane-Pfad (Stimmen in Registern, AVX2 acht,
NEON vier). Velocity steuert bei jeder Stimme auch die Klangfarbe. Unter 150 Hz spielen nur Kick, Sub und Mid-Bass.

### 5.1 Kick (Dok. 5, 7)

Totalitys Kick mit drei Engines (Sweep, Resonator nach Werner, 909) und 909-Topschicht, als drei Schichten nach Dok. 5:
Sub (Sinus-Sweep 150 → 50 Hz in ~30 ms), Body (100 bis 180 Hz, 909-artig), Click (2 bis 5 kHz). Der Grundton wird auf
Grundton oder Quinte der Tonart gestimmt und liegt höchstens einen Halbton neben dem Bassgrundton; kleine Dips bei 55 und
110 Hz (auf die Stimmung skaliert) machen Platz für die Bass-Obertöne. Kompression ~2:1 mit langsamem Attack. Je Profil:
Dream House trocken und kurz, Uplifting hart und gelayert, Acid 909 punchig, Progressive rund, Deep weich oder nur Sub.
Geschlossene Phase und Phasenkopplung an den Sub wie in Phosphene und Totality.

### 5.2 Sub und Mid-Bass (Dok. 3, 5)

Zwei bis drei Linien parallel (Dok. 3): der **Sinus-Sub** (Totalitys `SubBass`, phasenstarr zur Kick, Hochpass 35 Hz,
Tiefpass 100 bis 120 Hz, 10 bis 12 dB Duck), der **Mid-Bass** (Totalitys Mono-Synth: Säge oder Säge plus Rechteck, 24-dB-
Tiefpass, kurze Filterhüllkurve, mono, Sättigung, Hochpass so, dass sein Grundton über dem Sub liegt, 6 bis 10 dB Duck)
und optional eine **Achtellinie eine Oktave höher**. Muster: Offbeat-Achtel (0 ms Attack, ~40 ms Decay, kein Sustain, die
Note klingt vor der nächsten Kick aus), rolling 16tel-Offbeat (Release des Ducks unter 109 ms bei 138 BPM, absichtlich
überschießend), Gallop, sustained/walking mit Synkopen (Progressive, Deep: längere Decays, Reese-Detune, gefilterte
Rechtecke), Sub-Drone (Deep). Die Basslinie folgt den Grundtönen der Progression und signalisiert den Wechsel (2.5).

### 5.3 TB-303 (Dok. 5)

Phosphenes `Acid` (Säge/Rechteck in die Diodenleiter bei doppelter Rate, Accent mit Sweep-Kondensator, Slide ohne
Hüllkurven-Neustart) oder Totalitys Mono-Synth mit Diode; welches, entscheidet ein Hörvergleich in Phase 4. Dazu: die
Sequenz als 16 Steps aus Tonhöhe (eine Oktave plus Oktavschalter), Gate, Accent-Bit und Slide-Bit; Verzerrung (DS-1-artig,
ADAA); bis zu drei geschichtete 303 in Acid (Sub ohne Resonanz, Lead mit Slides, FX mit Selbstoszillation); vier
Automationskurven (Cutoff, Resonanz, Env-Mod, Decay), die über 16 bis 64 Takte steigen und an Phrasengrenzen zurückspringen
(Dok. 5). Die Höhepunkte eines Acid-Tracks sind die Filterpeaks (Dok. 6), deshalb gehört die Cutoff-Kurve der 303 zur
Energiekurve des Planers.

### 5.4 Das Kit (Dok. 3)

Totalitys zwölf Lanes (fünf Engines, 909-Metalltabelle, Lane-Ausgänge, Stimmung auf die Tonart) mit Trance-Rollen:
Closed Hat, Open Hat, Ride, Clap, Snare, Crash, Reverse-Crash, Shaker, Tambourine, Conga, Tom, Rim. Der Snare-Roll ist ein
eigener Generator (Phosphenes `Rhythm`, `FillType::SnareRoll`): Verdichtung Viertel → Achtel → 16tel → 32tel über 4, 8 oder
16 Takte, Tonhöhe steigend (Phosphene: eine Oktave), Lautstärke steigend, endet auf Zählzeit 3 oder 4 des letzten Takts.
Crash auf Takt 1 jeder 8er- oder 16er-Phrase. Kein Swing (2.3).

### 5.5 Supersaw-Lead (Dok. 5)

Phosphenes Supersaw nach Szabo (Detune-Polynom und Mix-Kurven des JP-8000, zufällige Phase je Note, Hochpass mit
Tonhöhenfolge, dynamisches Detune nach Notenlänge, thermische Drift). Neu nach Dok. 5: Osc B mit drei Stimmen eine Oktave
tiefer, optional Sub-Saw −12 ohne Unison; Sättigung nach der Unison-Summe, nicht je Stimme; ein Dip 2 bis 4 dB bei 200 bis
400 Hz; Hochpass, bis der Lead solo dünn klingt. Der Detune-Bereich wird in Cent Spreizung gemessen und so begrenzt, dass
das Fundament im Mix bleibt (Dok. 5: über 0,4 verschwindet es). Im Drop spielt der Lead zwei bis drei Oktaven (Supersaw
plus Oktave), im Haupt-Drop nach 16 Takten eine Oktave höher oder mit Gegenstimme (Counter aus Phosphene).

### 5.6 Pluck, Keys, Arp (Dok. 4, 5)

Phosphenes Poly im VA-Modus, zwei Schichten (Body mittig und leicht verstimmt, Top hell und breit), Amp 0/200–300/0/
150–250 ms, Filterhüllkurve kurz. Ping-Pong-Delay (`TempoDelay`) punktiertes 16tel oder Achtel mit 20 bis 30 % Feedback;
kurzer Stereo-Hall 0,8 bis 1,2 s mit 20 bis 40 ms Pre-Delay. Der Pluck ist in Uplifting und Progressive der eigentliche
Groove-Träger (Dok. 5); im Intro hochpassgefiltert, im ersten Groove offen. Der Arp (16tel up, up-down, random über
Akkordtöne plus Oktave, Gate-Muster; Deep: Achtel oder Triolen) ist in Progressive der Motor.

### 5.7 Pad, Streicherfläche, Trance-Gate (Dok. 5)

Phosphenes Wavetable-Pad (sieben Oszillatoren mit Szabos Detune) oder Supersaw-Pad, Attack 0,5 bis 2 s, Chorus/Ensemble
(Ephemeris `Bbd`), Tiefpass, dessen Öffnung über den Breakdown eine Kurve der Energie ist; im Drop 3 bis 5 dB Duck. Das
Trance-Gate (Phosphene) auf Pad, Streichern oder gehaltenen Akkorden mit freien 16-Step-Masken (x.xx.x.xx.x.x.x. und
Verwandte), Attack und Release als Härte; der Ton-Duck (Tiefpass im geschlossenen Teil) bleibt. Deep/Ambient: das Pad als
Hauptinstrument, langsame LFOs auf Filter und Pan, dazu Noctuarys Granularwolke aus dem eigenen Pad-Bus.

### 5.8 Das Piano (Dream House, Deep; neu): physikalisches Modell nach dem Stand der Technik

Kein Geschwister hat ein Piano. Dok. 5 will ein "leicht dumpfes" Piano im Stil des Kurzweil K2000, kein moderner
Konzertflügel. Parhelion synthetisiert es (Entscheidung 16.1) als **physikalisches Modell, am Vorbild Pianoteq
ausgerichtet** (Wunsch des Nutzers, 29.09.2026) und in jedem Teil auf dem Stand der Literatur. Recherche vom 29.09.2026:

**Was Pianoteq tut** (Patent US 7,915,515 B2, Philippe Guillaume, Modartt/INSA Toulouse). Ein gekoppeltes mechanisches
Modell aus Saiten (Balken mit Biegesteifigkeit, also inharmonisch), Resonanzboden (orthotrope Platte, Finite Elemente)
und ihrer Kopplung am Steg, einschließlich der Verstimmung zwischen den Saiten eines Chors, wird **offline** in seine
komplexen Eigenwerte zerlegt: je Partialton n und Note p eine Frequenz f_np und eine Dämpfung d_np, abhängig von der
Steg-Impedanz Z_np und der Chor-Verstimmung Δ_p. Diese Werte werden über den Parameterraum interpoliert (Polynome, RBF,
Padé); **in Echtzeit** läuft eine Summe gedämpfter Sinusschwingungen s(p,t) = Σ a_n e^(−d_n t) sin(2π f_n t + θ_n) plus
ein Restsignal, die Anregung aus einem nichtlinearen Modell der Hammer-Saite-Wechselwirkung. Die Regler, die Pianoteq
zeigt, sind genau die physikalischen Größen dieses Modells: Hammerhärte, Anschlagpunkt, Chorbreite (Unison Width),
Saitenlänge, Inharmonizität, Resonanzboden-Impedanz, Mitschwingen (Sympathetic Resonance), Dämpferposition, Halbpedal,
Hammer- und Mechanikgeräusche, Zustand (Verstimmung über die Zeit), Mikrofonposition.

**Was die Forschung als Echtzeit-Stand beschreibt.** Bank und Chabassier ("Model-Based Digital Pianos: From Physics to
Sound Synthesis", IEEE Signal Processing Magazine 36(1), 2019) fassen zusammen: vollständige Modelle (Chabassier, Chaigne
und Joly, JASA 2013: nichtlineare Saite mit Längs- und Scherwellen, Hammer mit Hysterese, Resonanzboden als
Reissner-Mindlin-Platte mit Rippen, Abstrahlung in die Luft, energieerhaltende Zeitschemata) sind genau, aber weit von
Echtzeit; Echtzeit-Modelle lassen das Unhörbare weg und rechnen **modal**. Das Referenzsystem ist Bank, Zambon und Fontana
("A Modal-Based Real-Time Piano Synthesizer", IEEE TASLP 18(4), 2010): Saite als Bank von Resonatoren zweiter Ordnung
(impulsinvariant diskretisiert, dadurch keine verzögerungsfreie Schleife mit dem Hammer), physikalischer Hammer
(Masse und nichtlineare Feder F = K Δy^p), Längsschwingungen und Phantompartialtöne aus der Spannungsmodulation (Bank und
Sujbert, JASA 2005), Chorkopplung und Mitschwingen über sekundäre Resonatoren, Resonanzboden als lange Faltung oder als
parallele Filter zweiter Ordnung mit fester, logarithmischer Polverteilung; volle Polyphonie mit 10000 Resonatoren auf einem
Laptop von 2010. Neuere Arbeiten zur nichtlinearen Saite (Ducceschi und Bilbao, JSV 2022, Energie-Quadratisierung; Russo,
Ducceschi und Bilbao, Nonlinear Dynamics 2026) kommen zu dem Schluss, dass die geometrisch exakte Saite mit
Hilfsvariablen-Verfahren (SAV) in Echtzeit wenig bringt, weil sie ein viel feineres Gitter braucht, und empfehlen das
**Kirchhoff-Carrier-Modell** (räumlich gleichförmige Spannung), das explizit und bedingungslos stabil integrierbar ist:
dieselbe Näherung, die Banks "statische" Längsbewegung ist. Differenzierbare Pianomodelle (Renault, Mignot und Roebel,
DAFx 2022; Simionato, Fasciani und Holm, Frontiers in Signal Processing 2024) schätzen Parameter aus Aufnahmen; sie sind
ein Werkzeug der Kalibrierung, kein Klangerzeuger für uns (keine Samples, keine lernenden Klangerzeuger).

**Das Modell von Parhelion** verbindet beides: die Parameter physikalisch berechnet wie bei Pianoteq, die Synthese
physikalisch angeregt wie bei Bank, die Nichtlinearität nach Kirchhoff-Carrier.

1. **Ein virtuelles Instrument als Datenblatt.** Je Note Saitenlänge, Durchmesser (umsponnen im Bass: Kern und Umspinnung),
   Spannung aus f0, Material (E, ρ), Zahl der Saiten im Chor (1 bis 3), Anschlagpunkt (etwa ein Achtel bis ein Neuntel der
   Länge), Hammermasse, Filzparameter; eine Mensur nach den Tabellen der Literatur (Conklin, JASA 1996, "Piano strings and
   scale design"). Daraus B = π³ E d⁴ / (64 T L²) (Fletcher 1964) und die Längsgrundfrequenz. "Dumpf wie ein K2000" ist dann
   ein Instrument: kleiner Flügel, weicher Filz, etwas tieferer Anschlagpunkt, früher Abfall des Resonanzbodens.
2. **Saiten modal**, je Partialton ein Resonator zweiter Ordnung (Bank 2010, Gl. 11), Frequenzen f_k = k f0 √(1 + B k²),
   Verluste R_k = b1 + b3 f_k² (Chaigne und Askenfelt 1994) plus der Anteil, den die Steg-Admittanz abführt. Zahl der
   Partialtöne bis f_s/2 und höchstens etwa 140 im Bass (Bank 2010, Abb. 7). In Lanes zu acht (AVX2) oder vier (NEON).
3. **Chor und Polarisation gekoppelt**, wie bei Pianoteq aus dem gekoppelten System statt aus Tricks: je Partialton die zwei
   oder drei Saiten eines Chors (und ihre zwei Polarisationen) über die komplexe Steg-Admittanz Y(f_k) gekoppelt
   (Weinreich, JASA 1977); die Eigenwerte der kleinen Matrix (höchstens 6 × 6) geben Frequenzen und Dämpfungen der
   gekoppelten Moden, der Hammer regt vor allem die symmetrische an. Schwebung und doppelter Abklingverlauf (Sofortklang,
   Nachklang) entstehen so aus der Physik; "Unison Width" ist die Verstimmung Δ. Berechnet beim Laden und bei Änderung
   eines Reglers auf einem Nebenfaden ("Kalibrierrate", Bank 2010), nie im Audio-Thread.
4. **Hammer mit Hysterese**: Masse und Filz nach Stulov ("Hysteretic model of the grand piano hammer felt", JASA 97(4),
   1995), F(t) = Q0 [u^p(t) − (ε/τ0) ∫ u^p(ξ) e^((ξ−t)/τ0) dξ]; das Integral ist ein einpoliger Filter über u^p, also
   billig. Härte und Exponent je Register (Stulov misst Q0 und p über die Tastatur), die Velocity ist die
   Anfangsgeschwindigkeit des Hammers; die Helligkeit über die Dynamik folgt daraus, nicht aus einem Filter. Kontakt mit
   allen Saiten des Chors, impulsinvariant diskretisiert wie Bank 2010 (Abb. 3), ohne verzögerungsfreie Schleife.
5. **Nichtlinearität nach Kirchhoff-Carrier**: die mittlere Spannung T̄(t) = T0 + (π² E S / 4L²) Σ k² y_k² (Bank 2010,
   Gl. 26) moduliert die Frequenzen (das leichte Absinken der Tonhöhe nach einem Fortissimo-Anschlag) und treibt die
   **Längsmoden** mit der dynamischen Korrektur für die untersten K (Bank 2010, Gl. 27 f.; K 2 bis 10); die Anregung der
   Längsmoden aus Paarprodukten transversaler Amplituden (Gl. 22b), getrennt für gerade und ungerade Längsmoden, nur bis
   f_s/4, gibt die **Phantompartialtöne** und den metallischen Bass. Das ist der Teil, den die Literatur bis 2026 als
   hörbar und echtzeitfähig bestätigt.
6. **Resonanzboden aus seiner Physik**, nicht aus einer Messung (keine Samples): bis etwa 1 kHz verhält er sich wie eine
   homogene orthotrope Platte, darüber sperren die Rippen die Wellen in Wellenleiter zwischen sich ein (Ege, Boutillon und
   Rébillat, JSV 332, 2013; Reduktionsmodelle in Ege et al. 2013, arXiv 1305.3057; Nichtlinearität des Bodens −40 dB,
   vernachlässigt). Parhelion berechnet beim Laden die Moden einer gerippten orthotropen Fichtenplatte in der Form eines
   Flügelbodens (offline wie Pianoteqs Finite Elemente, hier als Rayleigh-Ritz oder kleines FE-Modell) bis etwa 1 kHz und
   darüber die Punktmobilität des Rippen-Wellenleiter-Modells; die Modaldämpfung ist die des Holzes (dort gemessen: nahe am
   Verlustfaktor von Fichte). Das Ergebnis wird als **parallele Filter zweiter Ordnung** realisiert (Bank 2010, Abschnitt
   VII-B; Bank, ICMC 2007): die Pole bis 1 kHz auf den berechneten Moden, darüber logarithmisch, die Gewichte aus
   Mobilität und Abstrahlung, je Mikrofonposition ein Gewichtssatz (Stereo ohne zweites Filter). Die Admittanz am Steg geht
   in Punkt 3 und in die Verluste der Saiten ein: Boden und Saiten sind ein System wie bei Pianoteq.
7. **Mitschwingen und Dämpfer**: die Stegkraft aller klingenden Saiten, nach Registern zusammengefasst (Bank 2010: R = 8
   Regionen, Kopplungsmatrix B), treibt die Saiten, deren Dämpfer oben sind (gehaltene Tasten, Pedal), einseitig und damit
   strukturell stabil. Der Dämpfer ist ein Verlust, der beim Absenken über einige zehn Millisekunden auf die Saite wirkt,
   frequenzabhängig (hohe Partialtöne länger), mit Halbpedal als Zwischenstellung (Lehtonen, Penttinen, Rauhala und
   Välimäki, "Analysis and modeling of piano sustain-pedal effects", JASA 122(3), 2007; Lehtonen et al., Part-Pedaling,
   JASA 126(2), 2009); das kurze Geräusch des Dämpferfilzes beim Aufsetzen.
8. **Mechanik**: das "Klopfen" der Taste auf dem Tastenboden und der Hammerstoß als kurzer Impuls in den Resonanzboden
   (Askenfelt und Jansson 1990, "From touch to string vibrations"), die Auslösung der Taste; leise, aber das, was ein Piano
   von einer Orgel aus Sinustönen unterscheidet.

**Aufwand und Quest.** Wie Bank 2010 rechnet der Audio-Thread nur Resonatoren (drei Multiplikationen je Resonator und
Sample), den Hammer während des Kontakts (wenige Millisekunden) und die Längsanregung; alles Teure (Eigenwerte, Platte)
läuft auf dem Nebenfaden. Eine Stimmenverwaltung nach Kosten statt nach Zahl (Bank 2010), leise Partialtöne werden
abgeschaltet. Auf der Quest weniger Partialtöne, Kopplung nur für die ersten, weniger Bodenpole.

**Messung statt Samples.** Das Modell wird an Aufnahmen **gemessen**, nicht aus ihnen gespielt: Inharmonizität,
Abklingzeiten je Partialton, zweistufiger Abfall, Spektralschwerpunkt über der Velocity, Phantompartialtöne im Bass, aus
Piano-Stellen der Referenzen und frei verfügbaren Einzelton-Aufnahmen (nur Statistiken, wie bei allen Referenzen). Die
Parameterschätzung darf differenzierbar sein (Simionato et al. 2024), der Klangerzeuger bleibt physikalisch.

Dazu, im Arrangement: Achtel- oder punktiertes Delay mit 25 bis 35 % Feedback, Hall 2 bis 4 s, ein Oktav-Layer aus dem Pad
(Dok. 5).

### 5.9 Orchester und Chor (Cinematic; teils neu)

Die orchestrale Farbe von Cinematic ist der schwierigste Teil ohne Samples und ohne neuronales Modell (Dok. 8: "orchestrale
Layer sind prozedural schwer"). Der Vorschlag ist, zuerst den Klang zu treffen, den das Genre historisch hatte: bis etwa
2007 waren "Streicher" in Trance JP-8000-, Virus- und Streichermaschinen-Flächen, und genau die liefern Phosphenes Poly und
Ephemeris' `StringMachine`. Für das "echte" Orchester danach, wie beim Piano physikalisch und auf dem Stand der Literatur
(Wunsch des Nutzers, 29.09.2026; Übersicht in 2.12):

- **Sektionsstreicher**: je Spieler eine gestrichene Saite mit nichtlinearer Reibung am Bogen (Desvages und Bilbao,
  "Two-Polarisation Physical Model of Bowed Strings with Nonlinear Contact and Friction Forces", Applied Sciences 6(5),
  2016; für Echtzeit modal oder als Wellenleiter mit Reibungskennlinie, Smith 1986, Serafin 2004), dahinter ein modaler
  Korpus mit den Moden einer Violine, Viola, eines Cellos oder Kontrabasses aus der Literatur (A0, CBR, B1−, B1+ und
  darüber statistisch); eine Sektion aus mehreren Spielern mit eigenem Bogendruck, Vibrato, Einsatz und Intonation.
  Staccato-Ostinati speist der lokale MIDI-Pack "EMP Cinematic Strings Staccato" statistisch. Der Aufwand (viele Spieler
  mal Akkordtöne) entscheidet, wie viele Spieler eine Sektion hat; eine Hörrunde entscheidet, ob Physik oder die
  Divide-down-Streicher dem Genre besser stehen.
- **Chor**: Ephemeris' `TapeKeys`-Chor (Glottisquelle, Sänger mit eigenem Jitter und Shimmer, Klatt-Formanten "aah"/"ooh")
  ohne Bandmaschine, die Quelle als LF-Modell (Fant, Liljencrants und Lin 1985) statt des Tiefpass-Ersatzes; ein
  artikulatorisches Modell (VocalTractLab, Birkholz) ist der Stand der Technik für Einzelstimmen, für eine Vokalfläche
  ohne Text aber mehr, als hörbar wird.
- **Brass und Braam**: Lippenventil als Oszillator mit einer Masse, gekoppelt an ein Rohr als Wellenleiter mit
  Schallstück und viskothermischen Verlusten (Adachi und Sato 1996; Harrison, Bilbao et al., "An Environment for
  Physical Modeling of Articulated Brass Instruments", Computer Music Journal 2015); die Blechbläser-"Schmetterung" im
  Fortissimo aus der nichtlinearen Wellenausbreitung (Aufsteilen) als einfacher Verzerrer entlang des Rohrs; Braam als
  tiefer Cluster dieser Stimmen mit Verzerrung.
- **Pauke**: modale Kreismembran mit Luftlast des Kessels (die Luft verschiebt die Moden in die fast harmonische Reihe
  1 : 1,5 : 2 : 2,44 ..., Rossing), Schlägel als Kontakt wie der Pianohammer, Tonhöhenfahrt über die Spannung (Papiotis
  und Papaioannou, "Kettle: A Real-time Model for Orchestral Timpani"; Rhaouti, Chaigne und Joly, JASA 1999, für die
  Physik); Impacts aus Phosphenes `Sfx`.

Cinematic-Orchester, Streicher-Kontrapunkt und Chor-Stimmführung schreibt die Melodik (6.6); klingen sie nach der
Hörrunde von Phase 4 zu synthetisch, wird die Synthese nachgebessert (mehr Spieler je Sektion, Korpusresonanzen,
Einsatzgeräusche), denn Samples und neuronale Modelle sind ausgeschlossen (16.1).

### 5.10 FX (Dok. 5, 6)

Phosphenes `Sfx` (Riser, Downlifter, Impact, Sweep, Reverse Swell, Reverse Crash, Sub Drop; alles synthetisch, tonale
Anteile auf die Tonart gestimmt) und `FormSfx` (Platzierung an Punkten der Form). Neu: der White-Noise-Riser als
Hochpass-Sweep über 8 bis 16 Takte (Phosphenes Riser ist ein Bandpass mit gleitenden Sägen), die Delay-Throws auf der
letzten Note vor einem Breakdown (Feedback hoch, Filter zu), der Sub-Drop vor dem Drop, Braam, Pauke und Chor-Swell für
Cinematic, Vinyl-Rauschen, gefärbtes Rauschen (Wind, Wasser) und Granulartexturen aus dem eigenen Pad-Bus für Deep;
Field Recordings nicht (Samples, 16.1).

### 5.11 Räume (Dok. 7)

Zwei Hall-Sends je Deck: Platte (Ephemeris/Totality `Plate`, 1,2 bis 1,8 s, Pre-Delay ~20 ms) und Halle (4 bis 8 s,
Pre-Delay 50 bis 80 ms; Deep 8 bis 12 s): auf dem Desktop wahlweise Noctuarys Faltung mit einer erzeugten Impulsantwort,
auf dem Quest ein FDN. Decay und Return-Pegel sind Kurven der Sektion (Breakdown 2 bis 4 s, Drop 0,8 bis 1,5 s). Ein
Ping-Pong-Delay-Send (punktierte Achtel oder 16tel, Feedback 20 bis 55 %). Alle Send-Eingänge hochpassgefiltert (250 bis
300 Hz), alle Returns geduckt (3 bis 5 dB); Noctuarys `Unmask` optional gegen die "nasse Decke" im Breakdown.

## 6. Der Komponist

### 6.1 Ebenen

1. **Set**: Länge, Dramaturgie (6.8), Profil oder Profil-Reise, Tonartenreise, Tempoverlauf.
2. **Track**: Profil (gemorpht), Tonart, Tempo, Länge; der **Plan** (Sektionen, Layer-Matrix, Energiekurve); Progression
   und harmonischer Rhythmus; das **Motiv** und seine Fassungen; Klänge (ein Preset je Synth und Kit-Lane).
3. **Sektion**: Typ, Länge, Energie am Anfang und Ende, Automationsrampen (Filter, Hall, Sidechain-Tiefe, Register).
4. **8-Takt-Block**: eine Zeile der Layer-Matrix; das eine Element, das hinzukommt oder geht; Fill oder Crash am Ende.
5. **Takt und Step**: Noten der Stimmen im 16-Step-Raster.

### 6.2 Der Planer: Sektionen (Dok. 6, 10)

Eine gewichtete Grammatik je Profil mit einer Markov-Kette erster Ordnung über die Sektionsübergänge (Eigenfeldt und
Pasquier 2013):
```
Track     → Intro Body Outro
Body      → Groove Break Drop Breakdown Drop [Break2 Drop]           (Uplifting, Cinematic: Dok. 6, Referenz)
          | Groove Breakdown Drop [Breakdown2 Drop]                  (Dream House)
          | Groove (KickPause Groove)+ Peak                          (Acid: Höhepunkte als Filterpeaks)
          | Groove (Break Groove)+ Plateau                           (Progressive: mehrere kleine Breaks)
          | Drift (Swell Drift)+                                     (Deep/Ambient: fließend, oft ohne Kick)
Breakdown → BdIntro BdTease BdPeak BdRebuild                         (je 8 oder 16 Takte)
Drop      → DropA DropB                                              (Variation nach 16 Takten)
```
Längen aus {8, 16, 32, 64}; harte Constraints (Dok. 6, 10): Sektionsgrenzen auf Vielfachen von 8, Wechsel auf 16- oder
32-Takt-Linien, Breakdown-Anteil im Fenster des Profils, genau ein Haupt-Drop, Intro und Outro ohne Lead, Gesamtlänge im
Zeitfenster des Profils (2.1, 2.11), Gesamtzahl der Takte ein Vielfaches von 32 (damit der Bass-Swap im Set auf einer
32-Takt-Linie liegt; Phosphenes Regel). Die Mini-Breaks (Kick raus für einen Takt, Dok. 6) und das Pre-Drop-Vakuum
(Phosphenes vier Varianten: ganzer Takt leer, halber Takt, nur Zählzeit 4, Kick allein auf 4; Dok. 3: ein bis zwei
Zählzeiten Stille) sind Ereignisse im Plan.

### 6.3 Die Layer-Matrix (Dok. 6, 10)

Elemente × 8-Takt-Blöcke, jede Zelle **aus**, **gefiltert** (Hochpass im Intro, Tiefpass im Breakdown und Outro) oder
**an**. Elemente: Kick, Sub, Mid-Bass, Oktavbass, 303, Closed Hat, Open Hat, Clap, Ride, Percussion, Crash, Pluck, Arp,
Pad, Streicher, Lead, Counter, Piano, Chor, Orchester, FX. Regeln:
- je Block mindestens eine und höchstens zwei Änderungen (Dok. 6: "alle 8 Takte tritt ein Element hinzu oder
  verschwindet"); die Reihenfolge des Aufbaus im Intro nach Dok. 6 als Vorlage mit Streuung je Profil (Kick, Hats, Bass,
  Open Hat, Percussion, gefilterter Pluck);
- Lead und Motiv erst nach ihrer Einführung im ersten Breakdown (Regel 6); im Outro nur gefiltert und höchstens 16 Takte;
- im Breakdown kein Kick und kein Bass (außer den Varianten von Acid und Progressive), der Rebuild bringt die Kick in
  seinem letzten Teil zurück;
- die Energiekurve steigt monoton durch Build und Rebuild; im letzten Takt vor dem Drop fast leer.

Die Matrix ist Teil der Partitur und der `.parhset`; die GUI zeigt sie als Arrange-Ansicht, jede Zeile und jeder Block
einzeln sperr- und würfelbar.

### 6.4 Die Energiekurve und die Automation (Dok. 6, 7)

Energie 0 bis 10 je 8-Takt-Block, aus der Sektion (Tabelle 2.7) und dem Set-Bogen. Sie treibt auf drei Zeitskalen (Dok. 6):
im Takt Percussion-Dichte und Rolls; in der Phrase Akkordwechsel und Bass-Einsatz; in der Sektion Filter-Cutoff (Pluck-
Hochpass im Intro, Pad-Tiefpass im Breakdown), Hallgröße und Return-Pegel, Sidechain-Tiefe (im Breakdown null, zwei Takte vor
dem Drop hoch), Register des Leads, Cutoff-Kurve der 303. Automationskurven sind parametrische Gesten (Totalitys
`GestureEngine`, Minimum Jerk), verankert an 8-, 16- und 32-Takt-Linien.

### 6.5 Harmonie (Dok. 4)

Tonart gewichtet (Moll ≫ Dur; Grundton D bis A bevorzugt; F-Moll und A-Moll schwer), Progression aus der Tabelle 2.5 mit
Gewichten je Profil und Sektion (i–VII–VI–VII im Intro-Groove, i–VI–III–VII im Breakdown und Drop, VI–VII–i in den letzten
8 Takten vor dem Drop), harmonischer Rhythmus je Profil, Voicing nach 2.5 mit kleinster Stimmbewegung (Phosphenes
Voicing), bVII gehalten über die letzten zwei Takte des Builds, Auflösung auf i auf Zählzeit 1 des Drops; V7 aus harmonisch
Moll im Cinematic-Breakdown; Modulation eine kleine Terz aufwärts nach dem zweiten Breakdown (Cinematic, p [I]); Acid
statisch mit Quart-/Quintwechsel alle 16 Takte. Kick-Stimmung und Bassgrundton folgen der Tonart.

### 6.6 Melodik (Dok. 4, 5)

**Das Motiv** (2 bis 4 Takte) wird einmal je Track gezogen und ist seine Identität. Phrasenbau nach Phosphene: acht Takte
A A' B A'', A' behält den Rhythmus und passt Töne an die Akkorde an, A'' variiert die letzten zwei Takte zur Kadenz hin
(Dok. 4). Tonvorrat Moll-pentatonisch oder äolisch; große Intervalle (Sexte, Oktave) am Phrasenanfang, stufenweise abwärts
zum Phrasenende; starke 16tel auf Akkordtönen. **Fassungen**: Tease (die ersten zwei Takte, gefiltert, im ersten
Breakdown), Voll (die ganze Phrase mit Streichern/Chor im Haupt-Breakdown), Drop 1 (erste Fassung), Haupt-Drop (volle
Oktavverdopplung, nach 16 Takten eine Oktave höher oder mit Gegenstimme), Outro (gefiltert, 16 Takte). Neue Melodien nur im
Breakdown (Regel 6). Dream House: das Motiv gehört dem Piano, legato mit Pedal, weniger Oktaven.

**Die anderen Stimmen**: Pluck als rhythmisierte Akkorde (8/16-Step-Muster, wiederholt, ein bis zwei Überraschungsnoten,
oft nur Grundton und Quinte), Arp (2.5), Counter (Phosphene: antwortet in den Haltetönen des Leads eine Oktave darüber),
303 (5.3), Bass (5.2), Streicher-Kontrapunkt und Chor-Stimmführung in Cinematic (Gegenbewegung zum Lead, Haltetöne auf
Terz und Quinte).

**Regeln zuerst, Statistik zweitens.** Stufe A wie Phosphene: je Rolle ein Modell variabler Ordnung über Stufen, Dauer und
Akzent mit Constraint-Dekodierung (Pachet und Roy 2011), gelernt aus den EMP-Packs (Melodies, Chords, Pads, Pianos,
Basslines, Acid; 2.9) nach Transposition auf A-Moll-Bezug; aus den 5452 Transkriptionen nur Aggregate (Akkordfolgen je
Takt, harmonischer Rhythmus, Phrasenlängen, Intervall- und Konturverteilungen). Stufe B (Phosphenes kleiner Transformer mit
eigener Inferenz) erst, wenn Stufe A in der Hörrunde generisch klingt (Risiko 1). **Memorisierung**: jeder generierte
Takt von Lead und Piano wird gegen alle Takte der Transkriptionen verglichen (Phosphenes `memorisation.py`, erweitert
um transpositionsinvariante Intervall-n-Gramme über zwei Takte); ein Treffer verwirft den Kandidaten. So kommt kein
bekanntes Motiv aus dem Generator, auch nicht zufällig aus den Regeln. Arp, Pluck, Counter und 303 werden gemessen und
berichtet (Stand der Umsetzung, Phase 3): gebrochene Dreiklänge gleichen Transkriptionen zwangsläufig.

### 6.7 Stilprofile

Fünf Profile nach 2.1, jedes ein Vektor aus: Tempo, Grammatikgewichten, Längenfenster, Breakdown-Anteil, Kick-Rezept,
Bassmustern, Hat-Dichte, Lead-Archetyp und Palette (elektronisch, orchestral, organisch), Progressionsgewichten,
harmonischem Rhythmus, Hall- und Delay-Längen je Sektion, Sidechain-Tiefen je Bus, Lautheitsziel, Klangrezepten (welche
Preset-Gruppen je Synth). Morph zwischen zwei Profilen wie in Totality (`morphProfile`); die sechs trennenden Parameter von
2.2 sind zusätzlich direkt als Regler zugänglich. **Standard ist Uplifting/Cinematic** und wird zuerst kalibriert
(Entscheidung 16.1): das Referenz-Arrangement von Dok. 6 beruht darauf, und es braucht alle Bausteine außer Piano und 303.

### 6.8 Das Set (Totality, angepasst)

Totalitys Set-Komponist mit Decks, DJ-Mixer und Isolator, aber mit Trance-Praxis [I, zu belegen]: längere Tracks (6 bis 9
Minuten Extended Mix statt drei Minuten Techno-Tool), Blends über die DJ-Intros und -Outros (32 bis 64 Takte), Bass-Swap auf
einer 32-Takt-Linie, nie ein Breakdown des eingehenden Tracks im Blend, harmonisches Mischen nach Camelot (gleiche Tonart,
Quinte, Parallele; der "Energy Boost" einen Halb- oder Ganzton aufwärts als seltene Geste), Tempo innerhalb eines Sets nah
beieinander. Dramaturgien: Warm-up (Deep → Progressive), Peak (Uplifting, Acid), Closing, **Sunrise** (die Trance-Geste:
der euphorischste Track am Ende) und Journey (die Profile wandern mit der Energie: Deep, Progressive, Dream House,
Uplifting, Cinematic, Acid, gemorpht zwischen Nachbarn). Der dritte Deck borgt wie in Totality (Tease der nächsten Hookline
gefiltert, nur bei verträglichen Tonarten).

### 6.9 Sperren und Neuwürfeln

Wie Totality: jede Einheit auf eigenem Seed-Strom, einzeln neu würfelbar, der Rest bitgleich. Einheiten: `form`,
`matrix`, `energy`, `harmony`, `motif`, `lead`, `bass`, `acid`, `arp`, `pluck`, `piano`, `orchestra`, `drums`, `fx`,
`sounds`, `section<n>`; im Set `track<n>` und `track<n>.<unit>`.

### 6.10 Kandidaten und Prüfung

Je Track mehrere Pläne und je Motiv mehrere Kandidaten, geprüft gegen die Constraints (2.7) und ein Fenster je Profil:
Breakdown-Anteil, Länge, Glätte der Energiekurve, Ähnlichkeit der Motiv-Fassungen (erkennbar, aber nicht identisch,
Editierdistanz nach Phosphene), Ambitus und Registerabstand, Memorisierung (6.6). Später ein Ranker über die Bewertungen
des Nutzers (Phosphenes `Rating`, Totalitys "Favor Ratings").

## 7. Mix und Master (Dok. 7)

### 7.1 Kanalzüge

Je Stimme: Hochpass (alles außer Kick und Sub ab 150 bis 250 Hz, Mid-Bass so, dass sein Grundton über dem Sub liegt),
Pocket-EQ (Kick-Dips 55/110 Hz, Supersaw-Dip 200 bis 400 Hz), Duck vom Ghost-Kick, optional Trance-Gate, Pegel, Pan,
Sends. Mono unter 120 bis 150 Hz im Master (Totalitys "Mono Below").

### 7.2 Stereo

Kick, Sub und Mid-Bass mono; Pluck-Top, Pad, Supersaw breit; die Breite je Profil an den Referenzen gemessen
(Phosphenes `ref_width.py`).

### 7.3 Sidechain

Der Ghost-Kick ist eine Spur der Partitur: Viertel überall, wo das Profil pumpen will, auch in Breakdown und Intro ohne
hörbare Kick. Je Bus eine Duck-Kurve (Totalitys `Ducker`: Tiefe, Attack, Hold, Release mit Raised-Cosine-Form) nach 2.8:
Sub 10 bis 12 dB, Mid-Bass 6 bis 10 dB, Pad 3 bis 5 dB, Lead-Bus 1 bis 2 dB, Returns 3 bis 5 dB; Tiefe als Automation der
Energie (Breakdown null, zwei Takte vor dem Drop hoch). Die Pump-Tiefe je Band wird an den Referenzen gemessen (13.4).

### 7.4 Bus und Master

Drum-Bus mit leichtem Glue, Bass zweistufig, Master: serielle Kompression (~0,5 dB GR, dann sanft), Clipper bei 4×
Oversampling, True-Peak-Limiter auf −1 dBTP, Lautheit je Profil (2.1). Das kommt aus Totality, das als einziges Geschwister
schon echte Club-Lautheit fährt.

### 7.5 Leveler

Totalitys Leveler: die lauteste Stelle jedes Tracks (der Haupt-Drop) auf das Ziel des Profils; davor die Balance jeder
Stimme gegen die Kick in Fenstern je Rolle (Startwerte aus Dok. 7 und den Referenzen, Totality Phase 18). Zusätzlich misst
er die **Kurzzeit-Lautheit des Breakdown-Peaks gegen den Drop** und korrigiert, wenn der Abstand aus 4 bis 8 LU fällt
(Regel 3 von 2.7). Die Lehre aus Totality gilt hier von Anfang an: geprüft wird, was hörbar ist (Spitzen jeder Stimme gegen
die Kick je 8-Takt-Fenster, tonale Stimme in jedem Drop), nicht nur, was in der Partitur steht.

## 8. Ausgaben

Wie Totality: WAV mit Cue-Marken (Intro-Ende, Breakdown, Build, Drop, Outro; im Set jeder Track und Swap) und dieselben
Cues als JSON; MIDI (SMF 1, PPQ 960, Tempo-Karte, Marker, ein Track je Stimme, Drums mit GM-Zuordnung, Automationen als CC);
Stems, deren Summe exakt die Mischung vor dem Master ist; DJ-Loops (Intro- und Outro-Loops, Drop-Loop); Rekordbox-XML mit
Memory-Cues auf Breakdown und Drop; `.parhset`; OSC-Cues `/parh/...` für Kaleidoscope (Breakdown und Drop sind für ein
Visual die dankbarsten Ereignisse).

## 9. GUI und Quest

**Desktop** (JUCE, Layout aus den Parametertabellen, `PARH_SHOT`-Screenshot-Modus, Englisch): Tabs je Synth mit dem
gespielten Preset und dem Live-Ring (Phosphene), Style-Tab mit den sechs Achsen, **Arrange** mit Layer-Matrix und
Energiekurve (Sperren und Würfeln je Zelle, Zeile und Sektion), **Perform** mit "Breakdown now" und "Drop now" (der Planer
schreibt ab der nächsten 8-Takt-Linie um), Filter-Makro, Sidechain-Tiefe, Delay-Throw, Mutes; Set-Seite; Handbuch-Generator
(`Tools/manual`).

**Quest 2** (wie Totality): nativ, Qualitätsstufen (weniger Unison-Stimmen im Supersaw, weniger Piano-Partialtöne, FDN statt
Faltung), Performer-Oberfläche.

## 10. Vektorisierung und CPU

Wie die Geschwister: Stimmen in Registern (Poly acht Stimmen zu sieben Oszillatoren, Kit zwölf Lanes, Piano-Partialtöne in
Lanes), `Vec.h` für AVX2, NEON und skalar bitgleich. Budget [I]: ein Deck unter 25 % eines Desktop-Kerns, im Blend zwei; auf
der Quest ein Set unter 30 % eines Kerns in der niedrigen Stufe. Die teuerste Stimme ist der Supersaw-Akkord (bis zu acht
Stimmen × sieben plus drei Oszillatoren) in einem Blend zweier Drops.

## 11. Plattformen und Build

Wie Totality: CMake, Visual Studio 2026, AVX2, kein Fast-Math, JUCE aus `ThirdParty/JUCE` oder dem Checkout eines
Geschwisters, `PARH_BUILD_PLUGIN=OFF` für Kern, Renderer und Tests allein; Release über `Deploy\build_release.ps1` (Tests,
pluginval Strenge 10, Handbuch, Installer, Zip); Quest über `Quest/`.

## 12. Parameter, Presets und Modulation

**Presets** (Vorgabe des Nutzers, 29.09.2026: "möglichst 1024 pro Modul", "beim Abspielen natürlich auch entsprechend
angezeigt"). Eine Bank je Klangmodul, 16 Gruppen zu 64 Presets auf einem Raster von acht Adjektiven (dunkel bis hell) und
acht Nomen der Gruppe (Ephemeris, Totality, Phosphene), erzeugt aus `Tools/presets/bank_spec.py` (Phosphene) nach
`PresetBank.cpp`: Kick, Sub, Kit-Lanes (je Rolle), Bass, 303, Lead, Counter, Pluck, Arp, Pad, Stab, Piano, Streicher,
Chor, Blech, Pauken, FX, Wolke — 18 Bänke, 18 432 Presets, aus den Rezepturen von 2.6 und durch die Fenster von Dok. 7
geprüft. Eine Gruppe gibt jedem Knopf, den sie kennt, einen Bereich und eine Achse (das Adjektiv: meist die Helligkeit,
das Nomen: meist die Form, oder ein Zufallszug), dazu Filtermodell und Modulation aus ihren Rezepten (Vibrato,
Filterfahrt, PWM, Wavetable-Scan, Tremolo, Schwellung des Bogendrucks, Vokalwandern ...) und ihr Gewicht je Stil. Ein
Preset ist der Klang, nicht die Mischung: Pegel, Panorama, Sends, Duck, Gate, Hochpass und die Knöpfe des Komponisten
bleiben stehen (presetLeaves). Je Preset ein gemessener Pegelausgleich gegen die Standardknöpfe (PresetTrims.inl), damit
ein Wechsel nicht springt; jedes Preset endlich, ohne Denormale, im Pegelfenster, jeder Name in seiner Bank eindeutig.
Beim Piano gehören die Entwurfsknöpfe (Instrument bis Condition) zum Preset; den Entwurf rechnet der Nebenfaden, wenn
der Track lädt.

**Die Wahl des Komponisten**: je Track und Synth eine Gruppe nach den Stilgewichten des (gemorphten) Profils, dann eines
ihrer Presets (Einheit `sounds`, compose.pick_sounds; mit compose.use_ratings wiegen die Bewertungen des Spielers mit),
als `SoundPick` und Knopfsätze am Anfang des Tracks in der Partitur (Programmwechsel, Totality).

**Die Anzeige beim Abspielen**: die Engine meldet, welcher Track die Knöpfe stellt (soundsVersion, leadDeck); das Plugin
zeigt über den Knöpfen jedes Synths sein laufendes Preset (Gruppe und Name) und eine Zeile "Sounds: ..." für den Track,
der spielt; im Set wechselt die Anzeige mit dem Track, der die Knöpfe hat. `parh_render` druckt die Wahl, der Plan-JSON
enthält sie, die MIDI-Datei trägt je Synth und Track einen Programmwechsel (Bank aus der Gruppe, Programm aus dem Index).

**Modulation** (Vorgabe des Nutzers: "genügend Modulationsmöglichkeiten in hinreichender Komplexität"). Jede melodische
Stimme bekommt den Block von Ephemeris und Phosphene (Modulation.h): eine Mod-Hüllkurve (ADSR), vier LFOs (sieben
Formen, frei oder taktsynchron, Neustart je Note, Einblenden) und eine Matrix mit acht Slots. Quellen: LFO 1 bis 4, die
Mod-Hüllkurve, die Filterhüllkurve, Velocity, Tonhöhe, ein Zufallswert je Note, dazu neu das **Modrad** (perform.wheel,
MIDI CC 1) und der **Kanaldruck** als Hand des Spielers und die **Energie** der Partitur (0 bis 10 als 0 bis 1), damit
ein Preset mit dem Track atmet. Ziele je Stimme, was ihr Modell hergibt:
- Poly (sechs Stimmen): wie Phosphene (Tonhöhe, zweiter Oszillator, Pulsbreite, Wavetable-Position, FM-Index, Cutoff,
  Resonanz, Filtermodus, Pegel, Panorama), dazu Detune und die Tiefe des Trance-Gates;
- Bass und 303 (Totalitys Mono-Synth, bisher ohne Block): Tonhöhe, Pulsbreite, Cutoff, Resonanz, Hüllkurvenhub, Drive,
  Pegel, Panorama;
- Piano: Hammerhärte und Anschlagpunkt je Note, Pegel, Panorama, Tonhöhe (Drift als Drehung der modalen Zeiger);
- Streicher: Bogendruck, Bogengeschwindigkeit, Bogenposition, Tiefe und Rate des Vibratos, Tonhöhe, Pegel, Panorama;
- Chor: Vokal, Spannung (Rd), Hauch, Vibrato, Formantverschiebung, Tonhöhe, Pegel, Panorama;
- Blech: Atemdruck, Schmettern, Lippenspannung (Tonhöhe), Vibrato, Pegel, Panorama;
- Pauken: Tonhöhe (das Pedal), Härte, Anschlagpunkt, Abklingen, Pegel, Panorama;
- Kick und Kit behalten ihre Hüllkurven und Musterknöpfe (Dichte, Pan-Wanderung), die FX ihre Familienpresets.
Alles auf dem absoluten Sampletakt und dem Beat (bitgleich über Blockgrößen), die LFOs auf dem Steuerraster der Stimme;
die Kosten je Stimme im Budget (Test).

## 13. Tests und Messungen

### 13.1 Selbsttest (`parh_selftest`, Muster Totality)

Determinismus (Blockgrößen 1, 37, 512; Takt allein gleich Takt in Folge; Vektorpfade bitgleich), die Constraints der Form
auf der Partitur (Sektionsgrenzen mod 8, Wechsel auf 16/32, genau ein Haupt-Drop, Lead nicht vor dem ersten Breakdown, kein
Lead in Intro/Outro, letzter Takt vor dem Drop fast leer, Gesamttakte mod 32), Pockets (Energie unter 150 Hz nur von Kick,
Sub, Mid-Bass), Duck-Kurven (Tiefe und Release gegen den Sollwert), Kick-Stimmung gegen den Bassgrundton, Motiv-Fassungen im
Ähnlichkeitsfenster, Memorisierung, Balance und Hörbarkeit gegen die Kick (Totalitys `testBalance`), Breakdown-Drop-Abstand.

### 13.2 Vektor-, Host- und Build-Tests

vectest (AVX2, NEON-Shim, skalar), hosttest, vst3test (ohne die MIDI-CC-Parameter des Wrappers, Totality e1531e9), pluginval
Strenge 10, Golden-Files je Profil.

### 13.3 Hörprüfung

Hörrunden des Nutzers je Phase (Abschnitt 14), mit Dateien je Profil und Seed; Bewertungen fließen in den Ranker.

### 13.4 Referenzmessung

Referenzen über YouTube wie bei Totality (`Tools/fetch_refs.py`), nur Statistiken im Repo, Audio außerhalb (Liste vorher
bestätigen, Vorschlag 16.2). Die Titel aus Dok. 2 je Profil, von mir ergänzt (markiert mit +) auf etwa fünf je Profil;
die Ergänzungen sind Vorschläge und brauchen die Freigabe:
- Dream House: Robert Miles *Children* (liegt lokal vor), *Fable*; Zhi-Vago *Celebrate the Love*; Dreamland.
- Acid: Hardfloor *Acperience 1*, *Lost in the Silver Box*; + Union Jack *Two Full Moons and a Trout*; + Emmanuel Top
  *Acid Phase*; + Jam & Spoon *Stella*.
- Progressive: Sasha *Xpander*; + BT *Flaming June*; + L.S.G. *Netherworld*; Northern Exposure (Mix-Auszüge); Markus Schulz.
- Cinematic/Uplifting: Andy Blueman *Time to Rest*, *Florescence*; Sasha *Xpander: Refracted* (Orchester); Hybrid
  *Wide Angle*; + Aly & Fila, Arctic Moon, SoundLift (je ein Titel).
- Deep/Ambient: Chicane *Offshore*, *Saltwater*; Delerium *Silence* (Chillout-Mix); Energy 52 *Café del Mar* (Ambient-Mix).

Gemessen mit Totalitys `analyze_ref.py` und Phosphenes `ref_*.py`: Tempo, Sektionsgrenzen (Foote-Neuheit) und ihre Längen
in Takten, Kick-Grundton gegen Tonart, Bass-Onsets relativ zum Kick-Raster (Offbeat, Rolling, Gallop), Hat-Dichte,
Bandbalance, Breite, Lautheit und Crest; neu: **Pump-Tiefe je Band** (Hüllkurvenmodulation bei Viertelrate in Sub, Mid-Bass,
Mitten, Höhen), **Breakdown-Drop-Abstand** (Kurzzeit-LU), Halllänge im Breakdown gegen Drop (Abklingen nach Stopps),
Supersaw-Breite.

### 13.5 Evaluation (Dok. 9, 10)

Die strukturellen Metriken von Dok. 10 auf dem Render statt auf der Partitur: Neuheitskurve gegen die geplanten
Sektionsgrenzen (hört man die Sektionen?), Anteil der Grenzen auf Vielfachen von 8 (der "DJ-Tauglichkeits"-Proxy aus Dok.
10), Drop an der geplanten Stelle, Breakdown-Drop-Abstand 4 bis 8 LU, Tonart auf dem Audio gegen die geplante, Pump-Tiefe
und Bandbalance im Korridor des Profils; ein Evaluationsbericht wie Totalitys `eval_report.py`. FAD/CLAP gegen die Referenzen
höchstens als Nebenzahl (Dok. 9: sie sagen nichts über Drop-Timing oder DJ-Tauglichkeit).

## 14. Phasen und Meilensteine

Die Reihenfolge ist verbindlicher als der Umfang. Nach Phase 1 gibt es den ersten hörbaren Prüfstein, nach Phase 4 ist das
Produkt inhaltlich komplett.

| Phase | Inhalt | Prüfstein |
|---|---|---|
| **0 Gerüst** | Repo, CMake, Modulkopie (Totality-Gerüst, Phosphene-Stimmen), Parametersystem, Clock, Partitur mit Sektionen, Layer-Matrix, Energiekurve und Ghost-Kick, `parh_render`, Selbsttest-Skelett | `parh_render` gibt Stille mit Tempo-Karte und Sektionsmarken aus; Vektortests grün |
| **1 Ein Loop, der pumpt** | Kick (drei Schichten, gestimmt), Sinus-Sub und Offbeat-Mid-Bass, Ghost-Kick-Duck je Bus, CH/OH/Clap, Supersaw-Akkord mit Trance-Gate, Platte und Halle, Master; erste Referenzmessung (nach Freigabe der Liste) | zwei Minuten Uplifting-Groove, der pumpt wie die Referenzen; Pocket- und Duck-Tests grün; Pump-Tiefe und Kick gegen die Referenzen |
| **2 Form und Energie** | Planer (Grammatik, Layer-Matrix, Energiekurve, Breakdown-Dramaturgie, Mini-Breaks, Pre-Drop-Vakuum), Snare-Roll, Riser, Crash, Filter- und Hallkurven, Harmonie mit den Kernprogressionen, Pad und Pluck, Leveler mit Breakdown-Drop-Abstand | ein ganzer Uplifting-Track aus Kick, Bass, Pad, Pluck, Drums und FX; Form-Constraints grün; Breakdown-Drop 4 bis 8 LU gemessen; Neuheitskurve trifft die Sektionen |
| **3 Die Melodie** | Motiv und Fassungen, Lead-Regeln, Counter, Arp, rhythmisierte Akkorde; Korpus Stufe A aus VORTEX mit Memorisierung; Kandidaten | Hörrunde: nachsingbares Motiv, Breakdown, der berührt; Memorisierungstest grün |
| **4a Das Piano** | physikalisches Modell nach 5.8: Mensur, gekoppelte modale Saiten, Stulov-Hammer, Kirchhoff-Carrier und Längsmoden, Resonanzboden aus der Plattenphysik, Mitschwingen, Dämpfer und Halbpedal, Mechanik; Kalibrierrate auf dem Nebenfaden | Inharmonizität, zweistufiger Abfall, Phantompartialtöne und Spektralschwerpunkt über der Velocity gegen Einzelton-Aufnahmen (nur Statistiken); Hörrunde |
| **4b Das Orchester** | Sektionsstreicher (Bogenreibung, Korpus), Chor mit LF-Quelle, Brass (Lippenventil, Rohr), Pauke (luftbelastete Membran) nach 5.9 | Hörrunde gegen die Divide-down- und Supersaw-Flächen; CPU im Budget |
| **4c Die Sub-Genres** | 303 mit Kurven (Acid), Drift und Granular (Deep), Progressive-Groove, Dream House mit dem Piano, Cinematic mit dem Orchester; fünf Profile mit Morph; Kalibrierung gegen die Referenzen | je Profil ein Track im Korridor seiner Referenzen; Hörrunde je Profil |
| **5 Set und Ausgaben** | Decks, DJ-Mixer, Blends, Tonartenreise, Dramaturgien, Profil-Reise; MIDI, Stems, Loops, Cues, Rekordbox, OSC, `.parhset`, Sperren und Würfeln | ein Zwei-Stunden-Set aus einem Seed; Determinismus; Blend-Test; Evaluationsbericht |
| **5b Klangbänke und Modulation** | Modulationsblock für Bass, 303, Piano, Streicher, Chor, Blech und Pauken, Poly erweitert (Modrad, Druck, Energie); 18 Bänke zu 1024 aus `bank_spec.py` mit Pegelausgleich (12); die Wahl je Track und Synth (`sounds`), SoundPick und Programmwechsel in Partitur und MIDI, die Anzeige des laufenden Presets | jede Bank 1024, jedes Preset endlich und im Pegelfenster; Modulation bei Blockgrößen 1, 37 und 512 bitgleich; `parh_render` nennt je Track die Presets; Kalibrierung mit gewählten Presets |
| **6 GUI** | Tabs, Arrange mit Matrix und Energie, Perform mit Breakdown/Drop jetzt, Preset-Menüs (16 Gruppen zu 64, Nutzer-Presets) und das laufende Preset je Synth, Modulationsseiten, Handbuch | Standalone und VST3 bedienbar; pluginval Strenge 10 |
| **7 Quest** | NDK-Build, Qualitätsstufen, Performer-Oberfläche | Set läuft auf der Quest 2, auch im Blend |
| **8 Qualität und Release** | Hörrunden, Nachkalibrierung, Installer, Handbuch, Social Preview | v1.0 |
| **(9) Stufe B** | kleiner Transformer für Lead und Piano (Phosphene), nur wenn Stufe A generisch klingt | Hörrunde, Memorisierung |

## 15. Risiken

1. **Generische Melodien** (größtes Risiko; Dok. 8: "Melodien aus einfachen Regeln klingen schnell generisch"). Trance lebt
   vom Motiv. Gegenmittel: Korpus Stufe A früh (Phase 3), Kandidaten und Ranker, Motiv einzeln würfelbar, Stufe B als Reserve.
2. **Kitsch gegen Kanon**: zu viel Euphorie wird Eurodance (das lokale "Trance Collection" zeigt die Nachbarschaft). Gegenmittel:
   Profile an echten Referenzen kalibriert, Moll und die Voicing-Regeln hart.
3. **Synthetisches Orchester und Piano** klingen billig (Cinematic, Dream House); Samples und neuronale Modelle sind
   ausgeschlossen (16.1). Gegenmittel: der historische Synth-Klang zuerst (JP-8000- und Streichermaschinen-Flächen sind
   für Trance bis etwa 2007 authentisch), das Piano "leicht dumpf" wie Dok. 5 es will, Messung an den Referenzen,
   Hörrunde in Phase 4 und Nachbessern der Modelle.
4. **Club-Lautheit ohne Plattmachen**: −8 LUFS bei 4 bis 8 LU Abstand Breakdown → Drop. Gegenmittel: Totalitys Master,
   Leveler mit gemessenem Abstand, Oversampling an allen Nichtlinearitäten.
5. **Memorisierung bekannter Tracks** über die Fan-Transkriptionen. Gegenmittel: keine Tonhöhenmodelle aus ihnen, Test gegen
   jeden Takt, Kandidat verworfen bei Treffer.
6. **Überlappung mit Phosphene**: Supersaw und 303 teilen sich beide; ohne die klare Grenze aus 2.2 werden die Profile Acid
   und Uplifting zu langsamem Psytrance. Gegenmittel: Offbeat-Bass, natürliches Moll, kein Psy-FX als harte Regel.
7. **Referenzen aus YouTube**: verlustbehaftet, teils Radio-Edits (die Form ist dann gekürzt). Gegenmittel: Extended Mixes
   suchen, nur Statistiken, Liste vorher bestätigen.
8. **Name**: "Parhelion" vor dem Release gründlich prüfen (Umbra war vergeben).
9. **Quest-Budget** mit Supersaw-Akkorden auf zwei Decks. Gegenmittel: Qualitätsstufen ab Phase 1.

## 16. Entscheidungen des Nutzers

### 16.1 Getroffen (29.09.2026)

1. **Eigenes Projekt** in `G:\Tools\VRAudio\TranceGenerator`, nicht in Noctuary integriert (stehende Regel).
2. **Name: Parhelion**, Präfix `parh` (Kandidaten waren Parhelion, Heliacal, Anthelion; vor dem Release gründlich prüfen,
   Risiko 8).
3. **Piano und Orchester synthetisch**: modales Piano, Sektionsstreicher, Chor, Brass, Pauke; keine Samples, auch keine
   Field Recordings (5.8 bis 5.10).
4. **Keine neuronale Audio-Ebene**, auch nicht als spätere Offline-Stufe (Dok. 8 und 10 empfehlen sie; 1, Nicht-Ziele).
5. **Standardprofil Uplifting/Cinematic**, zuerst kalibriert (6.7).
6. **Keine Agenten** bei der Arbeit an diesem Projekt (stehende Regel).
7. **Jeder Baustein nach dem Stand der Technik** (2.12), das Piano physikalisch am Vorbild Pianoteq (5.8); Streicher, Brass
   und Pauke ebenfalls physikalisch (5.9).

### 16.2 Vorschläge, bestätigt am 29.09.2026 ("Ansonsten fange gerne an mit der Implementierung")

1. **Korpus**: die EMP-Packs (1358 Loops) für die Tonhöhen- und Rhythmusmodelle, die 5452 Fan-Transkriptionen nur für
   Aggregate und die Memorisierungsprüfung (2.9, 6.6).
2. **Referenzliste** (13.4) über YouTube wie bei Totality, nur Statistiken im Repo, Audio außerhalb; das Laden frage ich mit
   der konkreten Liste an, bevor es losgeht.
3. **Quest**: ja, der komplette Generator auf dem Gerät, wie bei allen Geschwistern.
4. **Bausteine** aller vier Geschwister, Gerüst aus Totality (Abschnitt 4).
5. **GUI und Handbuch Englisch**, Plan und Journal Deutsch.
6. **Im Host gilt das Host-Tempo**; Tempoverläufe eines Sets kommen über den MIDI-Export (wie Totality).
7. Die Phosphene-Regel **"keine kleine None in der Fläche"** gilt auch hier (2.5).

## 17. Literatur (Auswahl, je Baustein)

Die vollständige Quellenliste steht in Dok. (Quellen); hier nur, was Parhelion über das Dokument hinaus oder an zentraler
Stelle benutzt.

- Szabo, A. (2010). How to Emulate the Super Saw. Abschlussarbeit, Stockholm. (Supersaw, über Phosphene)
- Eigenfeldt, A.; Pasquier, P. (2013). Evolving Structures for Electronic Dance Music. GECCO. (Planer)
- Pachet, F.; Roy, P. (2011). Markov Constraints: Steerable Generation of Markov Sequences. Constraints 16(2). (Melodik)
- Begleiter, R.; El-Yaniv, R.; Yona, G. (2004). On Prediction Using Variable Order Markov Models. JAIR 22. (Korpus Stufe A)
- Knees, P. et al. (2015). Two Data Sets for Tempo Estimation and Key Detection in Electronic Dance Music Annotated from User
  Corrections. ISMIR. (Tonartverteilung)
- Grosz, P. et al. (2025). An Outline of the Narrative Grammar of Electronic Dance Music. Musicae Scientiae. (Sektionen,
  über Phosphene)
- Butler, M. J. (2006). Unlocking the Groove. Indiana UP. (Hypermetrum)
- Farbood, M. (2012). A Parametric, Temporal Model of Musical Tension. Music Perception 29(4). (Energiekurve)
- Foote, J. (2000). Automatic Audio Segmentation Using a Measure of Novelty. ICME. (Evaluation)
- Yadati, K. et al. (2014). Detecting Drops in Electronic Dance Music. ISMIR. (Evaluation, falls nötig)
- Fletcher, H. (1964). Normal Vibration Frequencies of a Stiff Piano String. JASA 36(1). (Piano)
- Weinreich, G. (1977). Coupled Piano Strings. JASA 62(6). (Piano)
- Bank, B.; Zambon, S.; Fontana, F. (2010). A Modal-Based Real-Time Piano Synthesizer. IEEE TASLP 18(4). (Piano)
- Smith, J. O.; Van Duyne, S. A. (1995). Commuted Piano Synthesis. ICMC. (Piano)
- Klatt, D. H. (1980). Software for a Cascade/Parallel Formant Synthesizer. JASA 67(3). (Chor, über Ephemeris)
- Werner, K. J.; Abel, J. S.; Smith, J. O. (2014). A Physically-Informed, Circuit-Bendable, Digital Model of the Roland
  TR-808 Bass Drum Circuit. DAFx. (Kick, über Totality)
- Dattorro, J. (1997). Effect Design, Part 1. JAES 45(9). (Platte, Halle)
- Giannoulis, D.; Massberg, M.; Reiss, J. D. (2012). Digital Dynamic Range Compressor Design. JAES. (Bus, Master)
- Flash, T.; Hogan, N. (1985). The Coordination of Arm Movements. J. Neuroscience 5(7). (Automationskurven)
- ITU-R BS.1770-4, EBU R 128 (Lautheit).
