#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Reel.h"
#include "TapeStock.h"

namespace hawthorne
{

//==============================================================================
/**
    Offline tape jobs (message-thread callers run these on a worker thread,
    with the transport stopped):
      - import an audio file onto one or two tracks (optionally printed
        through the reel's tape and machine, resampled to the reel's rate)
      - export a track as a WAV (optionally with the machine's repro sound)
      - flip a reel (reverse tape): every tape file reversed in place, about
        the reel's length, so the start becomes the end.
*/
namespace transfer
{
    using Progress = std::function<bool (double)>;    // return false to cancel

    /** Pulls audio from a reader at another sample rate, one channel at a time. */
    struct ResampledSource
    {
        ResampledSource (juce::AudioFormatReader& r, double outRate)
            : reader (r), ratio (r.sampleRate / outRate), channels ((int) juce::jmin (2u, r.numChannels))
        {
            interp.resize ((size_t) channels);
            inBuf.setSize (channels, 16384 + 512);
        }

        /** Fills out (channels x n). Returns false at the end of the file (rest zeroed). */
        int read (juce::AudioBuffer<float>& out, int n)
        {
            out.clear();
            if (ratio == 1.0)
            {
                const auto left = juce::jmax ((juce::int64) 0, reader.lengthInSamples - readPos);
                const int got = (int) juce::jmin ((juce::int64) n, left);
                if (got > 0) reader.read (&out, 0, got, readPos, true, channels > 1);
                readPos += got;
                return got;
            }

            int produced = 0;
            while (produced < n)
            {
                // keep enough input for the next piece
                const int want = juce::jmin (4096, n - produced);
                const int need = (int) std::ceil (want * ratio) + 8;
                if (inCount < need)
                {
                    const auto left = juce::jmax ((juce::int64) 0, reader.lengthInSamples - readPos);
                    const int get = (int) juce::jmin ((juce::int64) (inBuf.getNumSamples() - inCount), left);
                    if (get > 0)
                    {
                        juce::AudioBuffer<float> tmp (channels, get);
                        reader.read (&tmp, 0, get, readPos, true, channels > 1);
                        for (int c = 0; c < channels; ++c) inBuf.copyFrom (c, inCount, tmp, c, 0, get);
                        inCount += get;
                        readPos += get;
                    }
                    else if (inCount == 0 || drained)
                        return produced;
                    else
                    {
                        // pad the tail so the interpolator flushes
                        const int pad = juce::jmin (need - inCount, inBuf.getNumSamples() - inCount);
                        for (int c = 0; c < channels; ++c) inBuf.clear (c, inCount, pad);
                        inCount += pad;
                        drained = true;
                    }
                }
                int used = 0;
                for (int c = 0; c < channels; ++c)
                    used = interp[(size_t) c].process (ratio, inBuf.getReadPointer (c), out.getWritePointer (c, produced), want);
                used = juce::jmin (used, inCount);
                for (int c = 0; c < channels; ++c)
                    std::memmove (inBuf.getWritePointer (c), inBuf.getReadPointer (c) + used, sizeof (float) * (size_t) (inCount - used));
                inCount -= used;
                produced += want;
                if (drained && inCount <= 0) return produced;
            }
            return produced;
        }

        juce::int64 outputLength() const { return (juce::int64) std::ceil ((double) reader.lengthInSamples / ratio); }

        juce::AudioFormatReader& reader;
        const double ratio;
        const int channels;
        std::vector<juce::WindowedSincInterpolator> interp;
        juce::AudioBuffer<float> inBuf;
        int inCount = 0;
        juce::int64 readPos = 0;
        bool drained = false;
    };

    /** Import: 1 tape file = mono (stereo summed), 2 = left / right. */
    inline juce::String importFile (const juce::File& src, const juce::Array<juce::File>& tapes, double reelRate,
                                    juce::int64 at, const tape::Shared* print, const Progress& progress)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (src));
        if (r == nullptr) return "Can't read " + src.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3).";
        if (r->lengthInSamples <= 0) return src.getFileName() + " is empty.";

        std::vector<std::unique_ptr<TapeFile>> out;
        for (auto& f : tapes)
        {
            juce::String err;
            if (! f.existsAsFile() && ! TapeFile::createEmpty (f, reelRate, err)) return err;
            auto t = std::make_unique<TapeFile>();
            if (! t->open (f, reelRate, err)) return err;
            if (! t->seek (juce::jmax ((juce::int64) 0, at))) return "Could not write " + f.getFileName();
            out.push_back (std::move (t));
        }

        ResampledSource source (*r, reelRate);
        const int outs = (int) out.size();
        std::vector<tape::RecordChannel> rec ((size_t) outs);
        for (int i = 0; i < outs; ++i) rec[(size_t) i].reset (0xC0FFEEu + (uint32_t) i);
        const int skip = print != nullptr ? tape::HalfBand::latency : 0;   // the print's delay, taken off
        int toSkip = skip;

        const int chunk = 8192;
        juce::AudioBuffer<float> buf (2, chunk), mono (outs, chunk + skip);
        const auto total = source.outputLength();
        juce::int64 done = 0;
        bool finished = false;
        int tail = skip;

        while (! finished)
        {
            int got = source.read (buf, chunk);
            got = (int) juce::jmin ((juce::int64) got, juce::jmax ((juce::int64) 0, total + skip - done));   // the resampler pads its last piece
            if (got < chunk)
            {
                // flush the print's delay with silence
                const int extra = juce::jmin (chunk - got, tail);
                buf.clear (0, got, chunk - got);
                if (buf.getNumChannels() > 1) buf.clear (1, got, chunk - got);
                got += extra;
                tail -= extra;
                if (tail <= 0) finished = true;
            }
            if (got <= 0) break;

            for (int i = 0; i < outs; ++i)
            {
                float* m = mono.getWritePointer (i);
                if (outs == 1)
                {
                    if (source.channels > 1)
                        for (int k = 0; k < got; ++k) m[k] = 0.5f * (buf.getSample (0, k) + buf.getSample (1, k));
                    else
                        juce::FloatVectorOperations::copy (m, buf.getReadPointer (0), got);
                }
                else
                    juce::FloatVectorOperations::copy (m, buf.getReadPointer (juce::jmin (i, source.channels - 1)), got);

                if (print != nullptr)
                    rec[(size_t) i].process (*print, m, m, got);
            }

            const int s = juce::jmin (toSkip, got);
            toSkip -= s;
            for (int i = 0; i < outs; ++i)
                if (got - s > 0 && ! out[(size_t) i]->write (mono.getReadPointer (i) + s, got - s))
                    return "Writing to tape failed (disk full?)";

            done += got;
            if (progress && ! progress (juce::jlimit (0.0, 1.0, (double) done / (double) juce::jmax ((juce::int64) 1, total))))
                break;
        }

        for (auto& t : out)
            if (! t->close()) return "Writing to tape failed.";
        return {};
    }

    /** Export one tape file (padded to `length`) as a 24-bit WAV. */
    inline juce::String exportTrack (const juce::File& tapeFile, const juce::File& dest, double rate, juce::int64 length,
                                     const tape::Shared* repro, const Progress& progress)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (tapeFile.existsAsFile() ? fm.createReaderFor (tapeFile) : nullptr);

        auto tmp = dest.getSiblingFile (dest.getFileNameWithoutExtension() + ".partial.wav");
        tmp.deleteFile();
        std::unique_ptr<juce::OutputStream> os (tmp.createOutputStream().release());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (os != nullptr ? wav.createWriterFor (os.get(), rate, 1, 24, {}, 0) : nullptr);
        if (w == nullptr) return "Could not create " + dest.getFullPathName();
        os.release();

        tape::PlaybackChannel play;
        play.reset (0xBEEFu);
        juce::AudioBuffer<float> buf (1, 65536);
        for (juce::int64 pos = 0; pos < length;)
        {
            const int n = (int) juce::jmin ((juce::int64) buf.getNumSamples(), length - pos);
            buf.clear();
            if (r != nullptr && pos < r->lengthInSamples)
            {
                float* d = buf.getWritePointer (0);
                r->read (&d, 1, pos, (int) juce::jmin ((juce::int64) n, r->lengthInSamples - pos));
            }
            if (repro != nullptr) play.process (*repro, buf.getWritePointer (0), n);
            if (! w->writeFromAudioSampleBuffer (buf, 0, n)) { w.reset(); tmp.deleteFile(); return "Write failed (disk full?)"; }
            pos += n;
            if (progress && ! progress ((double) pos / (double) length)) { w.reset(); tmp.deleteFile(); return {}; }
        }
        w.reset();
        dest.deleteFile();
        if (! tmp.moveFileTo (dest)) return "Could not write " + dest.getFullPathName();
        return {};
    }

    /** Reverse a tape file about `length` samples (padded with silence first), via a temp file. */
    inline juce::String reverseFile (const juce::File& f, double rate, juce::int64 length, const Progress& progress)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        auto tmp = f.getSiblingFile (f.getFileNameWithoutExtension() + ".flip.wav");
        tmp.deleteFile();
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return "Can't read " + f.getFileName();
            juce::String err;
            if (! TapeFile::createEmpty (tmp, rate, err)) return err;
            TapeFile out;
            if (! out.open (tmp, rate, err)) return err;

            const int chunk = 65536;
            juce::HeapBlock<float> a ((size_t) chunk), b ((size_t) chunk);
            for (juce::int64 done = 0; done < length;)
            {
                // output [done, done+n) = input [length-done-n, length-done) reversed
                const int n = (int) juce::jmin ((juce::int64) chunk, length - done);
                const auto from = length - done - n;
                juce::FloatVectorOperations::clear (a.get(), n);
                if (from < r->lengthInSamples)
                {
                    float* d = a.get();
                    r->read (&d, 1, from, (int) juce::jmin ((juce::int64) n, r->lengthInSamples - from));
                }
                for (int k = 0; k < n; ++k) b[k] = a[n - 1 - k];
                if (! out.write (b.get(), n)) { out.close(); tmp.deleteFile(); return "Writing failed (disk full?)"; }
                done += n;
                if (progress) progress ((double) done / (double) length);
            }
            if (! out.close()) { tmp.deleteFile(); return "Writing failed."; }
        }
        if (! f.deleteFile() || ! tmp.moveFileTo (f)) return "Could not replace " + f.getFileName();
        return {};
    }
}

} // namespace hawthorne
