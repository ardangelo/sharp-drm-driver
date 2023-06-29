#ifndef INDICATORS_H_
#define INDICATORS_H_

#define MAX_INDICATORS 6
#define INDICATOR_WIDTH 8
#define INDICATOR_HEIGHT 8
#define INDICATORS_WIDTH (INDICATOR_WIDTH * MAX_INDICATORS)

static u8 const ind_shift[] =
{    0,    0,    0, 0xff, 0xff,    0,    0,    0
,    0,    0, 0xff, 0xff, 0xff, 0xff,    0,    0
,    0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,    0
, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
,    0,    0,    0, 0xff, 0xff,    0,    0,    0
,    0,    0,    0, 0xff, 0xff,    0,    0,    0
,    0,    0,    0, 0xff, 0xff,    0,    0,    0
,    0,    0,    0, 0xff, 0xff,    0,    0,    0
};

static u8 const* indicator_for(char c)
{
	switch (c) {

	case '^': return ind_shift;

	default:
		return NULL;
	}
}

#endif
