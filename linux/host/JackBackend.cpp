//
//  JackBackend.cpp
//  AudioKitSynthOne - Linux port
//
//  JACK output. Works against a real JACK server and against pipewire-jack,
//  which is what Arch installs by default.
//

#include "AudioBackend.h"

#include <jack/jack.h>

#include <cstring>

namespace s1 {

class JackBackend : public AudioBackend {
public:
    ~JackBackend() override {
        stop();
        if (mClient != nullptr) {
            jack_client_close(mClient);
            mClient = nullptr;
        }
    }

    bool open(double requestedRate, uint32_t requestedFrames, double /*latency*/,
              std::string &error) override {
        // JACK dictates both the period and the latency; --latency is a no-op here.
        // JACK dictates both; the requests are advisory only.
        (void)requestedRate;
        (void)requestedFrames;

        jack_status_t status;
        mClient = jack_client_open("SynthOne", JackNoStartServer, &status);
        if (mClient == nullptr) {
            error = "cannot connect to a JACK server";
            if (status & JackServerFailed) error += " (server not running)";
            return false;
        }

        mLeft = jack_port_register(mClient, "out_L", JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsOutput, 0);
        mRight = jack_port_register(mClient, "out_R", JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsOutput, 0);
        if (mLeft == nullptr || mRight == nullptr) {
            error = "cannot register JACK output ports";
            return false;
        }

        mSampleRate = jack_get_sample_rate(mClient);
        mBufferFrames = jack_get_buffer_size(mClient);
        return true;
    }

    bool start(RenderCallback render, std::string &error) override {
        mRender = std::move(render);

        if (jack_set_process_callback(mClient, &JackBackend::processTrampoline, this) != 0) {
            error = "cannot install the JACK process callback";
            return false;
        }
        if (jack_activate(mClient) != 0) {
            error = "cannot activate the JACK client";
            return false;
        }
        mActive = true;

        // Auto-connect to the system playback ports when they exist; harmless
        // if the user would rather patch it up by hand.
        if (const char **ports = jack_get_ports(mClient, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                                JackPortIsPhysical | JackPortIsInput)) {
            if (ports[0] != nullptr) {
                jack_connect(mClient, jack_port_name(mLeft), ports[0]);
                mConnected = ports[0];
            }
            if (ports[1] != nullptr) {
                jack_connect(mClient, jack_port_name(mRight), ports[1]);
                mConnected += std::string(", ") + ports[1];
            }
            jack_free(ports);
        }
        return true;
    }

    void stop() override {
        if (mActive) {
            jack_deactivate(mClient);
            mActive = false;
        }
    }

    const char *name() const override { return "jack"; }
    double sampleRate() const override { return mSampleRate; }
    uint32_t bufferFrames() const override { return mBufferFrames; }

    std::string description() const override {
        return mConnected.empty() ? std::string("not connected (patch SynthOne:out_L/R yourself)")
                                  : ("-> " + mConnected);
    }

private:
    static int processTrampoline(jack_nframes_t frames, void *arg) {
        return static_cast<JackBackend *>(arg)->process(frames);
    }

    int process(jack_nframes_t frames) {
        auto *left = static_cast<float *>(jack_port_get_buffer(mLeft, frames));
        auto *right = static_cast<float *>(jack_port_get_buffer(mRight, frames));
        if (left == nullptr || right == nullptr) return 0;

        if (mRender) {
            mRender(left, right, frames);
        } else {
            std::memset(left, 0, frames * sizeof(float));
            std::memset(right, 0, frames * sizeof(float));
        }
        return 0;
    }

    jack_client_t *mClient = nullptr;
    jack_port_t   *mLeft = nullptr;
    jack_port_t   *mRight = nullptr;
    RenderCallback mRender;
    double         mSampleRate = 48000.0;
    uint32_t       mBufferFrames = 256;
    bool           mActive = false;
    std::string    mConnected;
};

std::unique_ptr<AudioBackend> makeJackBackend() {
    return std::make_unique<JackBackend>();
}

/// JACK has nothing to choose between: the server owns the hardware and decides
/// the rate and buffer size, and where our ports go is a patchbay question, not
/// a device one. One entry keeps the picker honest rather than pretending
/// otherwise.
std::vector<OutputDevice> jackOutputDevices() {
    OutputDevice device;
    device.index = kAutoDevice;
    device.name = "JACK server";
    device.hostApi = "JACK";
    device.maxChannels = 2;
    device.isDefault = true;
    return {device};
}

} // namespace s1
