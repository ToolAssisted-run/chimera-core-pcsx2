/* Where the PS2's sound goes when there is no sound card.
 *
 * PCSX2's SPU2 mixes into an AudioStream, which on a desktop is a ring buffer
 * a device thread drains at its own pace. That "at its own pace" is exactly
 * what a core must not do: a frame's samples belong to that frame, and a movie
 * that replays elsewhere cannot depend on when someone's audio device happened
 * to ask.
 *
 * So this is an AudioStream whose device is the frontend. It is the same ring
 * buffer as upstream's - the mixer, the expansion and the stretcher above it
 * are PCSX2's own, unchanged - but nothing drains it in the background: the
 * frame does, through the ABI, taking exactly what accumulated while the frame
 * ran.
 *
 * It arrives as the "cubeb" backend because that is SPU2's default and the
 * factory for it is ours to write (a sandbox has no cubeb). Any backend a
 * project names lands here.
 */
#include "Host/AudioStream.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
	class ChimeraAudioStream final : public AudioStream
	{
	public:
		ChimeraAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
			: AudioStream(sample_rate, parameters)
		{
		}

		void Start() { BaseInitialize(&StereoSampleReaderImpl, false); }

		/* Everything buffered, up to what the caller has room for. */
		u32 Pull(SampleType* out, u32 max_frames)
		{
			const u32 frames = std::min(GetBufferedFramesRelaxed(), max_frames);
			if (frames > 0)
				ReadFrames(out, frames);
			return frames;
		}
	};

	ChimeraAudioStream* s_stream = nullptr;
} // namespace

std::unique_ptr<AudioStream> AudioStream::CreateCubebAudioStream(u32 sample_rate,
	const AudioStreamParameters& parameters, const char* driver_name, const char* device_name,
	bool stretch_enabled, Error* error)
{
	auto stream = std::make_unique<ChimeraAudioStream>(sample_rate, parameters);
	stream->Start();
	s_stream = stream.get();
	return stream;
}

std::unique_ptr<AudioStream> AudioStream::CreateSDLAudioStream(u32 sample_rate,
	const AudioStreamParameters& parameters, bool stretch_enabled, Error* error)
{
	return CreateCubebAudioStream(sample_rate, parameters, nullptr, nullptr, stretch_enabled, error);
}

std::vector<std::pair<std::string, std::string>> AudioStream::GetCubebDriverNames() { return {}; }
std::vector<AudioStream::DeviceInfo> AudioStream::GetCubebOutputDevices(const char* driver) { return {}; }

/* The frame's samples, as the signed 16-bit stereo pairs the ABI carries.
 * SPU2 works in floats; the conversion is here rather than in the frontend so
 * that both flavors of this core produce the same bytes.
 */
extern "C" int ChimeraAudioPull(int16_t* out, int max_frames)
{
	if (!s_stream || max_frames <= 0)
		return 0;

	static std::vector<float> scratch;
	scratch.resize(static_cast<size_t>(max_frames) * 2);

	const u32 frames = s_stream->Pull(scratch.data(), static_cast<u32>(max_frames));
	for (u32 i = 0; i < frames * 2; i++)
	{
		const float sample = std::clamp(scratch[i], -1.0f, 1.0f);
		out[i] = static_cast<int16_t>(sample * 32767.0f);
	}
	return static_cast<int>(frames);
}
