# Known issues

- **Not tested on real hardware or by a person:** Windows 10/11 PCs, the Steam
  Deck, physical controllers and arcade sticks, and actual play-throughs.
  Gameplay was exercised with scripted and random inputs (headless).
- **Loud passages clip** at full scale (0.3 % of samples in the gameplay demo),
  as the raw board mix does; MAME instead applies an automatic compressor by
  default. A frontend limiter could be added as an option.
- **Untranslated instruction types:** Scc, TAS, CHK and TRAPV are modelled
  neither by the translator nor by the interpreter; reaching one stops the game
  with a message. None was reached in any test.
- **TRAP** pushes the address of the TRAP instruction (behaviour inherited from
  the shared 68000 helpers); Gaia does not use TRAP in tested runs.
- **Only gaia** is supported (Thunder Heroes, same board, is refused).
- **Gadgets:** only the two scores are offered; lives/health/timer/stage
  addresses are not verified.
- **CRT curvature** is not offered (SDL_Renderer has no shaders).
