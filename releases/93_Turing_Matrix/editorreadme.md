# Turing Matrix Editor

Standalone Web MIDI editor for the **Turing Matrix** Workshop Computer card.

This browser app changes the card's saved settings over USB MIDI. It does not replace the
front-panel controls.

## What it edits

- Turing layer
  - scale
  - octave range
  - pulse length mode
  - channel 2 loop offset
  - pulse output mode
  - CV output range
- Mixer layer
  - mix curve
  - lane link
  - rise
  - fall
  - lane 1 low/high
  - lane 2 low/high

## Using it

1. Open the editor in Chrome or another Web MIDI browser.
2. Connect the card.
3. Read the current settings from the card.
4. Change the settings in the form.
5. Send the settings back to the card.

## Notes

- Chrome is recommended.
- `Z middle` is Turing mode.
- `Z up` is mixer mode.
- `Z down` remains tap tempo.

## Attribution

The Turing Matrix editor and card build on ideas and code from **Tom Whitwell** and **Chris Johnson**.

## Hosting

The GitHub Pages editor is here:

`https://soveda.github.io/Turing_Matrix_Editor/web`

## Files

- `web/index.html`
- `web/app.js`
- `web/styles.css`
