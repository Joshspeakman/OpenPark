/* Sound output: the emulated Sound Blaster 16 (runtime/sb16.c) through an
 * AVAudioSourceNode, and MPU-401 MIDI bytes through Apple's General MIDI
 * synthesizer (DLSSynth). Volumes come from the launcher settings. */
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include "host.h"

static AVAudioEngine *engine;
static AVAudioUnitMIDIInstrument *synth;
static float sfx_volume = 1.0f, music_volume = 1.0f;
/* CD-audio music (Launcher > Sound > Music Type): a track imported from the owner's Amiga CD32 disc
 * (tools/extras.py) plays in place of the game's General MIDI tune, which goes silent meanwhile. */
static AVAudioPlayerNode *cd;
static int cd_playing;

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
    sfx_volume = sfx; music_volume = music;
    if (synth) synth.volume = cd_playing ? 0 : music;
    if (cd) cd.volume = music;
}

/* Play a WAV track (loop: until stopped or replaced); the MIDI synth is muted while it plays. */
int tp_audio_cd_play(const char *path, int loop) {
    if (!engine || !cd) return -1;
    NSError *err = nil;
    AVAudioFile *f = [[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:@(path)] error:&err];
    if (!f) { fprintf(stderr, "audio: %s: %s\n", path, err.localizedDescription.UTF8String); return -1; }
    AVAudioPCMBuffer *b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:f.processingFormat frameCapacity:(AVAudioFrameCount)f.length];
    if (![f readIntoBuffer:b error:&err]) return -1;
    [cd stop];
    if (![[cd outputFormatForBus:0] isEqual:b.format]) {
        [engine disconnectNodeOutput:cd];
        [engine connect:cd to:engine.mainMixerNode format:b.format];
    }
    [cd scheduleBuffer:b atTime:nil options:loop ? AVAudioPlayerNodeBufferLoops : 0 completionHandler:nil];
    cd.volume = music_volume;
    [cd play];
    if (!cd_playing) { cd_playing = 1; if (synth) synth.volume = 0; }   /* (the game's MIDI keeps running underneath, unheard) */
    return 0;
}
void tp_audio_cd_stop(void) {
    if (cd) [cd stop];
    if (cd_playing) { cd_playing = 0; if (synth) synth.volume = music_volume; }
}
int tp_audio_cd_busy(void) { return cd && cd.isPlaying; }

int tp_audio_start(void) {
    if (engine) return 0;
    engine = [AVAudioEngine new];
    AVAudioFormat *fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:2];
    AVAudioSourceNode *pcm = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:
        ^OSStatus(BOOL *silence, const AudioTimeStamp *ts, AVAudioFrameCount frames, AudioBufferList *out) {
            static float buf[2 * 4096];
            if (frames > 4096) frames = 4096;
            sb16_play(buf, (int)frames);   /* (made in the game's time by sb16_pump) */
            static FILE *rec; static int rec_on = -1;              /* TP_AUDIO_REC=<file> (development): the card's output, 44.1 kHz stereo float */
            if (rec_on < 0) { const char *p = getenv("TP_AUDIO_REC"); rec = p ? fopen(p, "wb") : NULL; rec_on = rec != NULL; }
            if (rec_on) { fwrite(buf, sizeof(float), 2 * frames, rec); fflush(rec); }
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
    cd = [AVAudioPlayerNode new];
    [engine attachNode:cd];
    [engine connect:cd to:engine.mainMixerNode format:fmt];
    /* The player's own instruments for the General MIDI music (a SoundFont .sf2 or DLS file, launcher > Sound). */
    NSString *bank = NSProcessInfo.processInfo.environment[@"TP_SOUNDBANK"] ?: [NSUserDefaults.standardUserDefaults stringForKey:@"SoundBank"];
    if (bank.length && [NSFileManager.defaultManager fileExistsAtPath:bank]) {
        CFURLRef url = (__bridge CFURLRef)[NSURL fileURLWithPath:bank];
        OSStatus st = AudioUnitSetProperty(synth.audioUnit, kMusicDeviceProperty_SoundBankURL, kAudioUnitScope_Global, 0, &url, sizeof url);
        fprintf(stderr, "audio: instruments %s: %s\n", bank.lastPathComponent.UTF8String, st ? "could not be loaded, built-in used" : "loaded");
    }
    /* Everything (effects and music) through a look-ahead peak limiter on its way out: the effects come from
     * sb16.c in floating point with their level restored (they start at a quarter in the driver, qol.c), and
     * effects and music together can pass full scale; the limiter holds the sum under it instead of the
     * output clipping. TP_SOUND_RAW=1: no limiter. TP_AUDIO_REC_OUT=<file>: what goes out (development). */
    if (!getenv("TP_SOUND_RAW")) {
        AudioComponentDescription ld = {kAudioUnitType_Effect, kAudioUnitSubType_PeakLimiter, kAudioUnitManufacturer_Apple, 0, 0};
        AVAudioUnitEffect *limiter = [[AVAudioUnitEffect alloc] initWithAudioComponentDescription:ld];
        [engine attachNode:limiter];
        AVAudioFormat *of = [engine.mainMixerNode outputFormatForBus:0];
        [engine connect:engine.mainMixerNode to:limiter format:of];
        [engine connect:limiter to:engine.outputNode format:of];
        AudioUnitSetParameter(limiter.audioUnit, kLimiterParam_AttackTime, kAudioUnitScope_Global, 0, 0.005, 0);
        AudioUnitSetParameter(limiter.audioUnit, kLimiterParam_DecayTime, kAudioUnitScope_Global, 0, 0.08, 0);
        const char *ro = getenv("TP_AUDIO_REC_OUT");
        if (ro) { FILE *f = fopen(ro, "wb");
            if (f) [limiter installTapOnBus:0 bufferSize:4096 format:nil block:^(AVAudioPCMBuffer *b, AVAudioTime *when) {
                for (AVAudioFrameCount i = 0; i < b.frameLength; i++) for (AVAudioChannelCount c = 0; c < 2 && c < b.format.channelCount; c++)
                    fwrite(&b.floatChannelData[c][i], 4, 1, f);
                fflush(f); }]; }
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
    engine = nil; synth = nil; cd = nil; cd_playing = 0;
}
