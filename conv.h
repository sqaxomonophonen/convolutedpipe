#ifndef CONV_H

#ifdef __cplusplus
extern "C" {
#endif

struct conv;
struct conv* conv_new(float* samples, int n_samples, int stride);
void conv_process(struct conv* c, float* output, float* input, int n_samples);

#ifdef __cplusplus
}
#endif

#define CONV_H
#endif
