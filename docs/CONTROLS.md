# Controls

All game inputs can be remapped in **Controller Settings** (two keyboard keys
and one controller button/axis per input), with presets, dead zone and
"restore defaults". Controllers are hot-plugged. Menu input is never passed to
the game: while a menu is open the game is paused, and buttons held when it
closes are ignored until released.

## Default keyboard

| Input | Player 1 | Player 2 |
|---|---|---|
| Up / Down / Left / Right | Arrows (or W/S/A/D) | I / , / U / O |
| Button 1 / 2 / 3 / 4 | Z / X / C / V (or J / K / L / ;) | Keypad 1 / 2 / 3 / 4 |
| Start | Enter (or 1) | 2 |
| Coin | 5 | 6 |

Cabinet: Service coin = `9`, Test / Service Mode = `F2`.

Frontend hotkeys (remappable under Controller Settings > Hotkeys): `Esc` quick
menu, `F5` quick save, `F9` quick load, `F6`/`F7` previous/next slot, `F12`
screenshot, `F11` fullscreen, `Backspace` (hold) rewind, `Pause` or `P` pause,
`F10` FPS counter, `F3` reset.

## Default controller (Xbox / PlayStation / Steam Deck / arcade stick)

| Input | Button |
|---|---|
| Move | D-pad or left stick |
| Button 1 / 2 / 3 / 4 | A / B / X / Y (Cross / Circle / Square / Triangle) |
| Start | Start / Menu (Options) |
| Coin | Back / View (Share) |
| Quick menu | Back + Start held together (configurable) |

The buttons are named as MAME names them (Button 1–4); what each does is the
game's own business and is explained by its attract-mode instructions.

## DIP switches (Gameplay Settings)

Gaia has no EEPROM: the operator settings are the board's two DIP switch banks,
offered exactly as MAME 0.264 defines them. The frontend saves your choice.
Changes apply when the game is restarted.

| Switch | Values | Default |
|---|---|---|
| Flip Screen | Off / On | Off |
| Demo Sounds | Off / On | On |
| Language | English / Japanese | English |
| Coinage | 4C1C, 3C1C, 2C1C, 2C1C (1 to continue), 1C1C, 2C3C, 1C2C, 1C3C | 1 Coin / 1 Credit |
| Free Play | Off / On | Off |
| Allow Continue | Off / On | On |
| Lives | 1 / 2 / 3 / 4 | 3 |
| Bonus Life | Off / On (150k–200k first, depending on Difficulty) | On |
| Damage | +0 / +1 / +2 / +3 | +0 |
| Difficulty | Very Easy … Hardest (8 steps) | Medium |

## Service / test mode

Hold `F2` (the board's test switch) to enter the game's test menu.
