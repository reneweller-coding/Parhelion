/**
 * @file PluginProcessor.h
 * @brief The plugin (PLAN 10.1, 12; Phase 5): the engine, the composer on a thread of its own, transport, the performer.
 *
 * **Parameters.** Every entry of the engine's ParamStore is a host parameter (StoreParameter, after Phosphene's and
 * Ephemeris'): the store is the only place a value lives, read and written as a relaxed atomic, so a knob in the plugin
 * and the same knob in parh_render stand at the same place.
 *
 * **Composing.** "Compose" snapshots the parameters, the seed and the rerolls and hands them to the composer thread: a
 * track (composeTrack) or, with set.minutes above zero, a set (composeSet). The finished score waits until the message
 * thread loads it into the engine with processing suspended -- the engine allocates when it loads, and the audio thread
 * never does. Its loudness is measured afterwards on the same thread while it already plays (Leveler.h), and the
 * corrections glide in.
 *
 * **Time.** In a host the playhead is the clock: the engine follows its position in beats and jumps when the host does,
 * at the host's tempo -- the score is loaded with the host's tempo as a constant (forPlayback, PLAN 16.2: "im Host gilt
 * das Host-Tempo"), and a new host tempo loads it again on the message thread from the beat it was at. A set's own tempo
 * drift is the standalone's and the export's. The standalone has its own play and stop.
 *
 * **Performing** (PLAN 10.1, Perform). The engine plays live (Engine::setLive): the perform module's mutes, master
 * filter and echo throw act, and the mixer is in a track's path, so the isolator kills work on a single track too. MIDI
 * reaches them: the keys from middle C up (C to F#) toggle the mutes of kick, bass, hats, perc, lead, synths and pads; the
 * mod wheel and the channel pressure are sources of every voice's modulation matrix (perform.wheel, perform.pressure,
 * Modulation.h), controller 74 moves the master filter, the expression pedal the throw; any controller can be learned for any parameter
 * (learn()). The bindings are part of the state.
 *
 * **Cues** (PLAN 10.3, Cue.h): with cue.enabled the beats, bars, blocks, operations and keys go out as OSC over UDP to
 * `PARH_CUE_HOST` (default this machine) at cue.port, each at the moment it is heard.
 *
 * **Undo** (01.10.2026, the frame): a knob turned on the panel, a preset, a new seed, a reroll, a loaded set or a choice of
 * track or mix is a step (frame::UndoHistory); a step holds only what it changed, so taking it back never takes back the
 * sounds the engine wrote on the knobs since. Knobs moved from MIDI or by a host's automation are not steps.
 *
 * **The headset** (01.10.2026, the frame): the hands of the Quest app in bridge mode arrive as OSC (frame::Headset) while
 * the settings do not say Off: left pinch play and stop, both hands the next track, right pinch the kick out and in, the
 * left hand's height the master filter, the right hand's the echo throw.
 *
 * **Mute** (after Phosphene). The output can be muted: silence at the very end of processBlock, after the meters and the
 * test recording have read the block. `PARH_MUTE=1` -- and the screenshot mode `PARH_SHOT` -- start the plugin muted, and
 * then it never unmutes itself: an automated run makes no sound.
 *
 * @note After Ephemeris' Plugin/PluginProcessor.h at d047d79 (27.09.2026).
 * @note Copied from Totality `Plugin/PluginProcessor.h` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#pragma once
#include "parh/Engine.h"
#include "parh/SetFile.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Set.h"
#include "Frame.h"
#include "LinkClock.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

/** @brief One entry of the ParamStore as a host parameter. */
class StoreParameter final : public juce::RangedAudioParameter {
public:
    /** @brief Binds parameter @p id of @p store (which outlives this object) under the display name @p name. */
    StoreParameter(parh::ParamStore& store, int id, const juce::String& name);
    float getValue() const override;                  ///< the store's value, normalised
    void setValue(float newValue) override;           ///< writes the store (relaxed atomic)
    float getDefaultValue() const override;           ///< the descriptor's default, normalised
    juce::String getName(int maximumStringLength) const override;   ///< the display name (0: no limit)
    juce::String getLabel() const override;           ///< the unit
    int getNumSteps() const override;                 ///< steps of a discrete parameter
    bool isDiscrete() const override;                 ///< Int, Choice and Toggle are discrete
    bool isBoolean() const override;                  ///< a Toggle is boolean
    juce::String getText(float normalisedValue, int maximumStringLength) const override;   ///< the value as the panel shows it
    float getValueForText(const juce::String& text) const override;   ///< parses a choice name, On/Off or a number
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range_; }   ///< the store's own mapping
    int paramId() const { return id_; }               ///< the id in the store

private:
    parh::ParamStore& store_;   ///< the store it reads and writes
    int id_;   ///< the id in the store
    juce::String name_;   ///< the display name
    juce::NormalisableRange<float> range_;   ///< the store's mapping between the real and the normalised value
};

/** @brief A track where it lies in what plays (a track alone: at beat 0 on deck A). */
struct TrackPlace {
    double start = 0.0;   ///< set beat of the track's first bar
    double swapIn = 0.0;   ///< set beat where it takes over the low end
    double end = 0.0;   ///< set beat of its end
    int deck = 0;   ///< the deck it plays on
    parh::TrackInfo info;   ///< what the composer says of it
};

/** @brief What plays: a set, or a track as deck A of a one-track set, with where its tracks lie. */
struct Playing {
    parh::SetScore set;   ///< the set, or the one track as deck A
    bool isSet = false;   ///< a set (a DJ mix), not a single track
    std::vector<TrackPlace> tracks;   ///< its tracks where they lie
    parh::SetInfo setInfo;   ///< a set's (empty for a track)
    bool resume = false;     ///< the performer's "now": loaded where the engine is, and it plays on (performNow)
    std::vector<parh::Rewrite> rewrites;   ///< the "now"s this track was rewritten by, in order (a next one keeps them)
};

/** @brief The Parhelion processor. */
class ParhelionProcessor final : public juce::AudioProcessor, private juce::Thread, private juce::Timer,
                                private juce::AudioProcessorParameter::Listener {
public:
    ParhelionProcessor();                                ///< registers every parameter and composes a first track
    ~ParhelionProcessor() override;                      ///< stops the composer and the exporter

    // Composing and curating.
    void compose();                                  ///< compose with the current settings, seed and rerolls
    void newSeed();                                  ///< a fresh seed, no rerolls, then compose
    /** @brief Draws @p unit again ("blocks", or in a set "track3.blocks", "track3", "set"), then composes. */
    void reroll(const juce::String& unit);
    /**
     * @brief "Breakdown now" or "Drop now" (PLAN 9, Perform): the track that plays, rewritten from its next 8-bar line
     *        (the one after it where that is under six seconds away, so the composer is done in time) into a breakdown
     *        (its build, a drop) or a drop at once (planTrackRewritten); loaded where it plays, it plays on, its level
     *        corrections kept. A set is not rewritten (its next blend would have to move with it).
     * @return the bar the rewrite begins at, or -1 where there is nothing to rewrite
     */
    int performNow(parh::SectionKind kind);
    /** @brief The bar of the last performNow while it waits to be heard, -1 none. */
    int pendingNow() const { return nowBar_.load(); }
    bool isComposing() const { return composing_.load(); }   ///< whether the composer thread is at work
    /** @brief Whether the knobs ask for a DJ mix (set.minutes above 0) rather than a single track. */
    bool mixChosen() const;
    /**
     * @brief A single track or a DJ mix (message thread, 01.10.2026 -- after Totality's Phase 21): set.minutes to 0, or
     *        back to the mix's last length (60 min at first); then composes, if what plays is not that already.
     */
    void chooseMix(bool mix);
    bool playingMix() const { std::lock_guard<std::mutex> g(lock_); return current_.isSet; }   ///< whether what plays is a DJ mix
    uint64_t seed() const { std::lock_guard<std::mutex> g(lock_); return seed_; }   ///< the seed of what plays
    juce::String curationText() const;               ///< the rerolls, for the panel
    /** @brief The track under @p beat (its index in what plays), -1 if none. In a set, the one that owns the low end. */
    int trackAt(double beat) const;
    /**
     * @brief Phase 17: rates the track under the playhead (+1 liked, -1 not) into Documents/Parhelion/ratings.tsv; the
     *        ratings weigh the archetypes and preset groups while compose.use_ratings is on (Preferences.h).
     * @return what was rated, for the panel (empty when nothing plays)
     */
    juce::String rate(int value);

    // Transport (the standalone's; a host drives its own).
    void setPlaying(bool on) { playing_ = on; }      ///< play or stop (the standalone's transport)
    bool isPlaying() const { return playing_.load(); }   ///< whether the standalone plays
    /** @brief The standalone's Ableton Link, as the settings menu says it: off, alone, or how many apps are with it. */
    juce::String linkStatus() const;
    /** @brief Jumps to @p beat at the next block (the display goes there at once, also while nothing plays). */
    void seekTo(double beat) { seekRequest_ = beat; position_ = beat; }
    double positionBeats() const { return position_.load(); }   ///< where the audio thread is, in beats
    /** @brief Counts the scores the engine has loaded: a display compares it to know when to copy again. */
    int scoreVersion() const { return scoreVersion_.load(); }
    /** @brief A copy of what plays (message thread; for the arrange and eclipse views). */
    void copyPlaying(Playing& out) const { std::lock_guard<std::mutex> g(lock_); out = current_; }
    /**
     * @brief The factory preset the composer chose for instance @p instance of synth @p m in the track whose sounds the
     *        knobs show (Engine::leadDeck): its index in parh::factoryPresets(m), -1 if none.
     */
    int composedPreset(parh::Module m, int instance) const;
    /** @brief Puts factory preset @p index of @p m on instance @p instance's knobs, through the host's parameters. */
    void applyPreset(parh::Module m, int instance, int index);
    /**
     * @name User presets (after Phosphene's)
     * A synth's own sounds, `<name>.txt` in Documents/Parhelion/Presets/<synth> (the kit's twelve lanes share "perc"):
     * every knob a factory preset sets, one `knob=value` a line with the key inside the module ("cutoff=2400"), so a
     * lane's sound loads into any lane (parh::userPresetText). PARH_USER_DIR moves the folder.
     * @{ */
    /** @brief The folder of one synth's user presets. */
    juce::File userPresetFolder(parh::Module m, int instance) const;
    /** @brief Its user presets, sorted by name. */
    juce::Array<juce::File> userPresets(parh::Module m, int instance) const;
    /** @brief Saves the synth's knobs as the user preset @p name; the file, or File() when it failed. */
    juce::File saveUserPreset(parh::Module m, int instance, const juce::String& name);
    /** @brief Puts user preset @p file on the synth's knobs, the rest of the sound at its defaults (as a factory preset). */
    bool applyUserPreset(parh::Module m, int instance, const juce::File& file);
    /** @} */
    /** @brief The length of what plays, in beats and seconds (as composed). */
    void length(double& beats, double& seconds) const;

    // Files.
    bool saveSet(const juce::File& file);            ///< writes seed, lengths, rerolls and parameters as an .parhset
    bool loadSet(const juce::File& file);            ///< reads an .parhset and composes it
    /** @brief What an export writes beside the WAV and its MIDI and cues. */
    enum ExportExtra { kStems = 1, kLoops = 2 };
    /**
     * @brief Renders what plays offline (as composed: not live) to a 24-bit WAV with its cues (a cue chunk and JSON) and
     *        MIDI beside it, on a thread; returns at once. @p extras: kStems a folder "<name>_stems" (their sum is the mix
     *        before the master), kLoops a folder "<name>_loops" with the DJ loops (a track only).
     */
    void exportTo(const juce::File& wav, int extras);
    juce::String status() const;                     ///< one line for the panel
    static juce::File ratingsFile();                 ///< Phase 17: Documents/Parhelion/ratings.tsv
    /**
     * @brief The test mode (PARH_SEED, PARH_PLAY = seconds, PARH_RECORD = a WAV file; PARH_SET = minutes, a set): a fixed
     *        seed, play at once, record what the audio thread renders; recordingDone() when the seconds are full.
     */
    bool recordingDone() const { return recordTarget_ > 0 && recordPos_.load() >= recordTarget_; }
    void writeRecording();                           ///< writes the recording (message thread)
    /**
     * @brief The meters since the last call (message thread): per deck the peak and the RMS after its mixer channel, the
     *        output's peak, and its loudness over the last 400 ms (K-weighted, BS.1770), -70 in silence.
     */
    void takeMeters(float* deckPeak, float* deckRms, float& outPeak, float& momentaryLufs);
    /**
     * @brief The mixer's strips since the last call (message thread, the Mixer page): per parh::Deck::Stem the loudest
     *        sample and the RMS, both decks together.
     */
    void takeStripMeters(float* peak, float* rms);

    // Muting.
    bool muted() const { return mute_.load(std::memory_order_relaxed); }   ///< the output is silenced
    /** @brief Mutes or unmutes; does nothing while `PARH_MUTE` forces it. */
    void setMuted(bool on) { if (!forceMute_) mute_.store(on, std::memory_order_relaxed); }
    bool muteForced() const { return forceMute_; }   ///< `PARH_MUTE` (or `PARH_SHOT`) was set: the switch is stuck on

    // Undo (the frame).
    bool undo();                                     ///< takes the last step back; false if there was none
    bool redo();                                     ///< makes the last undone step again
    juce::String undoName() const { return history_.undoName(); }   ///< what undo takes back (empty: nothing)
    juce::String redoName() const { return history_.redoName(); }   ///< what redo makes again
    /** @brief Opens a step of several knobs (a preset, a reset): they are undone together. Close with endStep. */
    void beginStep(const juce::String& what);
    void endStep();                                  ///< closes beginStep's step
    /** @brief Store id @p id back to its default (one step). */
    void resetToDefault(int id);

    // The panel's live rings.
    /** @brief The value store id @p id plays at the moment, normalised -- NaN where it is the knob's own. */
    float playedNormalised(int id) const;

    // The headset (the frame).
    frame::Headset& headset() { return headset_; }   ///< the hands arriving from the Quest app

    // Performing.
    /** @brief Binds the next MIDI controller that arrives to store id @p id; -1 cancels. */
    void learn(int id) { learn_ = id; }
    int learning() const { return learn_.load(); }   ///< the store id waiting for a controller, or -1
    /** @brief The controller bound to store id @p id, or -1. */
    int controllerFor(int id) const;
    /** @brief Unbinds store id @p id. */
    void forget(int id);

    parh::ParamStore& store() { return engine_.params(); }   ///< the engine's parameters
    /** @brief The parameters (read only). */
    const parh::ParamStore& store() const { return engine_.params(); }
    /** @brief The host parameter of store id @p id, or null. */
    StoreParameter* parameter(int id) { return id >= 0 && id < static_cast<int>(params_.size()) ? params_[static_cast<size_t>(id)] : nullptr; }
    /** @brief Sets store id @p id to the real value @p value through its host parameter (a gesture). */
    void setFromUi(int id, float value) { setFromMidi(id, value); }

    // juce::AudioProcessor: a stereo instrument, one program, the state as XML.
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;   ///< prepares the engine and reloads the score
    void releaseResources() override {}               ///< nothing to release
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;   ///< stereo out only
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;   ///< plays, following the host's playhead
    juce::AudioProcessorEditor* createEditor() override;   ///< the panel
    bool hasEditor() const override { return true; }  ///< it has one
    const juce::String getName() const override { return JucePlugin_Name; }   ///< "Parhelion"
    bool acceptsMidi() const override { return true; }   ///< MIDI in: the performer's keys and controllers
    bool producesMidi() const override { return true; }   ///< MIDI out: the composer's notes as they play (02.10.2026)
    double getTailLengthSeconds() const override { return 8.0; }   ///< the rooms ring on
    int getNumPrograms() override { return 1; }       ///< one program
    int getCurrentProgram() override { return 0; }    ///< always the one
    void setCurrentProgram(int) override {}           ///< nothing to switch
    const juce::String getProgramName(int) override { return "Set"; }   ///< "Set"
    void changeProgramName(int, const juce::String&) override {}   ///< not renameable
    void getStateInformation(juce::MemoryBlock& destData) override;   ///< seed, rerolls, parameters, controllers as XML
    void setStateInformation(const void* data, int sizeInBytes) override;   ///< restores them and composes

private:
    /** @brief Nothing: a value alone is no step (the gestures are). */
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;   ///< a knob on the panel: a step
    std::vector<float> values() const;               ///< every store value (undo)
    juce::String extraState() const;                 ///< seed, rerolls and the mix's length as text (undo)
    void applyExtra(const juce::String& text);       ///< the inverse; composes if seed or rerolls changed
    /** @brief Puts undo step @p s back (@p after false) or makes it again (@p after true). */
    void applyStep(const frame::UndoStep& s, bool after);
    void pollHeadset();                              ///< the hands' events, 30 times a second (message thread)
    void run() override;   ///< the composer thread
    void timerCallback() override;   ///< loads a finished score on the message thread
    /** @brief Composes with the knobs as they are (copied into @p snapshot). */
    Playing composeNow(parh::ParamStore& snapshot);
    /** @brief Hands the loudness corrections, measured while it plays, to the engine (message thread). */
    void takeTrims();
    /** @brief @p p's set as the engine plays it here: in a host at the host's tempo (constant), in the standalone as composed. */
    parh::SetScore forPlayback(const Playing& p) const;
    /** @brief Loads @p p into the engine (message thread, processing suspended by the caller). */
    void loadEngine(const Playing& p);
    /** @brief The performer's MIDI: keys toggle the mutes, controllers move what they are bound to (audio thread). */
    void perform(const juce::MidiBuffer& midi);
    int keyboardSeen_ = 0;   ///< the keyboard target of the last block (audio thread): a change releases every key
    // MIDI out (02.10.2026): the composer's notes as the decks play them, on the channels of the MIDI export (Midi.h).
    parh::NoteTap noteTap_;   ///< what the decks played in the last process() call (audio thread)
    frame::KeyMemory keyMemory_;   ///< where each held key went (the split, Scale Lock), so its release follows (02.10.2026)
    /** @brief A note-off due later, for a note whose part has none of its own (NoteTap::Note::offSample). */
    struct PendingOff {
        int64_t sample;    ///< when, on the engine's sample counter
        uint8_t channel;   ///< MIDI channel, 1..16
        uint8_t pitch;     ///< MIDI note
    };
    std::array<PendingOff, 256> pendingOff_{};   ///< the note-offs not yet sent (audio thread)
    int pendingOffs_ = 0;                        ///< how many of pendingOff_ are due
    int64_t midiExpect_ = -1;                    ///< the sample the next block should start at; another is a jump
    bool midiSounding_ = false;                  ///< a note-on went out since the last all-notes-off
    /** @brief Writes the notes the decks played in the block from @p start (@p n samples) into @p midi, and the due offs. */
    void emitMidi(juce::MidiBuffer& midi, int64_t start, int n);
    /** @brief All notes off on every channel, at the block's start, when a note sounded (a stop, a jump). */
    void silenceMidi(juce::MidiBuffer& midi);
    /** @brief Sets store id @p id to the real value @p value through its host parameter. */
    void setFromMidi(int id, float value);

    parh::Engine engine_;   ///< the engine
    std::vector<StoreParameter*> params_;   ///< the host parameters, one per store id (owned by the processor)
    uint64_t seed_ = 1;   ///< the seed of what plays
    parh::Curation curation_;   ///< the rerolls and locks
    mutable std::mutex lock_;   ///< guards what the composer thread hands over
    std::vector<parh::Rating> ratings_;   ///< Phase 17: the player's ratings (lock_)
    std::unique_ptr<Playing> pending_;               ///< composed, waiting to be loaded
    Playing current_;                                ///< what the engine plays
    std::atomic<bool> composing_{ false };   ///< the composer thread works
    std::atomic<bool> playing_{ false };   ///< play is on
    std::atomic<bool> exporting_{ false };   ///< an export runs
    std::atomic<bool> again_{ false };               ///< compose was asked for while composing: once more when done
    std::atomic<int> nowBar_{ -1 };                  ///< performNow's bar, -1 none (read by composeNow)
    parh::SectionKind nowKind_ = parh::SectionKind::Breakdown;   ///< and its kind (lock_)
    // The loudness (Leveler.h): measured on the composer thread once the score is handed over, while it plays.
    std::atomic<bool> newer_{ false };               ///< a newer score is asked for: the measuring of the last one stops
    uint64_t composed_ = 0;   ///< counts the compositions
    uint64_t pendingId_ = 0;   ///< pending_'s number (lock_)
    uint64_t playingId_ = 0;   ///< current_'s number (lock_)
    std::vector<float> trims_[parh::kDecks];          ///< the corrections found for the composition trimsFor_ (lock_)
    std::vector<parh::BalanceDb> bal_[parh::kDecks];   ///< Phase 18: the parts' corrections found with them (lock_)
    uint64_t trimsFor_ = 0;   ///< the composition trims_ and bal_ belong to (lock_)
    bool levelled_ = false;                          ///< current_ carries its corrections (lock_)
    std::atomic<double> position_{ 0.0 };   ///< where the engine is, beats (audio thread writes)
    std::atomic<double> seekRequest_{ -1.0 };   ///< a jump asked for, beats; -1 none
    double sampleRate_ = 48000.0;   ///< the sample rate, Hz
    int blockSize_ = 512;   ///< the largest block, samples
    juce::String lastExport_;   ///< the last export's result, for the Export tab
    float mixMinutes_ = 60.0f;                       ///< the mix's length while a single track is chosen (chooseMix; the state)
    std::unique_ptr<std::thread> exporter_;   ///< the export thread while it runs
    std::vector<float> record_;                      ///< interleaved, allocated in prepareToPlay in the test mode only
    size_t recordTarget_ = 0;   ///< TOT_RECORD: how many values record_ takes, 0 off
    std::atomic<size_t> recordPos_{ 0 };   ///< how many are written
    bool autoPlay_ = false;   ///< TOT_PLAY: play once the first score is loaded
    // The meters: the decks through their taps, the output's peak and a K-weighted mean square.
    std::vector<float> tapBuf_;                      ///< kDecks x 2 x block (prepareToPlay)
    std::array<float*, parh::kDecks> tapL_{};   ///< per deck: its tap's left channel in tapBuf_
    std::array<float*, parh::kDecks> tapR_{};   ///< per deck: its tap's right channel in tapBuf_
    // The stems as outputs of their own (02.10.2026): a stereo bus per stem (Engine::stemName) besides the main output,
    // off until the host switches one on; then the engine writes every stem (Engine::setStems) and each bus that is on
    // gets its own. Pre-master, as the export's stems: they sum to the mix before the master.
    std::vector<float> stemBuf_;                                ///< kStems x 2 x block (prepareToPlay)
    std::array<float*, parh::Engine::kStems> stemL_{};       ///< per stem: its left channel in stemBuf_
    std::array<float*, parh::Engine::kStems> stemR_{};       ///< per stem: its right channel in stemBuf_
    bool stemsOn_ = false;                                      ///< audio thread: the engine writes the stems now
    /** @brief The buses: the main stereo output, then one stereo output per stem, those off by default. */
    static BusesProperties busLayout();
    std::array<std::atomic<float>, parh::kDecks> meterPeak_{};   ///< audio thread raises, the editor takes (exchange 0)
    std::array<std::atomic<double>, parh::kDecks> meterSum_{};   ///< sums of squares since the editor last took them
    std::atomic<int> meterCount_{ 0 };               ///< samples in those sums
    std::atomic<float> outPeak_{ 0.0f };   ///< the output's peak since the editor last took it
    std::atomic<float> lufs_{ -70.0f };   ///< the output's momentary loudness, LUFS (400 ms)
    /** @brief A biquad in transposed direct form II (the K weighting). */
    struct Biquad {
        double b0 = 1;   ///< feed-forward coefficient of x[n]
        double b1 = 0;   ///< ... of x[n-1]
        double b2 = 0;   ///< ... of x[n-2]
        double a1 = 0;   ///< feedback coefficient of y[n-1]
        double a2 = 0;   ///< ... of y[n-2]
        double z1 = 0;   ///< the first state
        double z2 = 0;   ///< the second state
        /** @brief One sample @p x through the filter. */
        double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    Biquad kShelf_[2];   ///< the K weighting: its high shelf, per channel
    Biquad kHigh_[2];   ///< ... and its high pass, per channel
    double kMs_ = 0.0;   ///< the K-weighted mean square
    double kCoef_ = 0.0;   ///< its one-pole coefficient
    std::atomic<int> scoreVersion_{ 0 };   ///< counts the scores loaded (the editor redraws)
    parh::CueSender cues_;                            ///< the OSC cues' socket and thread (message thread starts and stops it)
    parh::CueTap cueTap_;                             ///< audio thread: beat range -> cues
    int cuePort_ = 0;                                ///< the port the sender was started for, 0 = off (message thread)
    double lastBeat_ = -1.0;                         ///< the beat after the last block (audio thread), to see a jump
    std::array<std::atomic<int>, 128> ccMap_{};      ///< controller number -> store id, -1 unbound
    std::atomic<int> learn_{ -1 };                   ///< learn()
    std::atomic<bool> mute_{ false };                ///< muted()
    bool forceMute_ = false;                         ///< muteForced()
    std::atomic<double> hostBpm_{ 0.0 };             ///< the host's tempo as the audio thread last saw it, 0 outside a host
    std::atomic<double> playedBpm_{ 0.0 };           ///< the tempo the engine's score was loaded with, 0 as composed
    // Ableton Link in the standalone (02.10.2026, LinkClock.h; Settings > Ableton Link).
    frame::LinkClock link_;                          ///< the session (joined on the timer when the setting is on)
    std::atomic<bool> linkFollowing_{ false };       ///< other apps are in the session: it rules tempo and phase as a host does
    bool linkPlayed_ = false;                        ///< audio thread: the transport last told to or taken from the session
    /** @brief Whether a clock outside rules the tempo: a host's playhead, or a Link session with other apps in it. */
    bool followsClock() const { return wrapperType != wrapperType_Standalone || linkFollowing_.load(std::memory_order_relaxed); }
    parh::MeterSink meterSink_;                      ///< the strips' levels, added by the decks (the Mixer page)
    std::atomic<int64_t> stripSamples_{ 0 };         ///< samples rendered since takeStripMeters
    frame::UndoHistory history_;                     ///< undo and redo (message thread)
    bool restoring_ = false;                         ///< an undo or the headset moves knobs: no step of their own
    frame::Headset headset_;                         ///< the Quest's hands (message thread)
    juce::TimedCallback headsetTick_{ [this] { pollHeadset(); } };   ///< polls the headset 30 times a second
    uint32_t toldSounds_ = 0;                        ///< the engine's soundsVersion() the host was last told of
};
