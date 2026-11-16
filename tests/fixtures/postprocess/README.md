# Deliberately retained post-promotion comparison

These three 16-chunk captures use the unchanged official 1.21.11 binary with
structures disabled and biome feature lists emptied. The chunks have been
promoted to FULL, including vanilla fluid postprocessing. They are a later stage
than CUDA Craft 0.6's native density/surface/carvers output.

Run the diagnostic explicitly:

```powershell
py -3 tests/terrain_reference_test.py build/cuda/terrain-probe.exe --postprocess
```

This currently **returns failure**, correctly: two cells are air in the native
stage output and flowing water (`water[level=1]`, state 87) after vanilla promotion:
seed 1234567890, (12286,22,-8196) and (12285,22,-8195). No expected block was changed
or tolerated to get a pass. The other 4,718,590 states match. Preserve these files
when implementing promotion/fluid ticks; they are not evidence of full parity.

The normal CTest compares pre-promotion CARVERS chunks from the same controlled
worlds, plus a separate noise/surface-only capture. Those comparisons have zero
tolerance and must match every state.
