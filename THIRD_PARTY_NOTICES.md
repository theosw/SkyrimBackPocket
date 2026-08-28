# Third-party notices

Back Pocket links against CommonLibSSE-NG and uses SKSE/SkyUI interoperability APIs.

The initial CommonLib build, logging, menu-event, inventory-entry callback, Scaleform function
handler, focus guard, and serialization patterns were informed by the local Pick Up As Junk project,
which is MIT licensed. Back Pocket is an independent implementation and retains the MIT license.
Its native item-menu footer capacity hook, input-family presentation, SkyUI gamepad key mapping,
and controller release/coalescing policy are adapted from Pick Up As Junk's MIT-licensed source.
The small SWF writer used to generate Back Pocket's original pouch artwork is adapted from that
project's MIT-licensed inventory-state icon generator.

SkyUI's official ActionScript source was consulted to understand the public behavior of
`FilteredEnumeration.addFilter`, `IFilter.applyFilter`, and `ScrollingList.requestInvalidate`.
Back Pocket does not distribute or replace SkyUI ActionScript assets.

## Pick Up As Junk

MIT License

Copyright (c) 2026 Pick Up As Junk contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
