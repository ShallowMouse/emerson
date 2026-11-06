#pragma once

// WHITMAN's end of the EMERSON Link, without the Windows plumbing: given the shared
// memory, it runs every block EMERSON has written through a callback and writes the
// answer back. WHITMAN's "EMERSON" audio device calls pump() each time the tick event
// fires. Shared by EMERSON (for its tests) and WHITMAN: keep the two copies identical.

#include "EmersonLink.h"
#include <algorithm>
#include <cstring>

namespace emerson::link
{
    class ClientCore
    {
    public:
        explicit ClientCore (Shared* s = nullptr) : shared (s) {}
        void attach (Shared* s) noexcept { shared = s; readPos = -1; }
        void resync() noexcept          { readPos = -1; }

        /** Processes everything EMERSON has written since last time, in blocks of at most
            maxFrames. in / out: scratch buffers of toWhitmanChannels / fromWhitmanChannels
            channels of maxFrames each. fn (in, out, n) is the mixer. Returns frames done. */
        template <typename Fn>
        int pump (float* const* in, float* const* out, int maxFrames, Fn&& fn)
        {
            if (shared == nullptr || maxFrames <= 0) return 0;
            const auto w = shared->toWrite.load (std::memory_order_acquire);

            // starting, or so far behind that the ring has gone round: pick up from now
            if (readPos < 0 || readPos > w || w - readPos > ringFrames / 2)
            {
                readPos = w;
                shared->fromWrite.store (w, std::memory_order_release);
            }

            int done = 0;
            while (w - readPos > 0)
            {
                const int n = (int) std::min<std::int64_t> (w - readPos, maxFrames);
                const int p = (int) (readPos & ringMask);
                const int first = std::min (n, ringFrames - p);

                for (int c = 0; c < toWhitmanChannels; ++c)
                {
                    std::memcpy (in[c], shared->toWhitman[c] + p, sizeof (float) * (size_t) first);
                    if (first < n) std::memcpy (in[c] + first, shared->toWhitman[c], sizeof (float) * (size_t) (n - first));
                }

                fn (in, out, n);

                for (int c = 0; c < fromWhitmanChannels; ++c)
                {
                    std::memcpy (shared->fromWhitman[c] + p, out[c], sizeof (float) * (size_t) first);
                    if (first < n) std::memcpy (shared->fromWhitman[c], out[c] + first, sizeof (float) * (size_t) (n - first));
                }

                readPos += n;
                shared->fromWrite.store (readPos, std::memory_order_release);
                done += n;
            }
            return done;
        }

    private:
        Shared* shared = nullptr;
        std::int64_t readPos = -1;
    };
}
