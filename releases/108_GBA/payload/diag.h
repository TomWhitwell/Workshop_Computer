// diag.h — the link-characterisation screens, kept for diagnostics/linkrate.uf2 and
// diagnostics/bandwidth.uf2.
//
// These share this same payload image. They are the instruments that established every number
// in diagnostics/POSTMORTEM.md, and they are how we would re-measure after any regression, so
// they stay even though the applet no longer uses them.
#pragma once

int  diag_active(void);
void diag_frame(void);
