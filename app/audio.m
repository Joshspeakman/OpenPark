/* Sound output: the emulated Sound Blaster 16 (runtime/sb16.c) through an
 * AVAudioSourceNode, and MPU-401 MIDI bytes through Apple's General MIDI
 * synthesizer (DLSSynth). Volumes come from the launcher settings. */
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include "host.h"

static AVAudioEngine *engine;
static AVAudioUnitMIDIInstrument *synth;
static float sfx_volume = 1.0f;

/* MIDI byte stream -> messages (running status; SysEx passed whole). */
static uint8_t msg[3], status_byte; static int have, need;
static NSMutableData *sysex;
static int data_len(uint8_t s) {
    switch (s & 0xf0) { case 0xc0: case 0xd0: return 1; case 0xf0: return s == 0xf2 ? 2 : s == 0xf1 || s == 0xf3 ? 1 : 0; default: return 2; }
}
void tp_audio_midi(uint8_t b) {
    if (!synth) return;
    if (b >= 0xf8) return;                                  /* real-time messages */
    if (sysex) {
        [sysex appendBytes:&b length:1];
        if (b == 0xf7) { [synth sendMIDISysExEvent:sysex]; sysex = nil; }
        return;
    }
    if (b == 0xf0) { sysex = [NSMutableData dataWithBytes:&b length:1]; return; }
    if (b & 0x80) { status_byte = b; have = 0; need = data_len(b); if (!need) status_byte = 0; return; }
    if (!status_byte) return;
    msg[have++] = b;
    if (have < need) return;
    if (need == 1) [synth sendMIDIEvent:status_byte data1:msg[0]];
    else [synth sendMIDIEvent:status_byte data1:msg[0] data2:msg[1]];
    have = 0;
}

void tp_audio_midi_reset(void) {
    sysex = nil; status_byte = 0; have = 0;
    if (!synth) return;
    for (int ch = 0; ch < 16; ch++) {
        [synth sendMIDIEvent:0xb0 | ch data1:120 data2:0];   /* all sound off */
        [synth sendMIDIEvent:0xb0 | ch data1:123 data2:0];   /* all notes off */
        [synth sendMIDIEvent:0xb0 | ch data1:121 data2:0];   /* reset controllers */
    }
}

void tp_audio_set_volumes(float sfx, float music) {
    sfx_volume = sfx;
    if (synth) synth.volume = music;
}

int tp_audio_start(void) {
    if (engine) return 0;
    engine = [AVAudioEngine new];
    AVAudioFormat *fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:2];
    AVAudioSourceNode *pcm = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:
        ^OSStatus(BOOL *silence, const AudioTimeStamp *ts, AVAudioFrameCount frames, AudioBufferList *out) {
            static float buf[2 * 4096];
            if (frames > 4096) frames = 4096;
            sb16_render(buf, (int)frames, 44100);
            float *l = out->mBuffers[0].mData, *r = out->mNumberBuffers > 1 ? out->mBuffers[1].mData : NULL;
            for (AVAudioFrameCount i = 0; i < frames; i++) {
                l[i] = buf[2 * i] * sfx_volume;
                if (r) r[i] = buf[2 * i + 1] * sfx_volume;
            }
            return noErr;
        }];
    [engine attachNode:pcm];
    [engine connect:pcm to:engine.mainMixerNode format:fmt];
    AudioComponentDescription d = {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth, kAudioUnitManufacturer_Apple, 0, 0};
    synth = [[AVAudioUnitMIDIInstrument alloc] initWithAudioComponentDescription:d];
    [engine attachNode:synth];
    [engine connect:synth to:engine.mainMixerNode format:nil];
    /* The player's own instruments for the General MIDI music (a SoundFont .sf2 or DLS file, launcher > Sound). */
    NSString *bank = NSProcessInfo.processInfo.environment[@"TP_SOUNDBANK"] ?: [NSUserDefaults.standardUserDefaults stringForKey:@"SoundBank"];
    if (bank.length && [NSFileManager.defaultManager fileExistsAtPath:bank]) {
        CFURLRef url = (__bridge CFURLRef)[NSURL fileURLWithPath:bank];
        OSStatus st = AudioUnitSetProperty(synth.audioUnit, kMusicDeviceProperty_SoundBankURL, kAudioUnitScope_Global, 0, &url, sizeof url);
        fprintf(stderr, "audio: instruments %s: %s\n", bank.lastPathComponent.UTF8String, st ? "could not be loaded, built-in used" : "loaded");
    }
    /* Automated tests run the whole path at zero output volume. */
    if (getenv("W32_AUDIO_MUTE") || getenv("TP_MUTE")) engine.mainMixerNode.outputVolume = 0;
    NSError *err = nil;
    if (![engine startAndReturnError:&err]) {
        fprintf(stderr, "audio: %s\n", err.localizedDescription.UTF8String);
        engine = nil; synth = nil;
        return -1;
    }
    tp_host_audio_attached(1);
    fprintf(stderr, "audio: output %.0f Hz, General MIDI synth %s\n", [engine.outputNode outputFormatForBus:0].sampleRate, synth ? "on" : "off");
    return 0;
}

void tp_audio_stop(void) {
    [engine stop];
    engine = nil; synth = nil;
}
