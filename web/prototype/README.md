# Design prototype — NOT production code

`EspressoLogger.jsx` is a clickable prototype with **simulated** scale
data and hardcoded mock beans and shots. It exists to fix the visual
language and the interaction flow, not to be wired up as-is.

What to carry forward:

- **The chart-recorder aesthetic.** Salmon measurement grid, ink-blue
  weight trace, olive flow trace, monospace tabular readouts. Light
  background on purpose — this is read at arm's length in a kitchen,
  often with a window behind it.
- **The ghost trace.** The last shot on the same bean, drawn faint and
  dashed behind the live one, so divergence is visible *while it is
  happening*. This is the signature feature.
- **The target box.** Yield range x time range as an intersecting
  rectangle the trace should exit through. Two variables, one glance.
- **The capture flow.** Everything pre-filled, dose inferred and
  correctable, taste as three big chips plus a 1-10 tap. Four taps,
  no keyboard.

Replace all of `BEANS`, `SHOTS`, `PREP` and `synth()`. Real data arrives
over WebSocket from the Go service.
