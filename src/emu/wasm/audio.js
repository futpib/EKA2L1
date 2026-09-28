// The browser device is downstream of emulation and recording. Muting never
// changes guest callbacks, timing, or the PCM produced by the emulator.
window.EkaAudio = {
    module: null, pointer: 0, timer: null, context: null, node: null, gain: null,
    muted: true, received: 0, stats: {}, busy: false, generation: 0,
    attach(module) {
        if (this.timer) return;
        this.module = module;
        this.pointer = module._malloc(4096 * 4);
        this.received = 0;
        this.stats = {};
        this.timer = setInterval(() => {
            if (!window._gameRunning) return;
            const frames = module._eka2l1_audio_read(this.pointer, 4096);
            if (!frames) return;
            this.received += frames;
            if (this.node && this.context.state === 'running') {
                // Copy: the shared WASM heap and this scratch region are reused.
                const copy = new Int16Array(frames * 2);
                copy.set(HEAP16.subarray(this.pointer >> 1, (this.pointer >> 1) + frames * 2));
                this.node.port.postMessage({pcm: copy.buffer}, [copy.buffer]);
            }
        }, 10);
        const button = document.getElementById('btn-sound');
        if (button) button.disabled = false;
    },
    async toggle() {
        if (this.busy || !this.timer) return;
        this.busy = true;
        const generation = this.generation;
        const button = document.getElementById('btn-sound');
        if (button) button.disabled = true;
        try {
            if (!this.context) {
                const context = this.context = new AudioContext({sampleRate: 48000, latencyHint: 'interactive'});
                if (context.sampleRate !== 48000) throw Error('48 kHz audio is unavailable');
                // Invoke resume in the gesture handler, before awaiting a load.
                const resumed = context.resume();
                await Promise.all([resumed, context.audioWorklet.addModule('audio-worklet.js')]);
                if (generation !== this.generation) return;
                this.node = new AudioWorkletNode(context, 'eka-audio', {
                    numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2]
                });
                this.gain = context.createGain();
                this.gain.gain.value = 0;
                this.node.connect(this.gain).connect(context.destination);
                this.node.port.onmessage = ({data}) => { this.stats = data; };
                context.onstatechange = () => {
                    if (context.state !== 'running' && this.context === context)
                        this.node?.port.postMessage({reset: true});
                };
            } else {
                await this.context.resume();
                if (generation !== this.generation) return;
            }
            this.muted = !this.muted;
            const now = this.context.currentTime;
            this.gain.gain.cancelScheduledValues(now);
            this.gain.gain.setValueAtTime(this.gain.gain.value, now);
            this.gain.gain.linearRampToValueAtTime(this.muted ? 0 : 1, now + 0.005);
            if (button) {
                button.textContent = this.muted ? 'Enable sound' : 'Mute';
                button.setAttribute('aria-pressed', String(!this.muted));
            }
            document.getElementById('canvas')?.focus();
        } catch (error) {
            if (generation !== this.generation) return;
            this.closeDevice();
            if (button) button.textContent = 'Retry sound';
            this.module.printErr('Audio: ' + error.message);
        } finally {
            if (generation === this.generation) {
                this.busy = false;
                if (button) button.disabled = !this.timer;
            }
        }
    },
    closeDevice() {
        if (this.node) this.node.disconnect();
        if (this.gain) this.gain.disconnect();
        if (this.context && this.context.state !== 'closed') this.context.close().catch(() => {});
        this.context = this.node = this.gain = null;
        this.muted = true;
    },
    stop() {
        ++this.generation;
        this.busy = false;
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
        if (this.pointer && this.module) this.module._free(this.pointer);
        this.pointer = 0;
        this.closeDevice();
        const button = document.getElementById('btn-sound');
        if (button) {
            button.disabled = true;
            button.textContent = 'Enable sound';
            button.setAttribute('aria-pressed', 'false');
        }
    }
};
window.addEventListener('pagehide', () => EkaAudio.stop());
