# Working agreements

Rules that exist because breaking them cost real time. Each one names the
incident that produced it, so it can be argued with on evidence rather than
treated as ceremony.

---

## An imported model is not troubleshot until you have looked at it

**Every model that goes through the PES import must be screenshotted and the
screenshot actually examined before it is called good.** A texture audit, a
mesh count, a face-count histogram and a clean parse are all necessary and none
of them are sufficient. Render it. Look at it.

This rule exists because all four of those checks passed on `dbg_2009`,
`dbg_2014` and `dbg_2023`, and all three models are broken:

| model | player | what the audit said | what it looks like |
|---|---|---|---|
| `dbg_2009` | Master Roshi | 5 meshes, 99 622 faces, every texture resolves | torn in two: torso and arms floating, one leg far below |
| `dbg_2014` | THE HYPEMAN | 1 mesh, 99 314 faces, textures resolve | head and torso separated from the legs |
| `dbg_2023` | GREMLINFLA | 1 mesh, 5 621 faces, textures resolve | collapsed: hair mass, stub arms, no torso or legs |

They were reported as correct on the reasoning that their export ships no face
mesh - only `face_diff.bin`, so they are PES edit-face players - and that
falling back to a generic body is therefore right. That reasoning was sound and
the conclusion was wrong. Nobody had rendered them.

### How

`gfviewer` loads one model and nothing else - no stadium, no crowd, no squad,
no presentation - through the engine's own ASE loader, so what you see is what
the game sees rather than what a second parser thinks:

```bash
cd data
./gfviewer media/players/custom/<model>/fullbody_<model>.ase --shots 1 --out /tmp/m.raw
ffmpeg -f rawvideo -pixel_format rgba -video_size 1280x720 -i /tmp/m.raw /tmp/m_%02d.png
```

It takes the `.ase` directly; it will write the little `.object` wrapper the
loader wants if one is missing. It prints a per-mesh inventory as it goes, which
is worth reading, but the inventory is the hint and the picture is the evidence.

The first four frames are pipeline lead-in and are blank or flat. The last
`--shots` frames are the shots.

### What counts as looking

Disconnected pieces, limbs at the wrong scale, a body that is one flat colour,
geometry that floats away from the root. These are all obvious in a still and
invisible in any statistic the importer produces. A model that renders as a
recognisable, connected figure has passed; anything else is a defect even if
every number about it is healthy.

---

## Claims about visual output need a frame, not an inference

The general form of the rule above. "The camera no longer clips the models",
"the celebration reads correctly", "the kit looks right" are all claims about
pixels. Produce the pixels. An argument from the code, however good, is a
hypothesis - and the codebase has a documented history of good hypotheses that
were wrong: the camera-model collision at the entrance survived a pitch-scale
fix that was correct in principle and insufficient in fact.

State plainly when something could not be verified visually, rather than
softening it into a claim that it works.
