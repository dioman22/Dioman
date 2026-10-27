// The realtime engine, exercised with fake plugins: no SDK, no DAW, no audio device.
// Each test pins one promise the container makes to the host (passthrough, latency
// compensation, click-free bypass, NaN scrubbing, graceful failure).
#include "../source/host/audioops.h"
#include "../source/host/chain.h"
#include "../source/host/slot.h"
#include "test_framework.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace crate;
using namespace crate::host;
#include "fake_plugin.h"

using namespace crate::testing;


CRATE_TEST (slot_is_bit_transparent_when_empty)
{
	Slot slot;
	std::string error;
	CHECK (slot.prepare (stereoSetup (), 2, error));
	CHECK_MSG (error.empty (), error);
	CHECK (slot.isPrepared ());

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.ramp ();
	slot.process (in.block (), out.block ());

	for (int32_t c = 0; c < 2; ++c)
	{
		for (int32_t n = 0; n < kBlock; ++n)
			CHECK_EQ (out.channel (c)[n], in.channel (c)[n]);
	}
	CHECK (!out.block ().silent ());
	CHECK_EQ (slot.latencySamples (), 0);
	CHECK_EQ (slot.tailSamples (), 0);
}

CRATE_TEST (slot_reports_plugin_latency_and_tail)
{
	Slot slot;
	std::string error;
	CHECK (slot.load (makeFake (2, 2, 1.0f, 128, 4096), fakeIdentity (), registry::Status::Ok,
	                  error));
	CHECK (slot.prepare (stereoSetup (), 2, error));
	CHECK_MSG (error.empty (), error);
	CHECK_EQ (slot.latencySamples (), 128);
	CHECK_EQ (slot.tailSamples (), 4096);
	CHECK_EQ (static_cast<int> (slot.status ()), static_cast<int> (registry::Status::Ok));
}

CRATE_TEST (slot_dry_path_is_delayed_by_plugin_latency)
{
	Slot slot;
	std::string error;
	const int32_t latency = 16;
	CHECK (slot.load (makeFake (2, 2, 1.0f, latency), fakeIdentity (), registry::Status::Ok,
	                  error));
	CHECK (slot.prepare (stereoSetup (), 2, error));
	slot.setMix (0.0f); // fully dry: proves the dry path alone is latency compensated
	CHECK_MSG (error.empty (), error);

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.ramp ();
	out.fill (-1.0f, -1.0f);
	slot.process (in.block (), out.block ());

	for (int32_t n = 0; n < kBlock; ++n)
	{
		const float expected = n >= latency ? in.channel (0)[n - latency] : 0.0f;
		CHECK_MSG (std::fabs (out.channel (0)[n] - expected) < 1e-6f,
		           "dry path is not delayed by the reported latency at sample " +
		               std::to_string (n));
	}
}

CRATE_TEST (slot_wet_and_dry_stay_in_phase)
{
	// The fake delays by exactly its reported latency, so wet == dry. A mix of 0.5 must
	// therefore reproduce the delayed input instead of combing it.
	Slot slot;
	std::string error;
	CHECK (slot.load (makeFake (2, 2, 1.0f, 16), fakeIdentity (), registry::Status::Ok, error));
	CHECK (slot.prepare (stereoSetup (), 2, error));
	slot.setMix (0.5f);

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.ramp ();
	slot.process (in.block (), out.block ());

	for (int32_t n = 0; n < kBlock; ++n)
	{
		const float expected = n >= 16 ? in.channel (0)[n - 16] : 0.0f;
		CHECK_MSG (std::fabs (out.channel (0)[n] - expected) < 1e-6f,
		           "wet/dry blend combed at sample " + std::to_string (n));
	}
}

CRATE_TEST (slot_bypass_ramps_instead_of_stepping)
{
	Slot slot;
	std::string error;
	// A plugin that outputs silence makes the ramp visible: wet = 0, dry = input.
	CHECK (slot.load (makeFake (2, 2, 0.0f), fakeIdentity (), registry::Status::Ok, error));
	CHECK (slot.prepare (stereoSetup (), 2, error));
	CHECK_MSG (error.empty (), error);
	slot.setMix (1.0f);

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.fill (1.0f, 1.0f);
	slot.process (in.block (), out.block ());
	CHECK_MSG (std::fabs (out.channel (0)[0]) < 1e-6f, "wet-only slot should be silent");

	slot.setBypass (true);
	slot.process (in.block (), out.block ());

	const float step = 1.0f / static_cast<float> (48000.0 * Slot::kRampSeconds);
	CHECK_MSG (out.channel (0)[0] <= step * 1.5f, "the first sample of the ramp jumped");
	for (int32_t n = 1; n < kBlock; ++n)
	{
		const float delta = out.channel (0)[n] - out.channel (0)[n - 1];
		CHECK_MSG (delta <= step * 1.5f && delta >= 0.0f,
		           "bypass ramp is not monotonic at sample " + std::to_string (n));
	}

	// After the ramp has finished the output is exactly the (dry) input again.
	for (int32_t block = 0; block < 16; ++block)
		slot.process (in.block (), out.block ());
	CHECK_MSG (std::fabs (out.channel (0)[kBlock - 1] - 1.0f) < 1e-5f,
	           "bypass never reached full dry");
}

CRATE_TEST (slot_scrubs_nan_and_inf_from_plugin_output)
{
	Slot slot;
	std::string error;
	CHECK (slot.load (makeFake (2, 2, 1.0f, 0, 0, false, true), fakeIdentity (),
	                  registry::Status::Ok, error));
	CHECK (slot.prepare (stereoSetup (), 2, error));

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.fill (0.5f, 0.5f);
	slot.process (in.block (), out.block ());

	for (int32_t c = 0; c < 2; ++c)
	{
		for (int32_t n = 0; n < kBlock; ++n)
		{
			const float value = out.channel (c)[n];
			CHECK_MSG (value == value, "NaN reached the container output");
			CHECK_MSG (std::fabs (value) < 1e30f, "Inf reached the container output");
		}
	}
}

CRATE_TEST (slot_clamps_mix)
{
	Slot slot;
	slot.setMix (5.0f);
	CHECK_EQ (slot.mix (), 1.0f);
	slot.setMix (-3.0f);
	CHECK_EQ (slot.mix (), 0.0f);
	slot.setMix (0.25f);
	CHECK_EQ (slot.mix (), 0.25f);
}

CRATE_TEST (slot_failure_is_a_passthrough_with_a_status)
{
	Slot slot;
	std::string error;
	CHECK (slot.load (makeFake (2, 2, 1.0f, 0, 0, true), fakeIdentity (), registry::Status::Ok,
	                  error));
	CHECK (!slot.prepare (stereoSetup (), 2, error));
	CHECK (!error.empty ());
	CHECK_EQ (static_cast<int> (slot.status ()),
	          static_cast<int> (registry::Status::Incompatible));
	CHECK (slot.hasInstance ());
	CHECK (!slot.isPrepared ());

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.ramp ();
	slot.process (in.block (), out.block ());
	for (int32_t n = 0; n < kBlock; ++n)
		CHECK_EQ (out.channel (0)[n], in.channel (0)[n]);
}

CRATE_TEST (slot_refuses_to_load_while_prepared)
{
	Slot slot;
	std::string error;
	CHECK (slot.prepare (stereoSetup (), 2, error));
	CHECK (!slot.load (makeFake (2, 2, 1.0f), fakeIdentity (), registry::Status::Ok, error));
	CHECK (!error.empty ());
	slot.release ();
	CHECK (slot.load (makeFake (2, 2, 1.0f), fakeIdentity (), registry::Status::Ok, error));
}

CRATE_TEST (slot_hosts_a_mono_plugin_on_a_stereo_track)
{
	Slot slot;
	std::string error;
	CHECK (slot.load (makeFake (1, 1, 1.0f), fakeIdentity (), registry::Status::Ok, error));
	CHECK (slot.prepare (stereoSetup (), 2, error));
	CHECK_MSG (error.empty (), error);

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.fill (1.0f, 0.0f);
	slot.process (in.block (), out.block ());

	// mono in: 0.5 * (L + R); mono out: the same signal on both sides.
	for (int32_t n = 0; n < kBlock; ++n)
	{
		CHECK_MSG (std::fabs (out.channel (0)[n] - 0.5f) < 1e-6f, "mono downmix is wrong");
		CHECK_MSG (std::fabs (out.channel (1)[n] - 0.5f) < 1e-6f, "mono upmix is wrong");
	}
}

CRATE_TEST (slot_propagates_silence_flags)
{
	Slot slot;
	std::string error;
	CHECK (slot.load (makeFake (2, 2, 1.0f), fakeIdentity (), registry::Status::Ok, error));
	CHECK (slot.prepare (stereoSetup (), 2, error));

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.block ().setSilent (true);
	slot.process (in.block (), out.block ());
	CHECK (out.block ().silent ());
	CHECK (slot.lastOutputSilent ());

	in.block ().setSilent (false);
	slot.process (in.block (), out.block ());
	CHECK (!out.block ().silent ());
	CHECK (!slot.lastOutputSilent ());
}

CRATE_TEST (delay_line_reads_back_delayed_audio)
{
	DelayLine line;
	CHECK (line.resize (1, 48));
	Buffer in (1, 16);
	Buffer out (1, 16);
	for (int32_t n = 0; n < 16; ++n)
		in.channel (0)[n] = static_cast<float> (n + 1);
	line.write (in.block ());
	line.readDelayed (out.block (), 0);

	// Reading with zero delay returns the block just written.
	for (int32_t n = 0; n < 16; ++n)
		CHECK_EQ (out.channel (0)[n], static_cast<float> (n + 1));

	// A block later, the same read is one block older.
	Buffer zeros (1, 16);
	line.write (zeros.block ());
	line.readDelayed (out.block (), 16);
	for (int32_t n = 0; n < 16; ++n)
		CHECK_EQ (out.channel (0)[n], static_cast<float> (n + 1));

	// And with the block in between still counted, it is two blocks older.
	line.readDelayed (out.block (), 32);
	for (int32_t n = 0; n < 16; ++n)
		CHECK_EQ (out.channel (0)[n], 0.0f);
}

CRATE_TEST (channel_mapping_survives_odd_layouts)
{
	Buffer stereo (2, 4);
	Buffer mono (1, 4);
	Buffer quad (4, 4);
	stereo.fill (0.25f, 0.75f);

	mapToPluginInput (stereo.block (), mono.block ());
	CHECK_EQ (mono.channel (0)[0], 0.5f);

	mapFromPluginOutput (mono.block (), stereo.block ());
	CHECK_EQ (stereo.channel (0)[0], 0.5f);
	CHECK_EQ (stereo.channel (1)[0], 0.5f);

	mono.fill (0.5f);
	mapToPluginInput (mono.block (), quad.block ());
	for (int32_t c = 0; c < 4; ++c)
		CHECK_EQ (quad.channel (c)[0], 0.5f);

	Buffer out2 (2, 4);
	mapFromPluginOutput (quad.block (), out2.block ());
	CHECK_EQ (out2.channel (0)[0], 0.5f);
	CHECK_EQ (out2.channel (1)[0], 0.5f);
}
