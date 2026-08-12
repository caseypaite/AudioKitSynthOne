//
//  AlsaMidiOut.cpp
//  AudioKitSynthOne - Linux port
//
//  The Linux implementation of MidiOutput (see MidiOutput.h). WinMidiOut.cpp
//  is the Windows counterpart.
//

#include "MidiOutput.h"

#include <alsa/asoundlib.h>

#include <cstdio>

namespace s1 {

MidiOutput::~MidiOutput() {
    if (mSeq != nullptr) {
        snd_seq_close(static_cast<snd_seq_t *>(mSeq));
        mSeq = nullptr;
    }
}

bool MidiOutput::open(const std::string &clientName, std::string &error) {
    snd_seq_t *seq = nullptr;
    const int rc = snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0);
    if (rc < 0) {
        error = std::string("cannot open ALSA sequencer: ") + snd_strerror(rc);
        return false;
    }
    snd_seq_set_client_name(seq, clientName.c_str());

    // READ|SUBS_READ, the mirror of AlsaMidi.cpp's input port: this is the
    // capability that makes a port show up as a *source* other clients can
    // subscribe to read from.
    mPort = snd_seq_create_simple_port(
        seq, "out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
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

bool MidiOutput::connect(const std::string &spec, std::string &error) {
    if (spec.empty() || mSeq == nullptr) return true;

    snd_seq_t *seq = static_cast<snd_seq_t *>(mSeq);

    auto subscribe = [&](int client, int port) -> bool {
        snd_seq_addr_t sender{static_cast<unsigned char>(snd_seq_client_id(seq)),
                              static_cast<unsigned char>(mPort)};
        snd_seq_addr_t dest{static_cast<unsigned char>(client), static_cast<unsigned char>(port)};
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
        return true;
    };

    if (spec == "all") {
        bool any = false;
        for (const auto &dest : listDestinations()) {
            if (subscribe(dest.client, dest.port)) any = true;
        }
        if (!any) {
            error = "no connectable MIDI destinations found";
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

std::vector<MidiSource> MidiOutput::listDestinations() {
    std::vector<MidiSource> dests;

    snd_seq_t *seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return dests;

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
            // WRITE|SUBS_WRITE: a port other clients (a hardware synth, a DAW)
            // publish to receive MIDI, the mirror of the filter AlsaMidi.cpp
            // uses to find *sources*.
            const unsigned needed = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
            if ((caps & needed) != needed) continue;

            MidiSource dest;
            dest.client = client;
            dest.port = snd_seq_port_info_get_port(portInfo);
            dest.id = std::to_string(dest.client) + ":" + std::to_string(dest.port);
            dest.name = std::string(snd_seq_client_info_get_name(clientInfo)) + " / " +
                        snd_seq_port_info_get_name(portInfo);
            dests.push_back(dest);
        }
    }

    snd_seq_close(seq);
    return dests;
}

namespace {

void sendNoteEvent(void *seqPtr, int port, snd_seq_event_type_t type, int channel, int note,
                   int velocity) {
    if (seqPtr == nullptr) return;
    snd_seq_t *seq = static_cast<snd_seq_t *>(seqPtr);

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, static_cast<unsigned char>(port));
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    ev.type = type;
    ev.data.note.channel = static_cast<unsigned char>(channel & 0x0F);
    ev.data.note.note = static_cast<unsigned char>(note & 0x7F);
    ev.data.note.velocity = static_cast<unsigned char>(velocity & 0x7F);

    // _direct bypasses the output queue/scheduler entirely -- there is no
    // timestamp to honour here, only "as soon as possible", so draining an
    // explicit queue would add a step for nothing.
    snd_seq_event_output_direct(seq, &ev);
}

} // namespace

void MidiOutput::noteOn(int channel, int note, int velocity) {
    sendNoteEvent(mSeq, mPort, SND_SEQ_EVENT_NOTEON, channel, note, velocity);
}

void MidiOutput::noteOff(int channel, int note, int velocity) {
    sendNoteEvent(mSeq, mPort, SND_SEQ_EVENT_NOTEOFF, channel, note, velocity);
}

} // namespace s1
