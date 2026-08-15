# Factory Samples

This folder contains the original Punk Confusion vocal sample set and matching
`VocalSamples.h`.

The local web builder and custom UF2 scripts replace the files in `samples/`
and regenerate the top-level `VocalSamples.h`. To restore the factory sample
set from the repo root, run:

```sh
make restore-factory-samples
```

Then rebuild if you want a UF2 using the original shouts again.
