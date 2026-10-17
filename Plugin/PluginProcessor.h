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
 * @author Rene Weller
 */
#pragma once
#include "parh/Engine.h"
#include "parh/SetFile.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Set.h"
#include "Frame.h"
#include "LinkClock.h"
#include "Jam.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

/** @brief One entry of the ParamStore as a host parameter. */
class StoreParameter final : public juce::RangedAudioParameter
{
public:
    /**
     * @brief Binds parameter @p id of @p store (which outlives this object) under the display name @p name.
     * @param[in,out] store  the parameter store
     * @param[in]     id     the parameter
     * @param[in]     name   the display name
     */
    StoreParameter( parh::ParamStore &store, int id, const juce::String &name );
    /**
     * the store's value, normalised
     * @return the store's value, normalised
     */
    float getValue() const override;
    /**
     * writes the store (relaxed atomic)
     * @param[in] newValue  the value, normalised
     */
    void setValue( float newValue ) override;
    /**
     * the descriptor's default, normalised
     * @return the default, normalised
     */
    float getDefaultValue() const override;
    /**
     * the display name (0: no limit)
     * @param[in] maximumStringLength  the longest name the host takes, 0 for no limit
     * @return the display name
     */
    juce::String getName( int maximumStringLength ) const override;
    /**
     * the unit
     * @return the unit
     */
    juce::String getLabel() const override;
    /**
     * steps of a discrete parameter
     * @return the steps of a discrete parameter
     */
    int getNumSteps() const override;
    /**
     * Int, Choice and Toggle are discrete
     * @return true for Int, Choice and Toggle
     */
    bool isDiscrete() const override;
    /**
     * a Toggle is boolean
     * @return true for a Toggle
     */
    bool isBoolean() const override;
    /**
     * the value as the panel shows it
     * @param[in] normalisedValue      the value, normalised
     * @param[in] maximumStringLength  the longest text the host takes
     * @return the value as the panel shows it
     */
    juce::String getText( float normalisedValue, int maximumStringLength ) const override;
    /**
     * parses a choice name, On/Off or a number
     * @param[in] text  a choice name, On/Off or a number
     * @return the value, normalised
     */
    float getValueForText( const juce::String &text ) const override;
    /**
     * the store's own mapping
     * @return the store's mapping
     */
    const juce::NormalisableRange<float> &getNormalisableRange() const override { return m_range; }
    /**
     * the id in the store
     * @return the id in the store
     */
    int paramId() const { return m_id; }

private:
    parh::ParamStore              &m_store;   ///< the store it reads and writes
    int                            m_id;      ///< the id in the store
    juce::String                   m_name;    ///< the display name
    juce::NormalisableRange<float> m_range;   ///< the store's mapping between the real and the normalised value
};

/** @brief A track where it lies in what plays (a track alone: at beat 0 on deck A). */
struct TrackPlace
{
    double          m_start = 0.0;    ///< set beat of the track's first bar
    double          m_swapIn = 0.0;   ///< set beat where it takes over the low end
    double          m_end = 0.0;      ///< set beat of its end
    int             m_deck = 0;       ///< the deck it plays on
    parh::TrackInfo m_info;           ///< what the composer says of it
};

/** @brief What plays: a set, or a track as deck A of a one-track set, with where its tracks lie. */
struct Playing
{
    parh::SetScore          m_set;             ///< the set, or the one track as deck A
    bool                    m_isSet = false;   ///< a set (a DJ mix), not a single track
    std::vector<TrackPlace> m_tracks;          ///< its tracks where they lie
    parh::SetInfo           m_setInfo;         ///< a set's (empty for a track)
    bool m_resume = false;   ///< the performer's "now": loaded where the engine is, and it plays on (performNow)
    std::vector<parh::Rewrite>
        m_rewrites;   ///< the "now"s this track was rewritten by, in order (a next one keeps them)
};

/** @brief The Parhelion processor. */
class ParhelionProcessor final : public juce::AudioProcessor,
                                 private juce::Thread,
                                 private juce::Timer,
                                 private juce::AudioProcessorParameter::Listener
{
public:
    ParhelionProcessor();             ///< registers every parameter and composes a first track
    ~ParhelionProcessor() override;   ///< stops the composer and the exporter

    // Composing and curating.
    void compose();   ///< compose with the current settings, seed and rerolls
    void newSeed();   ///< a fresh seed, no rerolls, then compose
    /**
     * @brief Draws @p unit again ("blocks", or in a set "track3.blocks", "track3", "set"), then composes.
     * @param[in] unit  the unit drawn again
     */
    void reroll( const juce::String &unit );
    /**
     * @brief "Breakdown now" or "Drop now" (PLAN 9, Perform): the track that plays, rewritten from its next 8-bar line
     *        (the one after it where that is under six seconds away, so the composer is done in time) into a breakdown
     *        (its build, a drop) or a drop at once (planTrackRewritten); loaded where it plays, it plays on, its level
     *        corrections kept. A set is not rewritten (its next blend would have to move with it).
     * @param[in] kind  a breakdown or a drop
     * @return the bar the rewrite begins at, or -1 where there is nothing to rewrite
     */
    int performNow( parh::SectionKindE kind );
    /**
     * @brief The bar of the last performNow while it waits to be heard, -1 none.
     * @return the bar, -1 for none
     */
    int pendingNow() const { return m_nowBar.load(); }
    /**
     * whether the composer thread is at work
     * @return true while the composer thread works
     */
    bool isComposing() const { return m_composing.load(); }
    /**
     * @brief Whether the knobs ask for a DJ mix (set.minutes above 0) rather than a single track.
     * @return true when the knobs ask for a DJ mix
     */
    bool mixChosen() const;
    /**
     * @brief A single track or a DJ mix (message thread, 01.10.2026 -- after Totality's Phase 21): set.minutes to 0, or
     *        back to the mix's last length (60 min at first); then composes, if what plays is not that already.
     * @param[in] mix  true for a DJ mix, false for a single track
     * @pre called on the message thread
     */
    void chooseMix( bool mix );
    /**
     * whether what plays is a DJ mix
     * @return true when a DJ mix plays
     */
    bool playingMix() const
    {
        std::lock_guard<std::mutex> g( m_lock );
        return m_current.m_isSet;
    }
    /**
     * the seed of what plays
     * @return the seed
     */
    uint64_t seed() const
    {
        std::lock_guard<std::mutex> g( m_lock );
        return m_seed;
    }
    /**
     * the rerolls, for the panel
     * @return the rerolls as text
     */
    juce::String curationText() const;
    /**
     * @brief The track under @p beat (its index in what plays), -1 if none. In a set, the one that owns the low end.
     * @param[in] beat  the position in beats
     * @return the track's index, -1 for none
     */
    int trackAt( double beat ) const;
    /**
     * @brief Phase 17: rates the track under the playhead (+1 liked, -1 not) into Documents/Parhelion/ratings.tsv; the
     *        ratings weigh the archetypes and preset groups while compose.use_ratings is on (Preferences.h).
     * @param[in] value  +1 liked, -1 not
     * @return what was rated, for the panel (empty when nothing plays)
     */
    juce::String rate( int value );

    // Transport (the standalone's; a host drives its own).
    /**
     * play or stop (the standalone's transport)
     * @param[in] on  true to play
     */
    void setPlaying( bool on ) { m_playing = on; }
    /**
     * whether the standalone plays
     * @return true while it plays
     */
    bool isPlaying() const { return m_playing.load(); }
    /**
     * @brief The standalone's Ableton Link, as the settings menu says it: off, alone, or how many apps are with it.
     * @return the Link's state as text
     */
    juce::String linkStatus() const;
    /**
     * @brief The family jam, as the settings menu says it (Jam.h): off, leading, or whom it follows.
     * @return the jam's state as text
     */
    juce::String jamStatus() const { return m_jam.status(); }
    /**
     * @brief The transposition a follower plays now (semitones; the family jam's root, Jam.h).
     * @return the transposition in semitones
     */
    int jamTranspose() const { return m_jamTransposeOut.load( std::memory_order_relaxed ); }
    /**
     * @brief Jumps to @p beat at the next block (the display goes there at once, also while nothing plays).
     * @param[in] beat  the position in beats
     */
    void seekTo( double beat )
    {
        m_seekRequest = beat;
        m_position = beat;
    }
    /**
     * where the audio thread is, in beats
     * @return the position in beats
     */
    double positionBeats() const { return m_position.load(); }
    /**
     * @brief Counts the scores the engine has loaded: a display compares it to know when to copy again.
     * @return the number of scores loaded
     */
    int scoreVersion() const { return m_scoreVersion.load(); }
    /**
     * @brief A copy of what plays (message thread; for the arrange and eclipse views).
     * @param[out] out  the copy
     * @pre called on the message thread
     */
    void copyPlaying( Playing &out ) const
    {
        std::lock_guard<std::mutex> g( m_lock );
        out = m_current;
    }
    /**
     * @brief The factory preset the composer chose for instance @p instance of synth @p m in the track whose sounds the
     *        knobs show (Engine::leadDeck): its index in parh::factoryPresets(m), -1 if none.
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @return the preset's index, -1 for none
     */
    int composedPreset( parh::ModuleE m, int instance ) const;
    /**
     * @brief Puts factory preset @p index of @p m on instance @p instance's knobs, through the host's parameters.
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @param[in] index     the factory preset's index
     */
    void applyPreset( parh::ModuleE m, int instance, int index );
    /**
     * @brief Sets the knobs @p values (store id, real value) through the host's parameters as one undo step named @p what
     *        (15.10.2026: a value typed into the command palette).
     * @param[in] what    the undo step's name
     * @param[in] values  (store id, real value) of the knobs
     */
    void applyKnobs( const juce::String &what, const std::vector<std::pair<int, float>> &values );
    /**
     * @brief Plays @p values (store id, real value) while a preset is listened to in the browser -- in the store only: no
     *        undo step, nothing to the host --; endPreview() takes them back.
     * @param[in] values  (store id, real value) of the knobs
     */
    void previewKnobs( const std::vector<std::pair<int, float>> &values );
    /** @brief Back to the knobs as they were before previewKnobs (nothing when no preview plays). */
    void endPreview();
    /**
     * @name User presets (after Phosphene's)
     * A synth's own sounds, `<name>.txt` in Documents/Parhelion/Presets/\<synth\> (the kit's twelve lanes share "perc"):
     * every knob a factory preset sets, one `knob=value` a line with the key inside the module ("cutoff=2400"), so a
     * lane's sound loads into any lane (parh::userPresetText). PARH_USER_DIR moves the folder.
     * @{ */
    /**
     * @brief The folder of one synth's user presets.
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @return the folder
     */
    juce::File userPresetFolder( parh::ModuleE m, int instance ) const;
    /**
     * @brief Its user presets, sorted by name.
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @return the preset files
     */
    juce::Array<juce::File> userPresets( parh::ModuleE m, int instance ) const;
    /**
     * @brief Saves the synth's knobs as the user preset @p name; the file, or File() when it failed.
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @param[in] name      the preset's name
     * @return the file, or File() when it failed
     */
    juce::File saveUserPreset( parh::ModuleE m, int instance, const juce::String &name );
    /**
     * @brief Puts user preset @p file on the synth's knobs, the rest of the sound at its defaults (as a factory
     *        preset).
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @param[in] file      the preset's file
     * @return false if it cannot be read
     */
    bool applyUserPreset( parh::ModuleE m, int instance, const juce::File &file );
    /** @} */
    /**
     * @brief The length of what plays, in beats and seconds (as composed).
     * @param[out] beats    the length in beats
     * @param[out] seconds  the length in seconds
     */
    void length( double &beats, double &seconds ) const;

    // Files.
    /**
     * writes seed, lengths, rerolls and parameters as an .parhset
     * @param[in] file  the file
     * @return false if it cannot be written
     */
    bool saveSet( const juce::File &file );
    /**
     * reads an .parhset and composes it
     * @param[in] file  the file
     * @return false if it cannot be read
     */
    bool loadSet( const juce::File &file );
    /**
     * @brief Loads one of the factory's DJ sets (parh/FactorySets.h) and composes it.
     * @param[in] index  the set
     * @return false when there is none of that index
     */
    bool loadFactorySet( int index );
    /**
     * @brief Reads a set from its text (a file's, a factory set's) and composes it.
     * @param[in] name  what the undo step calls it
     * @param[in] text  the set's text
     * @return false when the text is no set
     */
    bool loadSetText( const juce::String &name, const std::string &text );
    /** @brief What an export writes beside the WAV and its MIDI and cues. */
    enum ExportExtraE
    {
        STEMS = 1,
        LOOPS = 2
    };
    /**
     * @brief Renders what plays offline (as composed: not live) to a 24-bit WAV with its cues (a cue chunk and JSON) and
     *        MIDI beside it, on a thread; returns at once. @p extras: STEMS a folder "\<name\>_stems" (their sum is the mix
     *        before the master), LOOPS a folder "\<name\>_loops" with the DJ loops (a track only).
     * @param[in] wav     the WAV file
     * @param[in] extras  the extras, a combination of STEMS and LOOPS
     */
    void exportTo( const juce::File &wav, int extras );
    /**
     * one line for the panel
     * @return the status line
     */
    juce::String status() const;
    /**
     * Phase 17: Documents/Parhelion/ratings.tsv
     * @return the ratings file
     */
    static juce::File ratingsFile();
    /**
     * @brief The test mode (PARH_SEED, PARH_PLAY = seconds, PARH_RECORD = a WAV file; PARH_SET = minutes, a set): a fixed
     *        seed, play at once, record what the audio thread renders; recordingDone() when the seconds are full.
     * @return true when the test mode's seconds are recorded
     */
    bool recordingDone() const { return m_recordTarget > 0 && m_recordPos.load() >= m_recordTarget; }
    /**
     * writes the recording (message thread)
     * @pre called on the message thread
     */
    void writeRecording();
    /**
     * @brief The meters since the last call (message thread): per deck the peak and the RMS after its mixer channel, the
     *        output's peak, and its loudness over the last 400 ms (K-weighted, BS.1770), -70 in silence.
     * @param[out] deckPeak       the peak per deck
     * @param[out] deckRms        the RMS per deck
     * @param[out] outPeak        the output's peak
     * @param[out] momentaryLufs  the output's momentary loudness, LUFS
     * @pre called on the message thread
     */
    void takeMeters( float *deckPeak, float *deckRms, float &outPeak, float &momentaryLufs );
    /**
     * @brief The mixer's strips since the last call (message thread, the Mixer page): per parh::Deck::Stem the loudest
     *        sample and the RMS, both decks together.
     * @param[out] peak  the peak per strip
     * @param[out] rms   the RMS per strip
     * @pre called on the message thread
     */
    void takeStripMeters( float *peak, float *rms );

    // Muting.
    /**
     * the output is silenced
     * @return true while the output is silenced
     */
    bool muted() const { return m_mute.load( std::memory_order_relaxed ); }
    /**
     * @brief Mutes or unmutes; does nothing while `PARH_MUTE` forces it.
     * @param[in] on  true to mute
     */
    void setMuted( bool on )
    {
        if ( !m_forceMute ) m_mute.store( on, std::memory_order_relaxed );
    }
    /**
     * `PARH_MUTE` (or `PARH_SHOT`) was set: the switch is stuck on
     * @return true when the environment forces the mute
     */
    bool muteForced() const { return m_forceMute; }

    // Undo (the frame).
    /**
     * takes the last step back; false if there was none
     * @return false if there was no step
     */
    bool undo();
    /**
     * makes the last undone step again
     * @return false if there was no step
     */
    bool redo();
    /**
     * what undo takes back (empty: nothing)
     * @return the step's name, empty for none
     */
    juce::String undoName() const { return m_history.undoName(); }
    /**
     * what redo makes again
     * @return the step's name, empty for none
     */
    juce::String redoName() const { return m_history.redoName(); }
    /**
     * @brief Opens a step of several knobs (a preset, a reset): they are undone together. Close with endStep.
     * @param[in] what  the step's name
     */
    void beginStep( const juce::String &what );
    void endStep();   ///< closes beginStep's step
    /**
     * @brief Store id @p id back to its default (one step).
     * @param[in] id  the store id
     */
    void resetToDefault( int id );

    // The panel's live rings.
    /**
     * @brief The value store id @p id plays at the moment, normalised -- NaN where it is the knob's own.
     * @param[in] id  the store id
     * @return the value played, normalised, or NaN
     */
    float playedNormalised( int id ) const;

    // The headset (the frame).
    /**
     * the hands arriving from the Quest app
     * @return the headset
     */
    frame::Headset &headset() { return m_headset; }

    // Performing.
    /**
     * @brief Binds the next MIDI controller that arrives to store id @p id; -1 cancels.
     * @param[in] id  the store id, -1 to cancel
     */
    void learn( int id ) { m_learn = id; }
    /**
     * the store id waiting for a controller, or -1
     * @return the store id, or -1
     */
    int learning() const { return m_learn.load(); }
    /**
     * @brief The controller bound to store id @p id, or -1.
     * @param[in] id  the store id
     * @return the controller, or -1
     */
    int controllerFor( int id ) const;
    /**
     * @brief Unbinds store id @p id.
     * @param[in] id  the store id
     */
    void forget( int id );

    /**
     * the engine's parameters
     * @return the parameters
     */
    parh::ParamStore &store() { return m_engine.params(); }
    /**
     * @brief The parameters (read only).
     * @return the parameters
     */
    const parh::ParamStore &store() const { return m_engine.params(); }
    /**
     * @brief The host parameter of store id @p id, or null.
     * @param[in] id  the store id
     * @return its host parameter, or null
     */
    StoreParameter *parameter( int id )
    {
        return id >= 0 && id < static_cast<int>( m_params.size() ) ? m_params[static_cast<size_t>( id )] : nullptr;
    }
    /**
     * @brief Sets store id @p id to the real value @p value through its host parameter (a gesture).
     * @param[in] id     the store id
     * @param[in] value  the real value
     */
    void setFromUi( int id, float value ) { setFromMidi( id, value ); }

    // juce::AudioProcessor: a stereo instrument, one program, the state as XML.
    /**
     * prepares the engine and reloads the score
     * @param[in] sampleRate       the sample rate
     * @param[in] samplesPerBlock  the largest block
     */
    void prepareToPlay( double sampleRate, int samplesPerBlock ) override;
    void releaseResources() override {}   ///< nothing to release
    /**
     * stereo out only
     * @param[in] layouts  the layout asked for
     * @return true for a stereo output
     */
    bool isBusesLayoutSupported( const BusesLayout &layouts ) const override;
    /**
     * plays, following the host's playhead
     * @param[out]    buffer  the block's audio
     * @param[in,out] midi    the block's MIDI in, then the composer's notes out
     */
    void processBlock( juce::AudioBuffer<float> &, juce::MidiBuffer & ) override;
    /**
     * the panel
     * @return a new panel
     */
    juce::AudioProcessorEditor *createEditor() override;
    /**
     * it has one
     * @return true
     */
    bool hasEditor() const override { return true; }
    /**
     * "Parhelion"
     * @return the plugin's name
     */
    const juce::String getName() const override { return JucePlugin_Name; }
    /**
     * MIDI in: the performer's keys and controllers
     * @return true
     */
    bool acceptsMidi() const override { return true; }
    /**
     * MIDI out: the composer's notes as they play (02.10.2026)
     * @return true
     */
    bool producesMidi() const override { return true; }
    /**
     * the rooms ring on
     * @return the tail in seconds
     */
    double getTailLengthSeconds() const override { return 8.0; }
    /**
     * one program
     * @return 1
     */
    int getNumPrograms() override { return 1; }
    /**
     * always the one
     * @return 0
     */
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram( int ) override {}   ///< nothing to switch
    /**
     * "Set"
     * @return the program's name
     */
    const juce::String getProgramName( int ) override { return "Set"; }
    void changeProgramName( int, const juce::String & ) override {}   ///< not renameable
    /**
     * seed, rerolls, parameters, controllers as XML
     * @param[out] destData  the state
     */
    void getStateInformation( juce::MemoryBlock &destData ) override;
    /**
     * restores them and composes
     * @param[in] data         the state
     * @param[in] sizeInBytes  its size in bytes
     */
    void setStateInformation( const void *data, int sizeInBytes ) override;

private:
    /** @brief Nothing: a value alone is no step (the gestures are). */
    void parameterValueChanged( int, float ) override {}
    /**
     * a knob on the panel: a step
     * @param[in] parameterIndex     the host parameter's index
     * @param[in] gestureIsStarting  true when the gesture begins
     */
    void parameterGestureChanged( int parameterIndex, bool gestureIsStarting ) override;
    /**
     * every store value (undo)
     * @return every store value
     */
    std::vector<float> values() const;
    /**
     * seed, rerolls and the mix's length as text (undo)
     * @return the state beyond the knobs as text
     */
    juce::String extraState() const;
    /**
     * the inverse; composes if seed or rerolls changed
     * @param[in] text  the state as extraState() wrote it
     */
    void applyExtra( const juce::String &text );
    /**
     * @brief Puts undo step @p s back (@p after false) or makes it again (@p after true).
     * @param[in] s      the undo step
     * @param[in] after  true to make it again, false to take it back
     */
    void applyStep( const frame::UndoStep &s, bool after );
    /**
     * the hands' events, 30 times a second (message thread)
     * @pre called on the message thread
     */
    void pollHeadset();
    void run() override;   ///< the composer thread
    /**
     * loads a finished score on the message thread
     * @pre called on the message thread
     */
    void timerCallback() override;
    /**
     * @brief Composes with the knobs as they are (copied into @p snapshot).
     * @param[out] snapshot  the knobs composed with
     * @return what was composed
     */
    Playing composeNow( parh::ParamStore &snapshot );
    /**
     * @brief Hands the loudness corrections, measured while it plays, to the engine (message thread).
     * @pre called on the message thread
     */
    void takeTrims();
    /**
     * @brief @p p's set as the engine plays it here: in a host at the host's tempo (constant), in the standalone as
     *        composed.
     * @param[in] p  what was composed
     * @return the set as it plays here
     */
    parh::SetScore forPlayback( const Playing &p ) const;
    /**
     * @brief Loads @p p into the engine (message thread, processing suspended by the caller).
     * @param[in] p  what was composed
     * @pre called on the message thread
     */
    void loadEngine( const Playing &p );
    /**
     * @brief The performer's MIDI: keys toggle the mutes, controllers move what they are bound to (audio thread).
     * @param[in] midi  the block's MIDI
     * @pre called on the audio thread
     */
    void perform( const juce::MidiBuffer &midi );
    int m_keyboardSeen = 0;   ///< the keyboard target of the last block (audio thread): a change releases every key
    // MIDI out (02.10.2026): the composer's notes as the decks play them, on the channels of the MIDI export (Midi.h).
    parh::NoteTap m_noteTap;   ///< what the decks played in the last process() call (audio thread)
    frame::KeyMemory
        m_keyMemory;   ///< where each held key went (the split, Scale Lock), so its release follows (02.10.2026)
    /** @brief A note-off due later, for a note whose part has none of its own (NoteTap::Note::offSample). */
    struct PendingOff
    {
        int64_t m_sample;    ///< when, on the engine's sample counter
        uint8_t m_channel;   ///< MIDI channel, 1..16
        uint8_t m_pitch;     ///< MIDI note
    };
    std::array<PendingOff, 256> m_pendingOff{};      ///< the note-offs not yet sent (audio thread)
    int                         m_pendingOffs = 0;   ///< how many of m_pendingOff are due
    int64_t                     m_midiExpect = -1;   ///< the sample the next block should start at; another is a jump
    bool                        m_midiSounding = false;   ///< a note-on went out since the last all-notes-off
    /**
     * @brief Writes the notes the decks played in the block from @p start (@p n samples) into @p midi, and the due
     *        offs.
     * @param[in,out] midi   the block's MIDI, added to
     * @param[in]     start  the block's first sample
     * @param[in]     n      the block's samples
     */
    void emitMidi( juce::MidiBuffer &midi, int64_t start, int n );
    /**
     * @brief All notes off on every channel, at the block's start, when a note sounded (a stop, a jump).
     * @param[in,out] midi  the block's MIDI, added to
     */
    void silenceMidi( juce::MidiBuffer &midi );
    /**
     * @brief Sets store id @p id to the real value @p value through its host parameter.
     * @param[in] id     the store id
     * @param[in] value  the real value
     */
    void setFromMidi( int id, float value );

    parh::Engine                  m_engine;     ///< the engine
    std::vector<StoreParameter *> m_params;     ///< the host parameters, one per store id (owned by the processor)
    uint64_t                      m_seed = 1;   ///< the seed of what plays
    parh::Curation                m_curation;   ///< the rerolls and locks
    mutable std::mutex            m_lock;       ///< guards what the composer thread hands over
    std::vector<parh::Rating>     m_ratings;    ///< Phase 17: the player's ratings (m_lock)
    std::unique_ptr<Playing>      m_pending;    ///< composed, waiting to be loaded
    Playing                       m_current;    ///< what the engine plays
    std::atomic<bool>             m_composing{ false };   ///< the composer thread works
    std::atomic<bool>             m_playing{ false };     ///< play is on
    std::atomic<bool>             m_exporting{ false };   ///< an export runs
    std::atomic<bool>             m_again{ false };   ///< compose was asked for while composing: once more when done
    std::atomic<int>              m_nowBar{ -1 };     ///< performNow's bar, -1 none (read by composeNow)
    parh::SectionKindE            m_nowKind = parh::SectionKindE::BREAKDOWN;   ///< and its kind (m_lock)
    // The loudness (Leveler.h): measured on the composer thread once the score is handed over, while it plays.
    std::atomic<bool>  m_newer{ false };       ///< a newer score is asked for: the measuring of the last one stops
    uint64_t           m_composed = 0;         ///< counts the compositions
    uint64_t           m_pendingId = 0;        ///< m_pending's number (m_lock)
    uint64_t           m_playingId = 0;        ///< m_current's number (m_lock)
    std::vector<float> m_trims[parh::Decks];   ///< the corrections found for the composition m_trimsFor (m_lock)
    std::vector<parh::BalanceDbT> m_bal[parh::Decks];   ///< Phase 18: the parts' corrections found with them (m_lock)
    uint64_t                      m_trimsFor = 0;       ///< the composition m_trims and m_bal belong to (m_lock)
    bool                          m_levelled = false;   ///< m_current carries its corrections (m_lock)
    std::atomic<double>           m_position{ 0.0 };    ///< where the engine is, beats (audio thread writes)
    std::atomic<double>           m_seekRequest{ -1.0 };    ///< a jump asked for, beats; -1 none
    double                        m_sampleRate = 48000.0;   ///< the sample rate, Hz
    int                           m_blockSize = 512;        ///< the largest block, samples
    juce::String                  m_lastExport;             ///< the last export's result, for the Export tab
    float m_mixMinutes = 60.0f;   ///< the mix's length while a single track is chosen (chooseMix; the state)
    std::unique_ptr<std::thread> m_exporter;   ///< the export thread while it runs
    std::vector<float>           m_record;     ///< interleaved, allocated in prepareToPlay in the test mode only
    size_t                       m_recordTarget = 0;   ///< PARH_RECORD: how many values m_record takes, 0 off
    std::atomic<size_t>          m_recordPos{ 0 };     ///< how many are written
    bool                         m_autoPlay = false;   ///< PARH_PLAY: play once the first score is loaded
    // The meters: the decks through their taps, the output's peak and a K-weighted mean square.
    std::vector<float>               m_tapBuf;   ///< Decks x 2 x block (prepareToPlay)
    std::array<float *, parh::Decks> m_tapL{};   ///< per deck: its tap's left channel in m_tapBuf
    std::array<float *, parh::Decks> m_tapR{};   ///< per deck: its tap's right channel in m_tapBuf
    // The stems as outputs of their own (02.10.2026): a stereo bus per stem (Engine::stemName) besides the main output,
    // off until the host switches one on; then the engine writes every stem (Engine::setStems) and each bus that is on
    // gets its own. Pre-master, as the export's stems: they sum to the mix before the master.
    std::vector<float>                       m_stemBuf;           ///< STEMS x 2 x block (prepareToPlay)
    std::array<float *, parh::Engine::STEMS> m_stemL{};           ///< per stem: its left channel in m_stemBuf
    std::array<float *, parh::Engine::STEMS> m_stemR{};           ///< per stem: its right channel in m_stemBuf
    bool                                     m_stemsOn = false;   ///< audio thread: the engine writes the stems now
    /**
     * @brief The buses: the main stereo output, then one stereo output per stem, those off by default.
     * @return the buses
     */
    static BusesProperties busLayout();
    std::array<std::atomic<float>, parh::Decks> m_meterPeak{};   ///< audio thread raises, the editor takes (exchange 0)
    std::array<std::atomic<double>, parh::Decks> m_meterSum{};   ///< sums of squares since the editor last took them
    std::atomic<int>                             m_meterCount{ 0 };   ///< samples in those sums
    std::atomic<float> m_outPeak{ 0.0f };   ///< the output's peak since the editor last took it
    std::atomic<float> m_lufs{ -70.0f };    ///< the output's momentary loudness, LUFS (400 ms)
    /** @brief A biquad in transposed direct form II (the K weighting). */
    struct Biquad
    {
        double m_b0 = 1;   ///< feed-forward coefficient of x[n]
        double m_b1 = 0;   ///< ... of x[n-1]
        double m_b2 = 0;   ///< ... of x[n-2]
        double m_a1 = 0;   ///< feedback coefficient of y[n-1]
        double m_a2 = 0;   ///< ... of y[n-2]
        double m_z1 = 0;   ///< the first state
        double m_z2 = 0;   ///< the second state
        /**
         * @brief One sample @p x through the filter.
         * @param[in] x  the input sample
         * @return the output sample
         */
        double run( double x )
        {
            const double y = m_b0 * x + m_z1;
            m_z1 = m_b1 * x - m_a1 * y + m_z2;
            m_z2 = m_b2 * x - m_a2 * y;
            return y;
        }
    };
    Biquad           m_kShelf[2];           ///< the K weighting: its high shelf, per channel
    Biquad           m_kHigh[2];            ///< ... and its high pass, per channel
    double           m_kMs = 0.0;           ///< the K-weighted mean square
    double           m_kCoef = 0.0;         ///< its one-pole coefficient
    std::atomic<int> m_scoreVersion{ 0 };   ///< counts the scores loaded (the editor redraws)
    parh::CueSender  m_cues;                ///< the OSC cues' socket and thread (message thread starts and stops it)
    parh::CueTap     m_cueTap;              ///< audio thread: beat range -> cues
    int              m_cuePort = 0;         ///< the port the sender was started for, 0 = off (message thread)
    double           m_lastBeat = -1.0;     ///< the beat after the last block (audio thread), to see a jump
    std::array<std::atomic<int>, 128>  m_ccMap{};             ///< controller number -> store id, -1 unbound
    std::atomic<int>                   m_learn{ -1 };         ///< learn()
    std::atomic<bool>                  m_mute{ false };       ///< muted()
    bool                               m_forceMute = false;   ///< muteForced()
    std::vector<std::pair<int, float>> m_previewSaved;        ///< the knobs a preview replaced (store id, real value)
    std::atomic<double> m_hostBpm{ 0.0 };     ///< the host's tempo as the audio thread last saw it, 0 outside a host
    std::atomic<double> m_playedBpm{ 0.0 };   ///< the tempo the engine's score was loaded with, 0 as composed
    // Ableton Link in the standalone (02.10.2026, LinkClock.h; Settings > Ableton Link).
    frame::LinkClock m_link;   ///< the session (joined on the timer when the setting is on)
    // The family jam (02.10.2026, Jam.h; Settings > Family jam).
    /**
     * @brief Audio thread: as the jam's leader, the sections and keys of the coming beat go out, stamped with their
     *        bar.
     * @param[in] before  the block's first beat
     * @param[in] after   the beat after the block
     * @param[in] shared  the shared clock's beat
     * @pre called on the audio thread
     */
    void jamLead( double before, double after, double shared );
    /**
     * @brief Audio thread: as a follower, the engine takes the leader's root (on a bar line), energy and breaks.
     * @param[in] before  the block's first beat
     * @param[in] shared  the shared clock's beat
     * @pre called on the audio thread
     */
    void jamFollow( double before, double shared );
    /**
     * @brief The root (pitch class) of the track playing at @p beat: its last key mark, else the score's key.
     * @param[in] beat  the position in beats
     * @return the root's pitch class
     */
    int homeRootAt( double beat ) const;
    /**
     * @brief Which of compose.scale's values a mode named @p mode is (-1: none of them).
     * @param[in] mode  the mode's name
     * @return the scale's value, -1 for none
     */
    static int scaleOfMode( const juce::String &mode );
    frame::JamBus     m_jam{ "Parhelion" };       ///< the family jam's bus
    bool              m_jamLeadStarted = false;   ///< audio thread: the leader has said where it stands
    double            m_jamLeadLast = -1.0;       ///< audio thread: where the leader's last block ended (a jump)
    double            m_jamLastBefore = -1.0;     ///< audio thread: the previous block's start (its length in beats)
    int               m_jamTranspose = 0;         ///< audio thread: the transposition the engine plays now
    std::atomic<int>  m_jamTransposeOut{ 0 };     ///< the same, for the message thread
    bool              m_jamFollowing = false;     ///< audio thread: the engine follows the jam (setJam was called)
    std::atomic<int>  m_jamComposeKey{ -1 };      ///< a follower's next track: the leader's root (-1: its own key)
    std::atomic<int>  m_jamComposeScale{ -1 };    ///< and its mode as compose.scale (-1: its own)
    juce::String      m_jamLogged;                ///< the last line written to FAMILY_JAM_LOG
    std::atomic<bool> m_linkFollowing{
        false
    };   ///< other apps are in the session: it rules tempo and phase as a host does
    double m_linkBeat = -1.0;      ///< audio thread: the session's beat at this block (the jam's timeline)
    bool   m_linkPlayed = false;   ///< audio thread: the transport last told to or taken from the session
    /**
     * @brief Whether a clock outside rules the tempo: a host's playhead, or a Link session with other apps in it.
     * @return true when a clock outside rules the tempo
     */
    bool followsClock() const
    {
        return wrapperType != wrapperType_Standalone || m_linkFollowing.load( std::memory_order_relaxed );
    }
    parh::MeterSink      m_meterSink;           ///< the strips' levels, added by the decks (the Mixer page)
    std::atomic<int64_t> m_stripSamples{ 0 };   ///< samples rendered since takeStripMeters
    frame::UndoHistory   m_history;             ///< undo and redo (message thread)
    bool                 m_restoring = false;   ///< an undo or the headset moves knobs: no step of their own
    frame::Headset       m_headset;             ///< the Quest's hands (message thread)
    juce::TimedCallback  m_headsetTick{ [this] { pollHeadset(); } };   ///< polls the headset 30 times a second
    uint32_t             m_toldSounds = 0;   ///< the engine's soundsVersion() the host was last told of
};
