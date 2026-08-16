# Factory Samples

This folder contains the original Punk Confusion vocal sample set and matching
`VocalSamples.h`.

The WebSerial beta firmware uses the top-level `VocalSamples.h` as its built-in
factory fallback. To restore the checked-in fallback files from the repo root,
run:

```sh
make restore-factory-samples
```

Then rebuild if you need to regenerate the beta UF2s with the original fallback
shouts.
