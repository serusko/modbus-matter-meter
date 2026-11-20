#pragma once

// Default is 8. Each poll can dirty up to 10 attributes (total power + V/I/P on 3 phases);
// when the pool overflows the reporting engine merges paths into whole clusters/endpoints
// and re-sends ~14 kB per cycle over Thread.
#define CHIP_IM_SERVER_MAX_NUM_DIRTY_SET 24
