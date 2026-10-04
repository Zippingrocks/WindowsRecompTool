# Windows-first quality gates

Native Windows x64 is the first product target. Other platform ports are
later work. Linux tools, cross-compilation and Wine may help diagnose code,
but their results cannot accept the native Windows game.

## Keep the milestones separate

1. **Build:** a generated, SHA-bound AMD64/PE32+ program compiles and links.
2. **Windows startup:** the actual target executes on native Windows, with
   its observed stops, environment and configuration retained.
3. **Game rendering:** the target loads its own content and renders its scene.
   Authored test scenes validate components, not this milestone.
4. **Playable:** sustained input produces corresponding in-game movement and
   actions. Record duration, scenario, host and remaining defects.
5. **Release-ready:** repeatable startup and play, tested resource lifetime and
   error recovery, responsive controls, appropriate host/driver coverage, and
   visual fidelity suitable for release. A first playable build is not a release.

## Reliability and appearance before promotion

Every change needs a recoverable source commit and evidence identifying that
exact code. Compare original and generated behavior where practical, including
full defined pixel output. Preserve negative tests, previous failures and known
unsupported paths. A selected passing suite is not a passing full integration
run. Main stays separate from an unaccepted development candidate.

Correct geometry, texture coordinates/filtering, depth, transparency, lighting,
effects, animation and frame pacing take priority over optional enhancement.
Do not conceal missing behavior with a visual overhaul, silent API success,
or unimplemented capabilities advertised as available. Native host pointers
must not enter guest memory. A scoped, documented unsupported result is safer
than fabricated behavior.

Report Windows, Wine and Linux observations separately. Keep proprietary game
inputs, assets and game-derived outputs out of hosted CI and Git. A planning
percentage is not a deadline and is not permission to lower an acceptance gate.
