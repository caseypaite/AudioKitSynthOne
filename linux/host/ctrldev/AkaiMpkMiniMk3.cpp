//
//  AkaiMpkMiniMk3.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Akai MPK Mini mk3. The keybed needs no special handling at
//  all -- it flows through the ordinary MidiQueue -> Engine::handleMidi()
//  path as regular notes, exactly like any other MIDI keyboard, regardless
//  of which MIDI channel the device sends on.
//
//  8 of the 16 pads (table indices 0-7 -- see parsePads()) are claimed as
//  function pads via PadFilter (host/PadFilter.h): they stop playing notes
//  entirely and become dedicated buttons instead. Bottom row (indices 0-3,
//  silkscreened PAD1-4): reported outward as a PadReport for the host to
//  turn into a GUI panel switch, since this driver has no UiState access of
//  its own. Top row (indices 4-7, PAD5-8): handled internally via Engine& --
//  panic, all-notes-off, arp on/off, arp<->sequencer mode. The remaining 8
//  pads (table indices 8-15) are left unclaimed and play notes normally.
//
//  This driver also gives the 8 knobs sensible default parameter targets
//  (Engine::setDeviceDefaultCc), discovered by querying the device's own
//  currently-stored program over SysEx.
//
//  Modes: the device's PAD CONTROLS row has a PROG SELECT button, a factory,
//  no-reprogramming-required way to switch between up to 9 onboard "program"
//  slots (0 = RAM/current, 1-8 = stored). Selecting one is expected to send a
//  MIDI Program Change (this is the same mechanism Zynthian's own driver
//  relies on for its mode switching). This driver treats each program number
//  as a distinct knob-mapping "mode": onProgramChange() clears the current
//  knob defaults and re-queries that slot's actual CC assignments, so the
//  same 8 physical knobs can reach more than 8 parameters by switching
//  programs on the device. Modes 0-3 have a designed target set below; any
//  other slot just clears the knob defaults and leaves them unbound (no
//  guessing at a target set that was never designed).
//
//  SysEx protocol byte layout below is transcribed from Zynthian's shipped
//  driver (zyngine/ctrldev/zynthian_ctrldev_akai_mpk_mini_mk3.py on the
//  vangelis branch of zynthian/zynthian-ui), itself sourced from
//  https://github.com/tsmetana/mpk3-settings/blob/master/src/message.h --
//  a working reference implementation, not a guess. The defensive checks
//  below (exact reply length, command byte, sane mode/CC values) are there
//  so a wrong assumption degrades to "seed nothing" rather than a bad
//  mapping.
//
//  Real-hardware status (a physical unit was available for one testing
//  session): the *read* path -- sendQuery()/onSysEx()/parsePads()/
//  parseKnobs(), the pad table, the knob table, all offsets above -- is
//  confirmed correct: a live query against a real unit was decoded by hand
//  against these exact offsets and matched what the driver itself bound.
//  Synthetic Note On/Program Change injection into a live, running instance
//  (not physically pressing the device's own buttons) additionally
//  confirmed the *dispatch* logic end to end: a claimed top-row pad note
//  silences voices without playing one, a claimed bottom-row pad note plays
//  nothing either, and onProgramChange()/modeStatusText() correctly re-bind
//  and re-report for modes 0-3 and an undesigned slot alike. Two things
//  remain genuinely unconfirmed, since neither is exercisable without
//  physically touching the device: whether PROG SELECT itself sends a
//  Program Change (this driver only confirmed what happens *if* one
//  arrives), and whether table index N truly corresponds to silkscreened
//  PAD(N+1) (indices 0-3 = bottom row, 4-7 = top row) -- see "Known gaps" in
//  the developer guide for both.
//
//  writeProgram() (CMD_WRITE_DATA) is confirmed working, on the second
//  attempt. The first attempt sent a structurally malformed message (see
//  writeProgram()'s own comment for the exact byte-duplication bug) that
//  the device silently rejected -- initially read as "this device doesn't
//  honor writes", until loading Zynthian's actual shipped driver and
//  comparing its write path byte-for-byte turned up the real cause. Once
//  fixed, --controller-driver-configure was run against real hardware and
//  independently verified by re-querying and hand-decoding all 4
//  reprogrammed slots: every knob came back with the right mode, CC, and
//  name, and everything this driver doesn't touch (pads, program name, arp
//  settings) came back byte-identical to before. See onConfigureReply().
//

#include "AkaiMpkMiniMk3.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "Engine.h"
#include "MidiOutput.h"

namespace s1::ctrldev {

namespace {

// -- SysEx envelope (confirmed) --
constexpr uint8_t kManufacturerAkai  = 0x47;
constexpr uint8_t kDirHostToDevice   = 0x7f;
constexpr uint8_t kDirDeviceToHost   = 0x00;
constexpr uint8_t kProductMpkMiniMk3 = 0x49;
constexpr uint8_t kCmdWriteData      = 0x64;
constexpr uint8_t kCmdQueryData      = 0x66;
constexpr uint8_t kCmdIncomingData   = 0x67; // the device's reply to a query

// -- Program body layout (confirmed; offsets relative to the manufacturer
// byte, i.e. the byte right after the leading 0xF0) --
constexpr int kBodyLength      = 246; // bytes from `program slot` onward, per the length field
constexpr int kFullBodyLength  = 252; // manufacturer..transpose inclusive
constexpr int kWireReplyLength = 254; // 0xF0 + kFullBodyLength + 0xF7
// 16 pads x 3 bytes each: note, program-change, CC. Confirmed offset (43 +
// 16*3 == 91 == kOffKnobs below, a self-consistency check on both
// constants). Only the first 8 table entries (kFunctionPadCount) are
// claimed as function pads -- see parsePads() and the file header comment
// for which table index is assumed to be which physical pad. The
// index-to-physical-pad correspondence itself is NOT confirmed against real
// hardware (see the file header and the developer guide's known-gaps list).
constexpr int kOffPads          = 43;
constexpr int kPadStride        = 3;
constexpr int kFunctionPadCount = 8;
constexpr int kOffKnobs   = 91; // 8 knobs x 20 bytes: mode, CC, min, max, name[16]
constexpr int kKnobStride = 20;
constexpr int kKnobCount  = 8;

constexpr int kModeCount = 4;

/// One row of 8 knob targets per mode, selected by the device's onboard
/// program number (see the file header comment). None of these collide with
/// universally-reserved CCs (mod wheel CC1, sustain CC64) since a target is
/// only ever bound to whatever CC the device's own query reply reports a
/// knob is actually sending, never a hardcoded guess.
constexpr S1Parameter kModeTargets[kModeCount][kKnobCount] = {
    // Mode 0 (program 0, RAM/default): sound -- filter, amp envelope, one
    // modulation rate, two FX sends, master level.
    {cutoff, resonance, attackDuration, releaseDuration,
     lfo1Rate, reverbMix, delayMix, masterVolume},
    // Mode 1 (program 1): oscillators/voice -- the sound-shaping half Mode 0
    // doesn't reach.
    {morph1Volume, morph2Volume, morph2Detuning, subVolume,
     fmAmount, noiseVolume, glide, morphBalance},
    // Mode 2 (program 2): envelope depth -- the filter envelope's full ADSR
    // (independent from Mode 0's amp attack/release), the amp envelope's
    // decay/sustain stages that Mode 0 doesn't reach, how much the filter
    // envelope affects cutoff, and how much the envelope tracks pitch.
    // (cutoffLFO/resonanceLFO were considered here but are the LFO ROUTING
    // matrix's boolean route-enable checkboxes in gui/Panels.cpp, not
    // continuous knobs -- a poor fit for a CC sweep, so they're not used as
    // targets anywhere in this table.)
    {filterAttackDuration, filterDecayDuration, filterSustainLevel, filterReleaseDuration,
     filterADSRMix, adsrPitchTracking, decayDuration, sustainLevel},
    // Mode 3 (program 3): modulation/FX depth -- LFO1 depth, LFO2 rate/depth,
    // phaser, autopan, bitcrush.
    {lfo1Amplitude, lfo2Rate, lfo2Amplitude, phaserMix,
     phaserRate, autoPanAmount, autoPanFrequency, bitCrushSampleRate},
};

/// One name per row of kModeTargets above, for modeStatusText() (the app's
/// own status line/console -- see below) and for renaming each mode's knobs
/// on the device's own screen (see kConfigureCcs and onConfigureReply()).
constexpr const char *kModeNames[kModeCount] = {
    "Sound", "Oscillators/Voice", "Envelope Depth", "Modulation/FX",
};

/// Same names, per knob rather than per mode -- what each knob's onscreen
/// label becomes when --controller-driver-configure (re)writes program
/// slots 0-3 (see onConfigureReply()). Each must fit the device's 16-
/// character name field.
constexpr const char *kModeTargetNames[kModeCount][kKnobCount] = {
    {"Cutoff", "Resonance", "Attack", "Release",
     "LFO1 Rate", "Reverb Mix", "Delay Mix", "Mstr Volume"},
    {"OSC1 Volume", "OSC2 Volume", "OSC2 Detune", "Sub Volume",
     "FM Amount", "Noise Volume", "Glide", "OSC1/2 Bal"},
    {"Filt Attack", "Filt Decay", "Filt Sustain", "Filt Release",
     "Filt/AmpMix", "EnvPitchTrk", "Amp Decay", "Amp Sustain"},
    {"LFO1 Amount", "LFO2 Rate", "LFO2 Amount", "Phaser Mix",
     "PhaserRate", "AutopanAmt", "AutopanRate", "BitcrushRate"},
};

/// CCs assigned to the 8 knobs when (re)configuring a program slot -- the
/// MIDI 1.0 spec leaves 20-31 undefined, so re-provisioning a unit can never
/// hand a knob a universally-reserved CC (mod wheel CC1, sustain CC64,
/// etc.), regardless of what the slot's previous owner had assigned. Same 8
/// CCs reused across all 4 modes -- each mode is a separate onboard program
/// (only one is ever the device's active slot at a time), so there's no
/// cross-mode collision to worry about.
constexpr uint8_t kConfigureCcs[kKnobCount] = {20, 21, 22, 23, 24, 25, 26, 27};

class AkaiMpkMiniMk3 : public ControllerDriver {
public:
    std::vector<std::string> deviceNameHints() const override {
        // "MPK mini 3" is the confirmed ALSA client-name substring (from
        // Zynthian's dev_ids = ["MPK mini 3 IN 1"]; AlsaMidi.cpp composes
        // MidiSource::name as "<client> / <port>", so matching the
        // client-name substring alone is robust to the port suffix). The
        // WinMM szPname string is NOT confirmed in this environment (no
        // Windows MIDI hardware available) -- check with --list-midi on
        // real hardware and extend this list if it differs.
        return {"MPK mini 3", "MPK Mini mk3", "MPK Mini Mk3"};
    }

    const char *driverName() const override { return "akai-mpk-mini-mk3"; }

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure,
             PadFilter &padFilter, CcFilter &ccFilter) override {
        mEngine = &engine;
        mMidiOut = midiOut;
        mAllowConfigure = allowConfigure;
        mPadFilter = &padFilter;
        (void)ccFilter; // this device's function pads are Note-based (see
                        // parsePads()), and its transport buttons are among
                        // them -- no CC-based control needs claiming here

        // No hardcoded factory-CC fallback: common factory defaults vary by
        // firmware/program slot and were never confirmed against real
        // hardware here, and guessing wrong risks hijacking a
        // universally-reserved CC (e.g. CC1 is the mod wheel). If the query
        // below never completes, the knobs simply stay unbound until the
        // user does their own MIDI Learn -- safe degradation over a guess.
        mCurrentMode = 0;
        if (midiOut != nullptr && midiOut->isConnected()) {
            if (allowConfigure) {
                // --controller-driver-configure: rewrite program slots 0-3
                // (Modes 0-3) so their 8 knobs are absolute, on the fixed
                // kConfigureCcs, and named for what Synth One binds them to
                // -- see onConfigureReply(). The sequence ends by re-querying
                // slot 0 the normal way, so the driver comes up bound for
                // actual use exactly like a non-configure run would.
                mConfiguring = true;
                mConfigureSlot = 0;
            }
            sendQuery(0); // program slot 0 = RAM/current
        }
    }

    void onSysEx(const uint8_t *data, size_t length) override {
        if (mEngine == nullptr) return;
        if (static_cast<int>(length) != kWireReplyLength) return;
        if (data[0] != 0xF0 || data[length - 1] != 0xF7) return;

        // Body is data[1..length-2], i.e. manufacturer..transpose.
        const uint8_t *body = data + 1;
        if (body[0] != kManufacturerAkai || body[1] != kDirDeviceToHost ||
            body[2] != kProductMpkMiniMk3 || body[3] != kCmdIncomingData) {
            return; // not a reply to our query
        }

        if (mConfiguring) {
            onConfigureReply(body);
            return;
        }

        parsePads(body, kFullBodyLength);
        parseKnobs(body, kFullBodyLength);
    }

    void onProgramChange(int program) override {
        if (mEngine == nullptr) return;

        // Whatever the previous mode had bound, it no longer applies -- the
        // physical knobs are about to mean something else (or, for an
        // undesigned slot below, nothing at all). Clearing first means a
        // query that never completes leaves the knobs safely unbound rather
        // than silently keeping the old mode's mapping under the new one.
        mEngine->clearDeviceDefaults();
        clearPadClaims();

        // Unlike the knob-target lookup below, pad repurposing has to
        // survive *every* onboard mode switch -- it's a permanent host-side
        // feature, not something that should turn off just because the user
        // flipped to an undesigned program. So a query goes out regardless
        // of whether `program` has a knob-target row; onSysEx()/parsePads()
        // will re-claim the 8 function pads from whatever this program's
        // table actually says, while onSysEx()/parseKnobs() separately
        // bails out (via the mCurrentMode bounds check below) if there's no
        // knob-target row for it.
        mLastProgram = program;
        mCurrentMode = (program >= 0 && program < kModeCount) ? program : -1;
        if (mMidiOut != nullptr && mMidiOut->isConnected()) {
            sendQuery(program);
        }
    }

    std::string modeStatusText() const override {
        if (mCurrentMode < 0 || mCurrentMode >= kModeCount) {
            return "Program " + std::to_string(mLastProgram) + ": knobs unbound (no designed mode)";
        }
        return "Mode " + std::to_string(mCurrentMode) + ": " + kModeNames[mCurrentMode];
    }

    /// Resolves a diverted pad note back to a semantic pad (table index
    /// 0-3 = bottom row PAD1-4, 4-7 = top row PAD5-8 -- see the file header
    /// for the unverified index-to-physical-pad assumption this rests on).
    /// Bottom row: reported outward, since panel-switching needs UiState
    /// this driver doesn't have. Top row: handled here directly via
    /// Engine&, only on the down edge (acting again on release would be
    /// harmless for these particular actions, but doesn't match how a
    /// physical button press is naturally understood).
    PadReport onPadButton(int channel, int note, bool isDown) override {
        (void)channel; // claimed with a wildcard channel -- see parsePads()
        int index = -1;
        for (int i = 0; i < kFunctionPadCount; ++i) {
            if (mPadNote[i] == static_cast<uint8_t>(note)) {
                index = i;
                break;
            }
        }
        if (index < 0) return {}; // shouldn't happen -- PadFilter only ever
                                  // diverts notes this driver itself
                                  // claimed -- but degrade to "ignore"
                                  // rather than assume, e.g. across a
                                  // clear()/re-claim race at a mode-switch
                                  // edge.

        if (index < 4) {
            return PadReport{true, PadRow::Bottom, index};
        }

        if (isDown && mEngine != nullptr) {
            switch (index - 4) {
                case 0: mEngine->panic(); break;
                case 1: mEngine->allNotesOff(); break;
                case 2:
                    mEngine->setParameter(
                        arpIsOn, mEngine->getParameter(arpIsOn) != 0.0f ? 0.0f : 1.0f);
                    break;
                case 3:
                    mEngine->setParameter(
                        arpIsSequencer,
                        mEngine->getParameter(arpIsSequencer) != 0.0f ? 0.0f : 1.0f);
                    break;
                default: break;
            }
        }
        return {};
    }

private:
    void clearPadClaims() {
        if (mPadFilter != nullptr) mPadFilter->clear();
        std::fill(std::begin(mPadNote), std::end(mPadNote), uint8_t{0xFF});
    }


    void sendQuery(int program) {
        const uint8_t msg[] = {0xF0,
                               kManufacturerAkai,
                               kDirHostToDevice,
                               kProductMpkMiniMk3,
                               kCmdQueryData,
                               0x00,
                               0x01,
                               static_cast<uint8_t>(program),
                               0xF7};
        mMidiOut->sendSysEx(msg, sizeof(msg));
    }

    /// Reads the first kFunctionPadCount of the 16 pad-table entries and
    /// claims each note in mPadFilter, so the MIDI reader thread diverts
    /// them away from the note/CC path from this point on. Bails to "claim
    /// nothing" (matching parseKnobs()'s degrade-safely style) if the table
    /// doesn't fit in the reply, or if any note byte is out of MIDI's 0-127
    /// range -- an implausible value the same way parseKnobs() treats a CC
    /// above 127, aborting the whole claim rather than accepting a partial
    /// one built on a wrong offset.
    void parsePads(const uint8_t *body, int bodyLength) {
        clearPadClaims();
        if (mPadFilter == nullptr) return;
        if (kOffPads + kFunctionPadCount * kPadStride > bodyLength) return;

        uint8_t notes[kFunctionPadCount];
        for (int i = 0; i < kFunctionPadCount; ++i) {
            const uint8_t *pad = body + kOffPads + i * kPadStride;
            const uint8_t note = pad[0];
            if (note > 127) return; // implausible -- leave everything unclaimed
            notes[i] = note;
        }
        for (int i = 0; i < kFunctionPadCount; ++i) {
            mPadNote[i] = notes[i];
            mPadFilter->claimNote(notes[i]);
        }
        // No claimChannel() call: the pad channel has no confirmed source in
        // this driver (see the file header), so this relies on PadFilter's
        // default of matching any channel.
    }

    /// Reads the 8 knob blocks from a confirmed-length, confirmed-envelope
    /// reply and seeds Engine's device-default CC map from whatever CC each
    /// one is actually assigned to right now, using the target set for
    /// whichever mode was active when the query that prompted this reply was
    /// sent. Bails without seeding anything if a block doesn't look
    /// plausible (mode > 1 or CC > 127) -- a malformed reply degrades to
    /// "nothing seeded", never a bad mapping.
    void parseKnobs(const uint8_t *body, int bodyLength) {
        if (kOffKnobs + kKnobCount * kKnobStride > bodyLength) return;
        if (mCurrentMode < 0 || mCurrentMode >= kModeCount) return; // no target set for this mode

        int ccs[kKnobCount];
        for (int k = 0; k < kKnobCount; ++k) {
            const uint8_t *knob = body + kOffKnobs + k * kKnobStride;
            const uint8_t mode = knob[0];
            const uint8_t cc = knob[1];
            if (mode > 1 || cc > 127) return; // implausible -- offset guess wrong for this unit
            ccs[k] = cc;
        }
        for (int k = 0; k < kKnobCount; ++k) {
            mEngine->setDeviceDefaultCc(ccs[k], kModeTargets[mCurrentMode][k]);
        }
    }

    /// Builds and sends a CMD_WRITE_DATA envelope with a caller-supplied
    /// 246-byte program body (program-slot..transpose, i.e. `program` itself
    /// is body[0] -- see kBodyLength). Gated behind mAllowConfigure
    /// (--controller-driver-configure) so nothing writes to the device
    /// unless the user explicitly asks for it.
    ///
    /// The envelope here previously duplicated the program-slot byte -- once
    /// as an explicit 8th header byte, again as body[0] -- producing a
    /// 255-byte message where the protocol (and a real device) expects 254,
    /// shifting every byte after it by one. That's why an earlier real-
    /// hardware attempt at this call silently failed: not a device/firmware
    /// limitation, a malformed message the device correctly rejected.
    /// Caught by loading Zynthian's actual shipped driver
    /// (zynthian_ctrldev_akai_mpk_mini_mk3.py) and comparing its
    /// `_restore_mpk_program()` byte-for-byte against this function --
    /// it builds a write by taking a previous *query reply* verbatim and
    /// flipping just the direction and command bytes in place, which is
    /// only consistent if the program-slot number appears exactly once, at
    /// its normal position inside the body. Fixed here to match: the header
    /// is just the fixed 6-byte envelope (manufacturer, direction, product,
    /// command, length), with `program` written into msg[7] (where body[0]
    /// lands) rather than trusted to already be there, so a caller can't
    /// reintroduce the same bug by getting body[0] wrong.
    bool writeProgram(int program, const uint8_t body[kBodyLength]) {
        if (!mAllowConfigure || mMidiOut == nullptr) return false;
        std::vector<uint8_t> msg;
        msg.reserve(1 + 6 + kBodyLength + 1); // F0 + envelope + body + F7 == kWireReplyLength
        msg.insert(msg.end(), {0xF0, kManufacturerAkai, kDirHostToDevice, kProductMpkMiniMk3,
                               kCmdWriteData,
                               static_cast<uint8_t>((kBodyLength >> 7) & 0x7F),
                               static_cast<uint8_t>(kBodyLength & 0x7F)});
        msg.insert(msg.end(), body, body + kBodyLength);
        msg[7] = static_cast<uint8_t>(program); // body[0] is the program-slot field
        msg.push_back(0xF7);
        mMidiOut->sendSysEx(msg.data(), msg.size());
        return true;
    }

    /// One step of the --controller-driver-configure provisioning sequence
    /// kicked off from init(): `body` is the 252-byte manufacturer..
    /// transpose reply for program `mConfigureSlot`. Builds a 246-byte write
    /// body (program-slot..transpose, i.e. body+6) that's byte-identical to
    /// what was just read *except* the 8 knob blocks, which are forced to
    /// absolute mode on the fixed kConfigureCcs and renamed to
    /// kModeTargetNames[mConfigureSlot] -- every other field (pads, MIDI
    /// channels, arp settings, joystick config, program name...) survives
    /// untouched. Writes it, then either moves on to the next mode or, once
    /// slot 3 is done, hands off to a normal slot-0 query so the driver
    /// comes up bound for actual use exactly like a non-configure run would.
    void onConfigureReply(const uint8_t *body) {
        if (mConfigureSlot < 0 || mConfigureSlot >= kModeCount) {
            mConfiguring = false;
            return;
        }

        std::vector<uint8_t> writeBody(body + 6, body + 6 + kBodyLength);
        for (int k = 0; k < kKnobCount; ++k) {
            const int off = (kOffKnobs - 6) + k * kKnobStride;
            writeBody[off + 0] = 0x00; // mode: absolute
            writeBody[off + 1] = kConfigureCcs[k];
            writeBody[off + 2] = 0x00; // min
            writeBody[off + 3] = 0x7F; // max
            const char *name = kModeTargetNames[mConfigureSlot][k];
            std::memset(&writeBody[off + 4], 0, 16);
            std::memcpy(&writeBody[off + 4], name, std::min<size_t>(std::strlen(name), 16));
        }
        writeProgram(mConfigureSlot, writeBody.data());

        // The device's own flash write is not instant, and this sequence
        // only ever runs once, on the control thread, when the user has
        // explicitly opted into rewriting their hardware -- never on the
        // render thread, never during normal operation. A brief pause here
        // is a deliberate, bounded trade-off for that one rare path, not a
        // normal-path stall.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        ++mConfigureSlot;
        if (mConfigureSlot < kModeCount) {
            sendQuery(mConfigureSlot);
        } else {
            mConfiguring = false;
            mCurrentMode = 0;
            sendQuery(0);
        }
    }

    Engine     *mEngine = nullptr;
    MidiOutput *mMidiOut = nullptr;
    bool        mAllowConfigure = false;
    PadFilter  *mPadFilter = nullptr;
    // The onboard program number a pending/completed knob query applies to;
    // indexes kModeTargets. Set before sendQuery() so a reply arriving in
    // onSysEx() knows which target set to seed. See onProgramChange() and
    // parseKnobs(). -1 when the current program has no knob-target row
    // (pads can still be claimed for it; only the knob mapping is unset).
    int mCurrentMode = 0;
    // The raw program number from the most recent onProgramChange(), kept
    // separately from mCurrentMode (which collapses to -1 for anything
    // outside 0..kModeCount-1) purely so modeStatusText() can name an
    // undesigned slot instead of just saying "no mode".
    int mLastProgram = 0;
    // Table index (0..kFunctionPadCount-1) -> the note number currently
    // claimed for it, or 0xFF if none/implausible. Lets onPadButton()
    // resolve a raw note back to a semantic pad without PadFilter itself
    // knowing anything semantic. Parallel array to kOffPads's table, set by
    // parsePads().
    uint8_t mPadNote[kFunctionPadCount] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    // True for the lifetime of the --controller-driver-configure sequence
    // kicked off from init(); routes onSysEx() replies to onConfigureReply()
    // instead of the normal parsePads()/parseKnobs() path. Always false
    // outside that one-time startup sequence -- see init() and
    // onConfigureReply().
    bool mConfiguring = false;
    // Which program slot (0..kModeCount-1) the next configure reply is for;
    // meaningless when mConfiguring is false.
    int mConfigureSlot = 0;
};

} // namespace

std::unique_ptr<ControllerDriver> makeAkaiMpkMiniMk3() {
    return std::make_unique<AkaiMpkMiniMk3>();
}

} // namespace s1::ctrldev
