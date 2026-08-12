//
//  AlsaMidi.cpp
//  AudioKitSynthOne - Linux port
//
//  The Linux implementation of MidiInput (see MidiInput.h). WinMidi.cpp is the
//  Windows counterpart.
//

#include "MidiInput.h"

#include <alsa/asoundlib.h>

#include <cstdio>
#include <cstring>
#include <poll.h>

namespace s1 {

MidiInput::~MidiInput() {
    stop();
    if (mSeq != nullptr) {
        snd_seq_close(static_cast<snd_seq_t *>(mSeq));
        mSeq = nullptr;
    }
}

bool MidiInput::open(const std::string &clientName, std::string &error) {
    snd_seq_t *seq = nullptr;
    const int rc = snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0);
    if (rc < 0) {
        error = std::string("cannot open ALSA sequencer: ") + snd_strerror(rc);
        return false;
    }
    snd_seq_set_client_name(seq, clientName.c_str());

    mPort = snd_seq_create_simple_port(
        seq, "in",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_SYNTH);
    if (mPort < 0) {
        error = std::string("cannot create ALSA port: ") + snd_strerror(mPort);
        snd_seq_close(seq);
        return false;
    }

    mSeq = seq;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d:%d", snd_seq_client_id(seq), mPort);
    mPortName = buf;
    return true;
}

bool MidiInput::connect(const std::string &spec, std::string &error) {
    if (spec.empty() || mSeq == nullptr) return true;

    snd_seq_t *seq = static_cast<snd_seq_t *>(mSeq);
    const std::vector<MidiSource> sources = listSources();

    auto subscribe = [&](int client, int port) -> bool {
        snd_seq_addr_t sender{static_cast<unsigned char>(client), static_cast<unsigned char>(port)};
        snd_seq_addr_t dest{static_cast<unsigned char>(snd_seq_client_id(seq)),
                            static_cast<unsigned char>(mPort)};
        snd_seq_port_subscribe_t *sub = nullptr;
        snd_seq_port_subscribe_alloca(&sub);
        snd_seq_port_subscribe_set_sender(sub, &sender);
        snd_seq_port_subscribe_set_dest(sub, &dest);
        const int rc = snd_seq_subscribe_port(seq, sub);
        if (rc < 0) {
            error = std::string("cannot connect to ") + std::to_string(client) + ":" +
                    std::to_string(port) + ": " + snd_strerror(rc);
            return false;
        }
        for (const auto &source : sources) {
            if (source.client == client && source.port == port) {
                mConnected.push_back(source);
                break;
            }
        }
        return true;
    };

    if (spec == "all") {
        bool any = false;
        for (const auto &source : sources) {
            std::string ignored;
            if (subscribe(source.client, source.port)) any = true;
        }
        if (!any) {
            error = "no connectable MIDI sources found";
            return false;
        }
        return true;
    }

    const size_t colon = spec.find(':');
    if (colon == std::string::npos) {
        error = "MIDI port should be given as CLIENT:PORT (or 'all')";
        return false;
    }
    try {
        return subscribe(std::stoi(spec.substr(0, colon)), std::stoi(spec.substr(colon + 1)));
    } catch (...) {
        error = "malformed MIDI port '" + spec + "'";
        return false;
    }
}

std::vector<MidiSource> MidiInput::listSources() {
    std::vector<MidiSource> sources;

    snd_seq_t *seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return sources;

    snd_seq_client_info_t *clientInfo = nullptr;
    snd_seq_port_info_t *portInfo = nullptr;
    snd_seq_client_info_alloca(&clientInfo);
    snd_seq_port_info_alloca(&portInfo);

    snd_seq_client_info_set_client(clientInfo, -1);
    while (snd_seq_query_next_client(seq, clientInfo) >= 0) {
        const int client = snd_seq_client_info_get_client(clientInfo);
        if (client == SND_SEQ_CLIENT_SYSTEM) continue;

        snd_seq_port_info_set_client(portInfo, client);
        snd_seq_port_info_set_port(portInfo, -1);
        while (snd_seq_query_next_port(seq, portInfo) >= 0) {
            const unsigned caps = snd_seq_port_info_get_capability(portInfo);
            const unsigned needed = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
            if ((caps & needed) != needed) continue;

            MidiSource source;
            source.client = client;
            source.port = snd_seq_port_info_get_port(portInfo);
            source.id = std::to_string(source.client) + ":" + std::to_string(source.port);
            source.name = std::string(snd_seq_client_info_get_name(clientInfo)) + " / " +
                          snd_seq_port_info_get_name(portInfo);
            sources.push_back(source);
        }
    }

    snd_seq_close(seq);
    return sources;
}

void MidiInput::start(MidiQueue *queue, SysExQueue *sysexQueue,
                      ProgramChangeQueue *programChangeQueue) {
    if (mSeq == nullptr || mRunning.load()) return;
    mQueue = queue;
    mSysExQueue = sysexQueue;
    mProgramChangeQueue = programChangeQueue;
    mRunning.store(true);
    mThread = std::thread(&MidiInput::run, this);
}

void MidiInput::stop() {
    if (!mRunning.exchange(false)) return;
    if (mThread.joinable()) mThread.join();
}

void MidiInput::run() {
    snd_seq_t *seq = static_cast<snd_seq_t *>(mSeq);

    const int pollCount = snd_seq_poll_descriptors_count(seq, POLLIN);
    std::vector<struct pollfd> fds(static_cast<size_t>(pollCount));
    snd_seq_poll_descriptors(seq, fds.data(), static_cast<unsigned>(pollCount), POLLIN);

    while (mRunning.load(std::memory_order_relaxed)) {
        // Short timeout so stop() is observed promptly.
        if (poll(fds.data(), static_cast<nfds_t>(pollCount), 100) <= 0) continue;

        snd_seq_event_t *event = nullptr;
        while (snd_seq_event_input(seq, &event) >= 0 && event != nullptr) {
            MidiMessage m;
            const uint8_t channel = static_cast<uint8_t>(event->data.note.channel & 0x0F);

            switch (event->type) {
                case SND_SEQ_EVENT_NOTEON:
                    m.length = 3;
                    m.data[0] = static_cast<uint8_t>(0x90 | channel);
                    m.data[1] = static_cast<uint8_t>(event->data.note.note);
                    m.data[2] = static_cast<uint8_t>(event->data.note.velocity);
                    break;
                case SND_SEQ_EVENT_NOTEOFF:
                    m.length = 3;
                    m.data[0] = static_cast<uint8_t>(0x80 | channel);
                    m.data[1] = static_cast<uint8_t>(event->data.note.note);
                    m.data[2] = static_cast<uint8_t>(event->data.note.velocity);
                    break;
                case SND_SEQ_EVENT_CONTROLLER:
                    m.length = 3;
                    m.data[0] = static_cast<uint8_t>(0xB0 |
                                (event->data.control.channel & 0x0F));
                    m.data[1] = static_cast<uint8_t>(event->data.control.param & 0x7F);
                    m.data[2] = static_cast<uint8_t>(event->data.control.value & 0x7F);
                    break;
                case SND_SEQ_EVENT_PITCHBEND: {
                    // ALSA reports pitch bend centred on zero; MIDI wants 0..16383.
                    const int value = event->data.control.value + 8192;
                    m.length = 3;
                    m.data[0] = static_cast<uint8_t>(0xE0 |
                                (event->data.control.channel & 0x0F));
                    m.data[1] = static_cast<uint8_t>(value & 0x7F);
                    m.data[2] = static_cast<uint8_t>((value >> 7) & 0x7F);
                    break;
                }
                case SND_SEQ_EVENT_SYSEX: {
                    const auto *bytes = static_cast<const uint8_t *>(event->data.ext.ptr);
                    const unsigned len = event->data.ext.len;
                    SysExMessage sysex;
                    // append() copies synchronously into the assembler's own
                    // buffer before snd_seq_free_event() below invalidates
                    // event->data.ext.ptr.
                    if (bytes != nullptr && mSysExAssembler.append(bytes, len, sysex)) {
                        sysex.sourceId = event->source.client;
                        if (mSysExQueue != nullptr) mSysExQueue->push(sysex);
                    }
                    break;
                }
                case SND_SEQ_EVENT_PGMCHANGE: {
                    // Not forwarded to the note/CC path -- Program Change was
                    // previously dropped entirely on Linux (no case here at
                    // all), and stays that way; this only feeds a controller
                    // driver's mode-switch detection (see
                    // ControllerDriverManager::dispatchProgramChange).
                    if (mProgramChangeQueue != nullptr) {
                        ProgramChangeMessage pc;
                        pc.channel = event->data.control.channel & 0x0F;
                        pc.program = event->data.control.value & 0x7F;
                        pc.sourceId = event->source.client;
                        mProgramChangeQueue->push(pc);
                    }
                    break;
                }
                default:
                    break;
            }

            if (m.length > 0 && mQueue != nullptr) {
                mQueue->push(m);
            }
            snd_seq_free_event(event);

            if (snd_seq_event_input_pending(seq, 0) == 0) break;
        }
    }
}

} // namespace s1
