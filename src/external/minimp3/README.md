minimp3 from https://github.com/lieff/minimp3, commit
`ea99364f61c14656440e8d77e9c233ccf3124633` (CC0; see LICENSE).

Only the low-level decoder header is vendored. EKA2L1 retains incomplete guest
buffers between writes and uses the existing Speex resampler for rate conversion.
