# MTS-ESP client (ODDSound)

`libMTSClient.cpp` and `libMTSClient.h` from https://github.com/ODDSound/MTS-ESP (`Client/`),
commit f214739b8832e7f297cb9970d0c0efbf783f1462, unchanged. Licence: 0BSD (see `LICENSE`),
compatible with Octavio's AGPLv3.

Octavio 2 uses it for the Intonation parameter's "MTS-ESP" choice: the client looks for the
MTS-ESP library (`libMTS`, installed by an MTS-ESP master such as MTS-ESP Mini) at run time with
`dlopen` / `LoadLibrary`. Nothing is linked at build time; without a master installed or running,
the choice plays equal temperament.
