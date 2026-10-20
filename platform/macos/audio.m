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
static AVAudioPlayerNode *cd, *vid;
static int cd_playing, vid_playing;                 /* (a 3DO ride video's sound: the music silent meanwhile) */
static void apply_music(void) {
    if (synth) synth.volume = cd_playing || vid_playing ? 0 : music_volume;
    if (cd) cd.volume = vid_playing ? 0 : music_volume;
}

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
    apply_music();
    if (vid) vid.volume = music;
}

/* Play a WAV track (loop: until stopped or replaced); the MIDI synth is muted while it plays. The track is read and
 * decoded on a queue of its own (a whole track: the display's tick, where the music follows the game, waited for
 * it, a hitch at every tune change), then played on the main thread unless another tune or a stop came first. */
static int cd_gen; static dispatch_queue_t cd_queue;
int tp_audio_cd_play(const char *path, int loop) {
    if (!engine || !cd) return -1;
    int gen = ++cd_gen; NSString *p = @(path);
    if (!cd_queue) cd_queue = dispatch_queue_create("theme-park.cd", DISPATCH_QUEUE_SERIAL);
    dispatch_async(cd_queue, ^{
        NSError *err = nil;
        AVAudioFile *f = [[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:p] error:&err];
        if (!f) { fprintf(stderr, "audio: %s: %s\n", p.UTF8String, err.localizedDescription.UTF8String); return; }
        AVAudioPCMBuffer *b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:f.processingFormat frameCapacity:(AVAudioFrameCount)f.length];
        if (![f readIntoBuffer:b error:&err]) return;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (gen != cd_gen || !engine || !cd) return;  /* (superseded, or the sound stopped) */
            [cd stop];
            if (![[cd outputFormatForBus:0] isEqual:b.format]) {
                [engine disconnectNodeOutput:cd];
                [engine connect:cd to:engine.mainMixerNode format:b.format];
            }
            [cd scheduleBuffer:b atTime:nil options:loop ? AVAudioPlayerNodeBufferLoops : 0 completionHandler:nil];
            [cd play];
            cd_playing = 1; apply_music();               /* (the game's MIDI keeps running underneath, unheard) */
        });
    });
    return 0;
}
void tp_audio_cd_stop(void) {
    cd_gen++;
    if (cd) [cd stop];
    cd_playing = 0; apply_music();
}
/* A ride video's sound (a WAV beside the 3DO stream), once; the music is silent until tp_audio_video_stop. */
int tp_audio_video_play(const char *path) {
    if (!engine || !vid) return -1;
    NSError *err = nil;
    AVAudioFile *f = [[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:@(path)] error:&err];
    if (!f) return -1;
    AVAudioPCMBuffer *b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:f.processingFormat frameCapacity:(AVAudioFrameCount)f.length];
    if (![f readIntoBuffer:b error:&err]) return -1;
    [vid stop];
    [engine disconnectNodeOutput:vid];
    [engine connect:vid to:engine.mainMixerNode format:b.format];
    [vid scheduleBuffer:b atTime:nil options:0 completionHandler:nil];
    vid.volume = music_volume;
    [vid play];
    vid_playing = 1; apply_music();
    return 0;
}
void tp_audio_video_stop(void) {
    if (vid) [vid stop];
    vid_playing = 0; apply_music();
}
int tp_audio_cd_busy(void) { return cd && cd.isPlaying; }

/* Drops, in the log (theme-park.log) whatever the settings, once a second at most: output callbacks that came late
 * (the device ran on without the game's sound), the output ring run dry, and game-thread stalls longer than the
 * output's lead (host.c: the card skipped them). Called by the app's tick. */
static volatile long late_callbacks; static volatile double late_worst_ms; static double t0;   /* (the output's start) */
void tp_audio_report(void) {
    static double last; double t = NSProcessInfo.processInfo.systemUptime; if (t - last < 1) return; last = t;
    long sb16_underruns(void), tp_host_card_stalls(double *longest);
    static long dry0; long dry = sb16_underruns(); double st; long stalls = tp_host_card_stalls(&st);
    long late = late_callbacks; double lw = late_worst_ms; late_callbacks = 0; late_worst_ms = 0;
    if (!late && dry == dry0 && !stalls) return;
    fprintf(stderr, "audio drop at %.0f s: late output callbacks %ld (longest %.0f ms), ran dry %ld, game stalls past the lead %ld (longest %.0f ms)\n",
            t - t0, late, lw, dry - dry0, stalls, st * 1e3);
    dry0 = dry;
}
int tp_audio_start(void) {
    if (engine) return 0;
    t0 = NSProcessInfo.processInfo.systemUptime;
    engine = [AVAudioEngine new];
    AVAudioFormat *fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100 channels:2];
    AVAudioSourceNode *pcm = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:
        ^OSStatus(BOOL *silence, const AudioTimeStamp *ts, AVAudioFrameCount frames, AudioBufferList *out) {
            static float buf[2 * 4096];
            {   /* (an output callback that came late: the device played on without us; tp_audio_report) */
                static double next = -1;
                if (ts && (ts->mFlags & kAudioTimeStampSampleTimeValid)) {
                    double gap = next >= 0 ? ts->mSampleTime - next : 0;
                    if (gap > 8) { late_callbacks++; if (gap / 44.1 > late_worst_ms) late_worst_ms = gap / 44.1; }
                    next = ts->mSampleTime + frames;
                }
            }
            static FILE *rec; static int rec_on = -1;              /* TP_AUDIO_REC=<file> (development): the card's output, 44.1 kHz stereo float */
            if (rec_on < 0) { const char *p = getenv("TP_AUDIO_REC"); rec = p ? fopen(p, "wb") : NULL; rec_on = rec != NULL; }
            float *l = out->mBuffers[0].mData, *r = out->mNumberBuffers > 1 ? out->mBuffers[1].mData : NULL;
            for (AVAudioFrameCount done = 0; done < frames; ) {   /* (in pieces of the buffer's size: a device can ask for more) */
                AVAudioFrameCount n = frames - done > 4096 ? 4096 : frames - done;
                sb16_play(buf, (int)n);   /* (made in the game's time by sb16_pump) */
                if (rec_on) { fwrite(buf, sizeof(float), 2 * n, rec); fflush(rec); }
                for (AVAudioFrameCount i = 0; i < n; i++) {
                    l[done + i] = buf[2 * i] * sfx_volume;
                    if (r) r[done + i] = buf[2 * i + 1] * sfx_volume;
                }
                done += n;
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
    vid = [AVAudioPlayerNode new];
    [engine attachNode:vid];
    [engine connect:vid to:engine.mainMixerNode format:fmt];
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
    engine = nil; synth = nil; cd = nil; vid = nil; cd_playing = vid_playing = 0;
}
