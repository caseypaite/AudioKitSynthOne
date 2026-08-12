//
//  AkaiMpkMiniMk3.cpp
//  AudioKitSynthOne - Linux / Windows port
//
//  Driver for the Akai MPK Mini mk3. The 16 pads and the keybed need no
//  special handling at all -- they flow through the ordinary
//  MidiQueue -> Engine::handleMidi() path as regular notes, exactly like any
//  other MIDI keyboard, regardless of which MIDI channel the device sends
//  them on. This driver exists only to give the 8 knobs sensible default
//  parameter targets (Engine::setDeviceDefaultCc), discovered by querying the
//  device's own currently-stored program over SysEx.
//
//  SysEx protocol byte layout below is transcribed from Zynthian's shipped
//  driver (zyngine/ctrldev/zynthian_ctrldev_akai_mpk_mini_mk3.py on the
//  vangelis branch of zynthian/zynthian-ui), itself sourced from
//  https://github.com/tsmetana/mpk3-settings/blob/master/src/message.h --
//  a working reference implementation, not a guess. It has not been
//  validated against physical MPK Mini mk3 hardware in this project's
//  development environment; the defensive checks below (exact reply length,
//  command byte, sane mode/CC values) are there so a wrong assumption
//  degrades to "seed nothing" rather than a bad mapping.
//

#include "AkaiMpkMiniMk3.h"

#include <cstring>
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
// Pads live at offset 43 (16 x 3 bytes: note, program-change, CC) but need no
// parsing here -- see the file header comment, they pass through as ordinary
// notes with zero driver code.
constexpr int kOffKnobs   = 91; // 8 knobs x 20 bytes: mode, CC, min, max, name[16]
constexpr int kKnobStride = 20;
constexpr int kKnobCount  = 8;

/// The 8 knob targets. cutoff/resonance (filter), attack/release (amp
/// envelope -- not the independent filter envelope), one modulation rate,
/// two FX sends, and master level: broad coverage across the synth with no
/// redundancy, and none of them collide with universally-reserved CCs (mod
/// wheel CC1, sustain CC64) since these are only ever bound to whatever CC
/// the device's own query reply reports its knobs are actually sending, not
/// a hardcoded guess.
constexpr S1Parameter kKnobTargets[kKnobCount] = {
    cutoff, resonance, attackDuration, releaseDuration,
    lfo1Rate, reverbMix, delayMix, masterVolume,
};

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

    void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure) override {
        mEngine = &engine;
        mMidiOut = midiOut;
        mAllowConfigure = allowConfigure;

        // No hardcoded factory-CC fallback: common factory defaults vary by
        // firmware/program slot and were never confirmed against real
        // hardware here, and guessing wrong risks hijacking a
        // universally-reserved CC (e.g. CC1 is the mod wheel). If the query
        // below never completes, the knobs simply stay unbound until the
        // user does their own MIDI Learn -- safe degradation over a guess.
        if (midiOut != nullptr && midiOut->isConnected()) {
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

        parseKnobs(body, kFullBodyLength);
    }

private:
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

    /// Reads the 8 knob blocks from a confirmed-length, confirmed-envelope
    /// reply and seeds Engine's device-default CC map from whatever CC each
    /// one is actually assigned to right now. Bails without seeding anything
    /// if a block doesn't look plausible (mode > 1 or CC > 127) -- a
    /// malformed reply degrades to "nothing seeded", never a bad mapping.
    void parseKnobs(const uint8_t *body, int bodyLength) {
        if (kOffKnobs + kKnobCount * kKnobStride > bodyLength) return;

        int ccs[kKnobCount];
        for (int k = 0; k < kKnobCount; ++k) {
            const uint8_t *knob = body + kOffKnobs + k * kKnobStride;
            const uint8_t mode = knob[0];
            const uint8_t cc = knob[1];
            if (mode > 1 || cc > 127) return; // implausible -- offset guess wrong for this unit
            ccs[k] = cc;
        }
        for (int k = 0; k < kKnobCount; ++k) {
            mEngine->setDeviceDefaultCc(ccs[k], kKnobTargets[k]);
        }
    }

    /// Builds and sends a CMD_WRITE_DATA envelope with a caller-supplied
    /// 246-byte program body. Exposed for completeness -- NOT called
    /// anywhere in this driver automatically. A write can overwrite the
    /// device's stored program, and while the byte layout above is
    /// transcribed from a working reference implementation rather than
    /// guessed, it is still unverified against real hardware; gated behind
    /// mAllowConfigure (--controller-driver-configure) so nothing writes to
    /// the device unless the user explicitly asks for it. Not currently
    /// invoked even when mAllowConfigure is true -- reserved for a future
    /// pass once the read path above has been validated on real hardware.
    bool writeProgram(int program, const uint8_t body[kBodyLength]) {
        if (!mAllowConfigure || mMidiOut == nullptr) return false;
        std::vector<uint8_t> msg;
        msg.reserve(9 + kBodyLength);
        msg.insert(msg.end(), {0xF0, kManufacturerAkai, kDirHostToDevice, kProductMpkMiniMk3,
                               kCmdWriteData,
                               static_cast<uint8_t>((kBodyLength >> 7) & 0x7F),
                               static_cast<uint8_t>(kBodyLength & 0x7F),
                               static_cast<uint8_t>(program)});
        msg.insert(msg.end(), body, body + kBodyLength);
        msg.push_back(0xF7);
        mMidiOut->sendSysEx(msg.data(), msg.size());
        return true;
    }

    Engine     *mEngine = nullptr;
    MidiOutput *mMidiOut = nullptr;
    bool        mAllowConfigure = false;
};

} // namespace

std::unique_ptr<ControllerDriver> makeAkaiMpkMiniMk3() {
    return std::make_unique<AkaiMpkMiniMk3>();
}

} // namespace s1::ctrldev
