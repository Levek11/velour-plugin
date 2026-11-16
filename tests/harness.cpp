// Offline test harness: ./harness in.f32 out.f32 sr depth sharp select atk rel lo hi mix delta link
// Files are raw interleaved stereo float32.
#include "../Source/dsp/ResonanceSuppressor.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 14) { std::fprintf (stderr, "bad args\n"); return 1; }
    FILE* f = std::fopen (argv[1], "rb");
    std::vector<float> inter;
    float buf[4096];
    size_t got;
    while ((got = std::fread (buf, sizeof (float), 4096, f)) > 0) inter.insert (inter.end(), buf, buf + got);
    std::fclose (f);

    const double sr = std::atof (argv[3]);
    velour::SuppressorParams p;
    p.depthDb = (float) std::atof (argv[4]);
    p.sharpness = (float) std::atof (argv[5]);
    p.selectivity = (float) std::atof (argv[6]);
    p.attackMs = (float) std::atof (argv[7]);
    p.releaseMs = (float) std::atof (argv[8]);
    p.focusLowHz = (float) std::atof (argv[9]);
    p.focusHighHz = (float) std::atof (argv[10]);
    p.mix = (float) std::atof (argv[11]);
    p.delta = std::atoi (argv[12]) != 0;
    p.stereoLink = std::atoi (argv[13]) != 0;

    velour::ResonanceSuppressor s;
    s.prepare (sr, 2);
    s.setParams (p);

    const size_t frames = inter.size() / 2;
    std::vector<float> L (frames), R (frames);
    for (size_t i = 0; i < frames; ++i) { L[i] = inter[2 * i]; R[i] = inter[2 * i + 1]; }

    // Process in host-like blocks of varying size.
    size_t pos = 0; int blk = 0;
    const int sizes[] = { 512, 128, 1000, 64, 256 };
    while (pos < frames)
    {
        const int n = (int) std::min ((size_t) sizes[blk++ % 5], frames - pos);
        float* ch[2] = { L.data() + pos, R.data() + pos };
        s.process (ch, n);
        pos += (size_t) n;
    }

    for (size_t i = 0; i < frames; ++i) { inter[2 * i] = L[i]; inter[2 * i + 1] = R[i]; }
    FILE* o = std::fopen (argv[2], "wb");
    std::fwrite (inter.data(), sizeof (float), inter.size(), o);
    std::fclose (o);
    std::printf ("latency=%d\n", s.getLatencySamples());
    return 0;
}
