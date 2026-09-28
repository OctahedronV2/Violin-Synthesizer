# PFFFT

A pretty fast FFT by Julien Pommier, used for the measured-body convolution
(`source/dsp/PartitionedConvolution.cpp`, docs/PHASE7.md step 7.3).

- Files: `pffft.c` and `pffft.h`, unmodified.
- Source: https://github.com/marton78/pffft at commit `3673ac0` (2019-12-22), the
  last version before that fork split the library into many files. It adds
  64-bit ARM (Apple silicon) and MSVC fixes to Pommier's original.
- Licence: the FFTPACKv5 licence (BSD-like), in the header of `pffft.c`.
  Copyright (c) 2013 Julien Pommier; (c) 2004 the University Corporation for
  Atmospheric Research ("UCAR").
