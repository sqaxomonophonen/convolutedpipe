#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "opt.h"
#include "conv.h"
#include "miniaudio.h"

#define MAX_CHANNELS (2)
#define DEFAULT_SAMPLE_RATE (48000)
int sample_rate = DEFAULT_SAMPLE_RATE;
char* prg = NULL;

#define INPUT_BUFFER_SZ (65536)
struct {
	int n_input_channels;
	int n_output_channels;
	float* input_buffers[MAX_CHANNELS];
	float* output_buffer;
} audio;

ma_uint32 n_kernel_channels = 0;
struct conv* convs[MAX_CHANNELS];

static void list_devices()
{
	ma_context context;
	if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
		fprintf(stderr, "ma_context_init() failed\n");
		exit(EXIT_FAILURE);
	}

	ma_device_info* playback_infos;
	ma_uint32 n_playback_infos;
	ma_device_info* capture_infos;
	ma_uint32 n_capture_infos;

	if (ma_context_get_devices(&context, &playback_infos, &n_playback_infos, &capture_infos, &n_capture_infos) != MA_SUCCESS) {
		fprintf(stderr, "ma_context_get_devices() failed\n");
		exit(EXIT_FAILURE);
	}

	printf("Audio devices:\n");

	for (int pc = 0; pc < 2; pc++) {
		ma_device_info* infos;
		ma_uint32 n_infos;

		if (pc == 0) {
			infos = playback_infos;
			n_infos = n_playback_infos;
		} else if (pc == 1) {
			infos = capture_infos;
			n_infos = n_capture_infos;
		} else {
			assert(!"UNREACHABLE");
		}

		for (int i = 0; i < n_infos; i++) {
			ma_device_info* info = &infos[i];

			printf("\n");
			printf("  Type:     %s\n", pc == 0 ? "output" : "input");
			printf("  Id:       %d\n", i);
			printf("  Name:     %s\n", info->name);
			printf("  Default:  %s\n", info->isDefault ? "YES" : "no");
		}
	}

	printf("\n");
}

static int init_decoder(ma_decoder* decoder, char* path, int force_n_channels)
{
	assert(path != NULL);

	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, force_n_channels, sample_rate);
	ma_result result = ma_decoder_init_file(path, &config, decoder);
	if (result != MA_SUCCESS) {
		fprintf(stderr, "%s: read/decode error\n", path);
		exit(EXIT_FAILURE);
	}

	int n_channels = decoder->outputChannels;

	if (n_channels != 1 && n_channels != 2) {
		fprintf(stderr, "%s: must have 1 or 2 channels\n", path);
		exit(EXIT_FAILURE);
	}

	return n_channels;
}

static void audio_callback(ma_device* device, void* v_output, const void* v_input, ma_uint32 n_frames)
{
	const float* input = (const float*)v_input;
	float* output = (float*)v_output;

	int remaining = n_frames;

	while (remaining > 0) {
		int n = remaining;
		if (n > INPUT_BUFFER_SZ) n = INPUT_BUFFER_SZ;

		int p = 0;
		for (int i = 0; i < n; i++) {
			for (int j = 0; j < audio.n_input_channels; j++) {
				audio.input_buffers[j][i] = input[p++];
			}
		}

		for (int i = 0; i < audio.n_output_channels; i++) {
			conv_process(convs[i], audio.output_buffer, audio.input_buffers[i % audio.n_input_channels], n);
			for (int j = 0; j < n; j++) {
				output[j * audio.n_output_channels + i] = audio.output_buffer[j];
			}
		}

		remaining -= n;
		input = &input[p];
	}
}

static void usage(FILE* out, int status)
{
	//            01234567890123456789012345678901234567890123456789012345678901234567890123456789
	fprintf(out, "Usage: %s <kernel> [options...]\n", prg);
	fprintf(out, " or\n");
	fprintf(out, "Usage: %s <kernel> <in-file> <out-file> [options...]\n", prg);
	fprintf(out, "\n");
	fprintf(out, "Options:\n");
	fprintf(out, "   -h, --help                        Help (you're looking at it)\n");
	fprintf(out, "   -L, --list-devices                List available audio devices\n");
	fprintf(out, "   -S, --sample-rate   <rate>        Sample rate (default: %d)\n", DEFAULT_SAMPLE_RATE);
	fprintf(out, "\n");

	list_devices();

	exit(status);
}

static void usage_ok()
{
	usage(stdout, EXIT_SUCCESS);
}

static void usage_err()
{
	usage(stderr, EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	prg = argv[0];

	struct OptDef defs[] = {
		{OPT_FLAG,   'h', "help"},
		{OPT_FLAG,   'L', "list-devices"},
		{OPT_OPTION, 'S', "sample-rate"},
		{0},
	};

	struct Opt opt;
	opt_init(&opt, defs, argc-1, argv+1);

	char* kernel_path = NULL;
	char* in_path = NULL;
	char* out_path = NULL;

	while (opt_next(&opt)) {
		if (opt.is_invalid) {
			fprintf(stderr, "invalid option %s\n\n", opt.arg);
			usage_err();
		}

		if (opt.is_flag) {
			if (opt.short_opt == 'h') {
				usage_ok();
			} else if (opt.short_opt == 'L') {
				list_devices();
				exit(EXIT_SUCCESS);
			} else {
				assert(!"UNREACHABLE");
			}
		} else if (opt.is_option) {
			if (opt.short_opt == 'S') {
				sample_rate = atoi(opt.value);
				assert(sample_rate >= 12000 && sample_rate <= 200000);
			} else {
				assert(!"UNREACHABLE");
			}
		} else if (opt.is_npos) {
			if (kernel_path == NULL) {
				kernel_path = opt.value;
			} else if (in_path == NULL) {
				in_path = opt.value;
			} else if (out_path == NULL) {
				out_path = opt.value;
			} else {
				fprintf(stderr, "additional non-positional arguments not expected\n\n");
				usage_err();
			}
		} else {
			assert(!"UNREACHABLE");
		}
	}

	if (kernel_path == NULL) usage_err();

	{
		float* kernel_frames = NULL;

		ma_decoder decoder;
		n_kernel_channels = init_decoder(&decoder, kernel_path, 0);

		ma_uint64 n_kernel_frames = ma_decoder_get_length_in_pcm_frames(&decoder);
		if (n_kernel_frames == 0) {
			fprintf(stderr, "%s: failed to read length\n", kernel_path);
			exit(EXIT_FAILURE);
		}

		printf("Kernel: %s; %d channel%s; %.1f seconds (%llu frames)\n",
			kernel_path,
			n_kernel_channels,
			n_kernel_channels > 1 ? "s" : "",
			(float)n_kernel_frames/(float)sample_rate,
			n_kernel_frames);

		ma_uint64 n_samples = n_kernel_channels * n_kernel_frames;

		assert((kernel_frames = calloc(n_samples, sizeof *kernel_frames )) != NULL);

		ma_uint64 n_frames_read = ma_decoder_read_pcm_frames(&decoder, kernel_frames, n_kernel_frames);
		if (n_frames_read != n_kernel_frames) {
			fprintf(stderr, "%s: failed to read entire file (read %lld of %lld)\n", kernel_path, n_frames_read, n_kernel_frames);
			exit(EXIT_FAILURE);
		}

		ma_decoder_uninit(&decoder);

		for (int i = 0; i < MAX_CHANNELS; i++) {
			int fi = i % n_kernel_channels;
			assert((convs[i] = conv_new(kernel_frames+fi, n_kernel_frames, n_kernel_channels)) != NULL);
		}

		free(kernel_frames);
	}

	if (in_path == NULL && out_path == NULL) {
		// real-time
		ma_device device;

		ma_device_config config = ma_device_config_init(ma_device_type_duplex);
		config.capture.format = ma_format_f32;
		config.capture.channels = 2;
		config.playback.format = ma_format_f32;
		config.playback.channels = 2;
		config.sampleRate   = sample_rate;
		config.dataCallback = audio_callback;

		if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
			fprintf(stderr, "ma_device_init() failed\n");
			exit(EXIT_FAILURE);
		}

		audio.n_input_channels = device.capture.channels;
		audio.n_output_channels = device.playback.channels;

		{
			size_t sz = INPUT_BUFFER_SZ * sizeof(float);
			for (int i = 0; i < MAX_CHANNELS; i++) {
				assert((audio.input_buffers[i] = malloc(sz)) != NULL);
			}
			assert((audio.output_buffer = malloc(sz)) != NULL);
		}

		ma_device_start(&device);

		for(;;) usleep(1000000);
	} else if (in_path != NULL && out_path != NULL) {
		// file-based

		ma_decoder decoder;
		ma_uint32 n_input_channels = init_decoder(&decoder, in_path, n_kernel_channels);
		assert(n_input_channels == n_kernel_channels);

		ma_uint32 n_output_channels = n_input_channels;
		if (n_kernel_channels > n_output_channels) {
			n_output_channels = n_kernel_channels;
		}

		ma_encoder encoder;
		ma_encoder_config encoder_config = ma_encoder_config_init(ma_resource_format_wav, ma_format_f32, n_output_channels, sample_rate);
		if (ma_encoder_init_file(out_path, &encoder_config, &encoder) != MA_SUCCESS) {
			fprintf(stderr, "%s: encoder init failed\n", out_path);
		}

		const int bufsz = 65536;
		float* input_frames = calloc(bufsz, sizeof *input_frames);
		assert(input_frames != NULL);
		float* output_frames = calloc(bufsz, sizeof *output_frames);
		assert(output_frames != NULL);
		float* input_samples = calloc(bufsz, sizeof *input_samples);
		assert(input_samples != NULL);
		float* output_samples = calloc(bufsz, sizeof *output_samples);
		assert(output_samples != NULL);

		for (;;) {
			ma_uint64 rq = bufsz / n_input_channels;
			ma_uint64 n_read = ma_decoder_read_pcm_frames(&decoder, input_frames, rq);

			for (int i = 0; i < n_output_channels; i++) {
				{
					int im = i % n_input_channels;
					for (int j = 0; j < n_read; j++) {
						input_samples[j] = input_frames[j*n_input_channels + im];
					}
				}

				conv_process(convs[i], output_samples, input_samples, n_read);

				{
					int im = i % n_output_channels;
					for (int j = 0; j < n_read; j++) {
						output_frames[j*n_output_channels+im] = output_samples[j];
					}
				}
			}

			ma_encoder_write_pcm_frames(&encoder, output_frames, n_read);

			if (n_read != rq) break;
		}

		ma_encoder_uninit(&encoder);
		ma_decoder_uninit(&decoder);
	} else {
		fprintf(stderr, "wrong number of non-positional arguments\n\n");
		usage_err();
	}

	return EXIT_SUCCESS;
}
