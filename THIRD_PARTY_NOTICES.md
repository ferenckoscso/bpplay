# Third-party notices

bpplay itself is licensed under GPL-3.0-or-later (see [`LICENSE`](LICENSE)). It vendors two
third-party single-header decoders:

## dr_flac (FLAC decoding)

- **Project**: [dr_libs](https://github.com/mackron/dr_libs) — `dr_flac.h`, v0.13.4
- **Author**: David Reid
- **License**: available as a choice of Public Domain (Unlicense) or MIT No Attribution

```
This software is available as a choice of the following licenses. Choose
whichever you prefer.

===============================================================================
ALTERNATIVE 1 - Public Domain (www.unlicense.org)
===============================================================================
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

===============================================================================
ALTERNATIVE 2 - MIT No Attribution
===============================================================================
Copyright 2023 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Reproduced verbatim from the license statement at the end of `src/dr_flac.h` as vendored in this
repository (dr_libs, https://github.com/mackron/dr_libs).

## ALAC (Apple Lossless decoding)

- **Project**: Apple's own open-sourced ALAC reference decoder, taken from the pure-C fork
  [mikebrady/alac](https://github.com/mikebrady/alac) (originally
  [macosforge/alac](https://github.com/macosforge/alac))
- **Copyright**: (c) 2011 Apple Inc. All rights reserved.
- **License**: Apache License, Version 2.0
- **Vendored at**: `src/alac.h` — the codec math (adaptive Golomb-Rice coding, dynamic
  predictor, channel matrixing, bit buffer, endian swap) is Apple's C source, essentially
  verbatim. The one C++ file in that project (`ALACDecoder.cpp`, a thin orchestration class) was
  ported to plain C by hand — struct instead of class, functions instead of methods, same control
  flow and numeric computation. See the file header of `src/alac.h` for the full account of what
  was changed and why.

```
Apache License

Version 2.0, January 2004

http://www.apache.org/licenses/

TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

1. Definitions.

"License" shall mean the terms and conditions for use, reproduction, and distribution as
defined by Sections 1 through 9 of this document.

"Licensor" shall mean the copyright owner or entity authorized by the copyright owner that is
granting the License.

"Legal Entity" shall mean the union of the acting entity and all other entities that control,
are controlled by, or are under common control with that entity. For the purposes of this
definition, "control" means (i) the power, direct or indirect, to cause the direction or
management of such entity, whether by contract or otherwise, or (ii) ownership of fifty percent
(50%) or more of the outstanding shares, or (iii) beneficial ownership of such entity.

"You" (or "Your") shall mean an individual or Legal Entity exercising permissions granted by
this License.

"Source" form shall mean the preferred form for making modifications, including but not limited
to software source code, documentation source, and configuration files.

"Object" form shall mean any form resulting from mechanical transformation or translation of a
Source form, including but not limited to compiled object code, generated documentation, and
conversions to other media types.

"Work" shall mean the work of authorship, whether in Source or Object form, made available under
the License, as indicated by a copyright notice that is included in or attached to the work (an
example is provided in the Appendix below).

"Derivative Works" shall mean any work, whether in Source or Object form, that is based on (or
derived from) the Work and for which the editorial revisions, annotations, elaborations, or
other modifications represent, as a whole, an original work of authorship. For the purposes of
this License, Derivative Works shall not include works that remain separable from, or merely
link (or bind by name) to the interfaces of, the Work and Derivative Works thereof.

"Contribution" shall mean any work of authorship, including the original version of the Work and
any modifications or additions to that Work or Derivative Works thereof, that is intentionally
submitted to Licensor for inclusion in the Work by the copyright owner or by an individual or
Legal Entity authorized to submit on behalf of the copyright owner. For the purposes of this
definition, "submitted" means any form of electronic, verbal, or written communication sent to
the Licensor or its representatives, including but not limited to communication on electronic
mailing lists, source code control systems, and issue tracking systems that are managed by, or
on behalf of, the Licensor for the purpose of discussing and improving the Work, but excluding
communication that is conspicuously marked or otherwise designated in writing by the copyright
owner as "Not a Contribution."

"Contributor" shall mean Licensor and any individual or Legal Entity on behalf of whom a
Contribution has been received by Licensor and subsequently incorporated within the Work.

2. Grant of Copyright License. Subject to the terms and conditions of this License, each
Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
irrevocable copyright license to reproduce, prepare Derivative Works of, publicly display,
publicly perform, sublicense, and distribute the Work and such Derivative Works in Source or
Object form.

3. Grant of Patent License. Subject to the terms and conditions of this License, each
Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
irrevocable (except as stated in this section) patent license to make, have made, use, offer to
sell, sell, import, and otherwise transfer the Work, where such license applies only to those
patent claims licensable by such Contributor that are necessarily infringed by their
Contribution(s) alone or by combination of their Contribution(s) with the Work to which such
Contribution(s) was submitted. If You institute patent litigation against any entity (including
a cross-claim or counterclaim in a lawsuit) alleging that the Work or a Contribution incorporated
within the Work constitutes direct or contributory patent infringement, then any patent licenses
granted to You under this License for that Work shall terminate as of the date such litigation
is filed.

4. Redistribution. You may reproduce and distribute copies of the Work or Derivative Works
thereof in any medium, with or without modifications, and in Source or Object form, provided
that You meet the following conditions: (a) You must give any other recipients of the Work or
Derivative Works a copy of this License; (b) You must cause any modified files to carry
prominent notices stating that You changed the files; (c) You must retain, in the Source form of
any Derivative Works that You distribute, all copyright, patent, trademark, and attribution
notices from the Source form of the Work, excluding those notices that do not pertain to any
part of the Derivative Works; (d) If the Work includes a "NOTICE" text file as part of its
distribution, then any Derivative Works that You distribute must include a readable copy of the
attribution notices contained within such NOTICE file, excluding those notices that do not
pertain to any part of the Derivative Works, in at least one of the following places: within a
NOTICE text file distributed as part of the Derivative Works; within the Source form or
documentation, if provided along with the Derivative Works; or, within a display generated by
the Derivative Works, if and wherever such third-party notices normally appear.

5. Submission of Contributions. Unless You explicitly state otherwise, any Contribution
intentionally submitted for inclusion in the Work by You to the Licensor shall be under the
terms and conditions of this License, without any additional terms or conditions.

6. Trademarks. This License does not grant permission to use the trade names, trademarks,
service marks, or product names of the Licensor, except as required for reasonable and customary
use in describing the origin of the Work and reproducing the content of the NOTICE file.

7. Disclaimer of Warranty. Unless required by applicable law or agreed to in writing, Licensor
provides the Work (and each Contributor provides its Contributions) on an "AS IS" BASIS, WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied, including, without limitation,
any warranties or conditions of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
PARTICULAR PURPOSE.

8. Limitation of Liability. In no event and under no legal theory, whether in tort (including
negligence), contract, or otherwise, unless required by applicable law or agreed to in writing,
shall any Contributor be liable to You for damages, including any direct, indirect, special,
incidental, or consequential damages of any character arising as a result of this License or out
of the use or inability to use the Work, even if such Contributor has been advised of the
possibility of such damages.

9. Accepting Warranty or Additional Liability. While redistributing the Work or Derivative Works
thereof, You may choose to offer, and charge a fee for, acceptance of support, warranty,
indemnity, or other liability obligations and/or rights consistent with this License.
```

Full text also at <http://www.apache.org/licenses/LICENSE-2.0>.
