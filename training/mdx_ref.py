"""Reference MDX-Net (UVR) inference in numpy/onnxruntime. Mirrors audio-separator's demix (no overlap,
trim-based), used to validate the C++ port and to measure quality."""
import numpy as np, onnxruntime as ort

MODELS = {
 'Kim_Vocal_2': dict(n_fft=7680, dim_f=3072, dim_t=8, compensate=1.009, primary='Vocals'),
 'UVR-MDX-NET-Voc_FT': dict(n_fft=7680, dim_f=3072, dim_t=8, compensate=1.021, primary='Vocals'),
 'UVR_MDXNET_KARA_2': dict(n_fft=5120, dim_f=2048, dim_t=8, compensate=1.065, primary='Instrumental'),
 'Reverb_HQ_By_FoxJoy': dict(n_fft=6144, dim_f=3072, dim_t=9, compensate=1.035, primary='Reverb'),
}
HOP = 1024

def hann(n):
    return (0.5 - 0.5*np.cos(2*np.pi*np.arange(n)/n)).astype(np.float32)

def stft(x, n_fft):  # x (C, L) -> (C, F, T) complex; torch.stft(center=True, reflect pad)
    pad = n_fft//2
    xp = np.pad(x, ((0,0),(pad,pad)), mode='reflect')
    T = 1 + (xp.shape[1]-n_fft)//HOP
    w = hann(n_fft)
    idx = np.arange(n_fft)[None,:] + HOP*np.arange(T)[:,None]
    fr = xp[:, idx] * w  # (C,T,n_fft)
    return np.fft.rfft(fr, axis=-1).transpose(0,2,1)

def istft(S, n_fft, length):  # S (C,F,T)
    w = hann(n_fft); C, F, T = S.shape
    fr = np.fft.irfft(S.transpose(0,2,1), n=n_fft, axis=-1) * w
    L = n_fft + HOP*(T-1)
    out = np.zeros((C, L), np.float32); ws = np.zeros(L, np.float32)
    for t in range(T):
        out[:, t*HOP:t*HOP+n_fft] += fr[:, t]
        ws[t*HOP:t*HOP+n_fft] += w*w
    out /= np.maximum(ws, 1e-8)
    pad = n_fft//2
    return out[:, pad:pad+length]

class MDX:
    def __init__(self, path, name=None, threads=2):
        import json, os
        side = os.path.splitext(path)[0] + '.json'
        if name is None and os.path.exists(side):
            j = json.load(open(side)); self.p = dict(n_fft=j['n_fft'], dim_f=j['dim_f'], dim_t=j['dim_t'], compensate=j['compensate'], primary=j['primary'])
        else:
            self.p = MODELS[name or os.path.splitext(os.path.basename(path))[0]]
        so = ort.SessionOptions(); so.intra_op_num_threads = threads
        self.s = ort.InferenceSession(path, so)
    def run_spec(self, x):  # x (2, chunk)
        p = self.p; n_fft = p['n_fft']; dim_f = p['dim_f']
        S = stft(x, n_fft)  # (2,F,T)
        inp = np.stack([S.real, S.imag], 1).reshape(4, S.shape[1], S.shape[2])[:, :dim_f].astype(np.float32)
        inp[:, :3, :] = 0
        out = self.s.run(None, {'input': inp[None]})[0][0]  # (4,dim_f,T)
        full = np.zeros((4, n_fft//2+1, S.shape[2]), np.float32); full[:, :dim_f] = out
        full = full.reshape(2, 2, n_fft//2+1, S.shape[2])
        return istft(full[:,0] + 1j*full[:,1], n_fft, x.shape[1])
    def separate(self, mix, denoise=False):  # mix (2,N) 44.1k -> primary stem (2,N)
        p = self.p; n_fft = p['n_fft']; dim_t = 2**p['dim_t']
        chunk = HOP*(dim_t-1); trim = n_fft//2; gen = chunk - 2*trim
        N = mix.shape[1]; pad = gen - N % gen
        mp = np.concatenate([np.zeros((2,trim)), mix, np.zeros((2,pad)), np.zeros((2,trim))], 1).astype(np.float32)
        outs = []
        for i in range(0, N+pad, gen):
            w = mp[:, i:i+chunk]
            y = self.run_spec(w)
            if denoise:
                y = 0.5*(y - self.run_spec(-w))
            outs.append(y[:, trim:-trim])
        return np.concatenate(outs, 1)[:, :N] * p['compensate']
