// Bounded 48 kHz stereo PCM sink. No guest callbacks or guest-clock advancement.
class EkaAudioProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.capacity = 24000;
        this.pcm = new Int16Array(this.capacity * 2);
        this.read = 0; this.count = 0; this.started = false;
        this.target = options.processorOptions?.prebufferFrames ?? 3840;
        this.underruns = 0; this.dropped = 0; this.played = 0; this.nonzero = 0;
        this.blocks = 0; this.resyncs = 0;
        this.port.onmessage = ({data}) => {
            if (data.reset) { this.read = this.count = 0; this.started = false; return; }
            if (!(data.pcm instanceof ArrayBuffer)) return;
            const input = new Int16Array(data.pcm);
            for (let i=0; i+1<input.length; i+=2) {
                if (this.count === this.capacity) {
                    this.read = (this.read + 1) % this.capacity;
                    --this.count; ++this.dropped;
                }
                const at = (this.read + this.count) % this.capacity;
                this.pcm[at*2] = input[i]; this.pcm[at*2+1] = input[i+1]; ++this.count;
            }
            // A compilation stall can be followed by a burst of guest catch-up.
            // Keeping that backlog would leave sound permanently behind video.
            // Resync only after >200 ms, then resume at the 80 ms target.
            if (this.count > Math.max(this.target, 9600)) {
                const skip = this.count - this.target;
                this.read = (this.read + skip) % this.capacity;
                this.count -= skip; this.dropped += skip; ++this.resyncs;
            }
        };
    }
    process(inputs, outputs) {
        const output = outputs[0];
        if (!output.length) return true;
        if (!this.started && this.count >= this.target) this.started = true;
        for (let i=0; i<output[0].length; ++i) {
            if (this.started && this.count) {
                const left=this.pcm[this.read*2], right=this.pcm[this.read*2+1];
                output[0][i]=left/32768;
                if(output[1]) output[1][i]=right/32768;
                this.nonzero += (left !== 0 || right !== 0) ? 1 : 0;
                this.read=(this.read+1)%this.capacity;--this.count;++this.played;
            } else {
                if(this.started){++this.underruns;this.started=false;}
                for(const channel of output)channel[i]=0;
            }
        }
        if(++this.blocks%128===0)this.port.postMessage({queued:this.count,resyncs:this.resyncs,underruns:this.underruns,dropped:this.dropped,played:this.played,nonzero:this.nonzero});
        return true;
    }
}
registerProcessor('eka-audio', EkaAudioProcessor);
