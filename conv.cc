#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "FFTConvolver.h"
#include "TwoStageFFTConvolver.h"
#include "Utilities.h"

#include "conv.h"

#define HEAD_BLOCK_SZ (64)
#define TAIL_BLOCK_SZ (512)

#define MAX_CONVOLVERS (2)

struct conv {
	fftconvolver::TwoStageFFTConvolver convolver;
};

struct conv* conv_new(float* samples, int n_samples, int stride)
{
	assert(samples != NULL);
	assert(n_samples > 0);
	assert(stride > 0);

	struct conv* c = (struct conv*)malloc(sizeof *c);
	assert(c != NULL);
	memset(c, 0, sizeof *c);

	new(&c->convolver) fftconvolver::TwoStageFFTConvolver;
	float* ir = (float*)calloc(n_samples, sizeof *ir);
	assert(ir != NULL);
	for (int i = 0; i < n_samples; i++) {
		ir[i] = samples[i*stride];
	}
	assert(c->convolver.init(HEAD_BLOCK_SZ, TAIL_BLOCK_SZ, ir, n_samples));
	free(ir);

	return c;
}

void conv_process(struct conv* c, float* output, float* input, int n_samples)
{
	c->convolver.process(input, output, n_samples);
}
