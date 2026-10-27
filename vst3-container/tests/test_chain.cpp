// Chain-level promises: bit-transparent when empty, serial order, summed latency, and
// a failing slot that degrades to pass-through instead of taking the session with it.
#include "../source/host/chain.h"
#include "fake_plugin.h"
#include "test_framework.h"

#include <cmath>

using namespace crate;
using namespace crate::host;
using namespace crate::testing;

CRATE_TEST (chain_is_bit_transparent_when_empty)
{
	Chain chain;
	chain.setSlotCount (4);
	std::string error;
	CHECK (chain.prepare (stereoSetup (), error));
	CHECK_MSG (error.empty (), error);
	CHECK_EQ (chain.loadedSlots (), 0);

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.ramp ();
	chain.process (in.block (), out.block ());

	for (int32_t c = 0; c < 2; ++c)
	{
		for (int32_t n = 0; n < kBlock; ++n)
			CHECK_EQ (out.channel (c)[n], in.channel (c)[n]);
	}
	CHECK_EQ (chain.latencySamples (), 0);
	CHECK_EQ (chain.tailSamples (), 0);
}

CRATE_TEST (chain_runs_slots_in_series)
{
	Chain chain;
	chain.setSlotCount (2);
	std::string error;
	CHECK (chain.slot (0).load (makeFake (2, 2, 2.0f), fakeIdentity ("Two"), registry::Status::Ok,
	                         error));
	CHECK (chain.slot (1).load (makeFake (2, 2, 3.0f), fakeIdentity ("Three"),
	                           registry::Status::Ok, error));
	CHECK (chain.prepare (stereoSetup (), error));
	CHECK_MSG (error.empty (), error);
	CHECK_EQ (chain.loadedSlots (), 2);

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.fill (0.25f, -0.25f);
	chain.process (in.block (), out.block ());

	for (int32_t n = 0; n < kBlock; ++n)
	{
		CHECK_MSG (std::fabs (out.channel (0)[n] - 1.5f) < 1e-6f, "serial order is wrong");
		CHECK_MSG (std::fabs (out.channel (1)[n] + 1.5f) < 1e-6f, "serial order is wrong");
	}
}

CRATE_TEST (chain_sums_latency_and_tail_of_loaded_slots)
{
	Chain chain;
	chain.setSlotCount (3);
	std::string error;
	CHECK (chain.slot (0).load (makeFake (2, 2, 1.0f, 32, 1000), fakeIdentity ("A"),
	                         registry::Status::Ok, error));
	CHECK (chain.slot (1).load (makeFake (2, 2, 1.0f, 100, 2000), fakeIdentity ("B"),
	                           registry::Status::Ok, error));
	// slot 2 stays empty and must not change either number
	CHECK (chain.prepare (stereoSetup (), error));
	CHECK_MSG (error.empty (), error);
	CHECK_EQ (chain.latencySamples (), 132);
	CHECK_EQ (chain.tailSamples (), 3000);
}

CRATE_TEST (chain_degrades_a_failing_slot_to_pass_through)
{
	Chain chain;
	chain.setSlotCount (2);
	std::string error;
	CHECK (chain.slot (0).load (makeFake (2, 2, 2.0f), fakeIdentity ("Good"),
	                         registry::Status::Ok, error));
	CHECK (chain.slot (1).load (makeFake (2, 2, 3.0f, 0, 0, true), fakeIdentity ("Bad"),
	                           registry::Status::Ok, error));
	CHECK (chain.prepare (stereoSetup (), error));

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.fill (0.25f, 0.25f);
	chain.process (in.block (), out.block ());

	for (int32_t n = 0; n < kBlock; ++n)
		CHECK_MSG (std::fabs (out.channel (0)[n] - 0.5f) < 1e-6f,
		           "a failed slot must pass the signal through untouched");
	CHECK_EQ (chain.latencySamples (), 0); // the failed plugin contributes no latency
	CHECK_EQ (chain.loadedSlots (), 2);
}

CRATE_TEST (chain_can_be_released_and_prepared_again)
{
	Chain chain;
	chain.setSlotCount (1);
	std::string error;
	CHECK (chain.slot (0).load (makeFake (2, 2, 2.0f), fakeIdentity (), registry::Status::Ok,
	                         error));
	CHECK (chain.prepare (stereoSetup (), error));

	Buffer in (2, kBlock);
	Buffer out (2, kBlock);
	in.fill (0.5f, 0.5f);
	chain.process (in.block (), out.block ());
	CHECK_MSG (std::fabs (out.channel (0)[0] - 1.0f) < 1e-6f, "gain was not applied");

	chain.release ();
	CHECK (!chain.isPrepared ());
	// After release the chain is an honest pass-through rather than a stale buffer.
	Buffer fresh (2, kBlock);
	fresh.fill (-0.75f, 0.125f);
	chain.process (in.block (), fresh.block ());
	CHECK_EQ (fresh.channel (0)[0], 0.5f);

	CHECK (chain.prepare (ProcessSetup{44100.0, 128, 2}, error));
	CHECK_MSG (error.empty (), error);
	Buffer in2 (2, 128);
	Buffer out2 (2, 128);
	in2.fill (0.5f, 0.5f);
	chain.process (in2.block (), out2.block ());
	CHECK_MSG (std::fabs (out2.channel (0)[0] - 1.0f) < 1e-6f,
	           "re-prepared chain lost its plugin");
}

CRATE_TEST (chain_never_returns_stale_samples_past_the_prepared_block)
{
	Chain chain;
	chain.setSlotCount (1);
	std::string error;
	CHECK (chain.slot (0).load (makeFake (2, 2, 1.0f), fakeIdentity (), registry::Status::Ok,
	                         error));
	CHECK (chain.prepare (stereoSetup (48000.0, 64), error));

	Buffer in (2, 128);
	Buffer out (2, 128);
	in.fill (1.0f, 1.0f);
	out.fill (99.0f, 99.0f);
	chain.process (in.block (), out.block ());

	CHECK_MSG (std::fabs (out.channel (0)[63] - 1.0f) < 1e-6f, "prepared part is wrong");
	CHECK_EQ (out.channel (0)[64], 0.0f); // beyond maxBlockSize: silence, not garbage
	CHECK_EQ (out.channel (0)[127], 0.0f);
}
