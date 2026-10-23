/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code"
#include "audio.h"
#pragma clang diagnostic pop
#include "android/jnipp.h"
#include "application.h"
#include "utils/named_thread.h"
#include "wivrn_client.h"
#include "xr/instance.h"

#include "spdlog/spdlog.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>
#if __ANDROID_API__ >= 26
#include <aaudio/AAudio.h>
#endif

namespace
{
// OpenSL ES 1.0.1 cannot query the mixer configuration. The Android CDD requires
// 48 kHz output (and 192 frames per buffer) on API 19+ devices.
constexpr uint8_t opensles_channels = 2;
constexpr uint32_t opensles_sample_rate = 48'000;
} // namespace

// Audio transport, shared by the AAudio and OpenSL ES backends
void wivrn::android::audio::fill_speaker_buffer(uint8_t * audio_data, size_t bytes, int channels, int sample_rate)
{
	size_t frame_size = channels * sizeof(uint16_t);
	size_t remain = bytes;

	while (remain != 0)
	{
		// remaining bytes in existing buffer
		ptrdiff_t tmp_remain = speaker_tmp.payload.size_bytes();
		// limit to requested bytes
		tmp_remain = std::min<ptrdiff_t>(tmp_remain, remain);
		if (tmp_remain)
		{
			memcpy(audio_data, speaker_tmp.payload.data(), tmp_remain);
			speaker_tmp.payload = speaker_tmp.payload.subspan(tmp_remain);
			audio_data += tmp_remain;
			remain -= tmp_remain;
			buffer_size_bytes.fetch_sub(tmp_remain);
		}
		else
		{
			if (auto tmp = output_buffer.read())
			{
				speaker_tmp = std::move(*tmp);
			}
			else
			{
				// Buffer underrun: add 5ms buffer
				size_t target_buffer_size = frame_size * sample_rate * 0.005;
				speaker_tmp.data.c = std::make_shared<uint8_t[]>(target_buffer_size, 0);
				speaker_tmp.payload = std::span(speaker_tmp.data.c.get(), target_buffer_size);
				buffer_size_bytes += target_buffer_size;
				spdlog::debug("Audio sync: underrun, add {} bytes buffer",
				              target_buffer_size);
			}
		}
	}
}

void wivrn::android::audio::discard_excess_buffer(int channels, int sample_rate)
{
	size_t frame_size = channels * sizeof(uint16_t);

	// If we have more than 50ms of buffered data, discard some data
	if (buffer_size_bytes > frame_size * sample_rate * 0.05)
	{
		// discard excess data until we only have 30ms left
		size_t target_buffer_size = frame_size * sample_rate * 0.03;
		while (buffer_size_bytes > target_buffer_size and output_buffer.size() > 1)
		{
			auto tmp = output_buffer.read();
			if (not tmp)
				break;
			auto prev = buffer_size_bytes.fetch_sub(tmp->payload.size_bytes());
			spdlog::info("Audio sync: discard {} bytes (buffer {} target {})",
			             tmp->payload.size_bytes(),
			             prev,
			             target_buffer_size);
		}
	}
}

bool wivrn::android::audio::send_microphone_data(const uint8_t * data, size_t bytes)
{
	// Copy data because we encrypt in-place, we don't want to write on the input data
	thread_local std::vector<uint8_t> data_copy;
	data_copy.assign(data, data + bytes);
	try
	{
		session.send_control(wivrn::audio_data{
		        .timestamp = instance.now(),
		        .payload = std::span(data_copy),
		});
	}
	catch (...)
	{
		return false;
	}
	return true;
}

void wivrn::android::audio::exit()
{
	exiting = true;
	if (use_aaudio)
	{
#if __ANDROID_API__ >= 26
		if (speaker)
		{
			speaker_stop_ack.wait(false);
			AAudioStream_requestStop(speaker);
		}
		if (microphone and mic_running)
		{
			microphone_stop_ack.wait(false);
			AAudioStream_requestStop(microphone);
		}
#endif
	}
	else
	{
		if (player_itf)
		{
			(*player_itf)->SetPlayState(player_itf, SL_PLAYSTATE_STOPPED);
			speaker_stop_ack = true;
			speaker_stop_ack.notify_all();
		}
		if (record_itf and mic_running)
		{
			mic_running = false;
			(*record_itf)->SetRecordState(record_itf, SL_RECORDSTATE_STOPPED);
			microphone_stop_ack = true;
			microphone_stop_ack.notify_all();
		}
	}
}

#if __ANDROID_API__ >= 26
int32_t wivrn::android::audio::speaker_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto self = (wivrn::android::audio *)userdata;
	uint8_t * audio_data = (uint8_t *)audio_data_v;

	if (self->exiting)
	{
		self->speaker_stop_ack = true;
		self->speaker_stop_ack.notify_all();
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t num_channels = AAudioStream_getChannelCount(stream);
	size_t sample_rate = AAudioStream_getSampleRate(stream);
	size_t frame_size = num_channels * sizeof(uint16_t);

	self->fill_speaker_buffer(audio_data, num_frames * frame_size, num_channels, sample_rate);
	self->discard_excess_buffer(num_channels, sample_rate);

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

int32_t wivrn::android::audio::microphone_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto self = (wivrn::android::audio *)userdata;
	uint8_t * audio_data = (uint8_t *)audio_data_v;

	if (self->exiting)
	{
		self->microphone_stop_ack = true;
		self->microphone_stop_ack.notify_all();
		self->mic_running = false;
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t frame_size = AAudioStream_getChannelCount(stream) * sizeof(uint16_t);

	if (not self->send_microphone_data(audio_data, frame_size * num_frames))
	{
		self->microphone_stop_ack = true;
		self->microphone_stop_ack.notify_all();
		self->mic_running = false;
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void wivrn::android::audio::build_microphone(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels)
{
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	AAudioStreamBuilder_setSampleRate(builder, sample_rate);
	AAudioStreamBuilder_setChannelCount(builder, num_channels);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	if (application::get_config().mic_unprocessed_audio)
		AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_UNPROCESSED);
	else
		AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);

	AAudioStreamBuilder_setDataCallback(builder, &microphone_data_cb, this);
	AAudioStreamBuilder_setErrorCallback(builder, &microphone_error_cb, this);

	aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &microphone);
	if (result != AAUDIO_OK)
		spdlog::error("Cannot create input stream: {}", AAudio_convertResultToText(result));
	mic_running = false;
	// Microphone is started manually by a tracking_control packet
}

void wivrn::android::audio::build_speaker(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels)
{
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStreamBuilder_setSampleRate(builder, sample_rate);
	AAudioStreamBuilder_setChannelCount(builder, num_channels);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

	AAudioStreamBuilder_setDataCallback(builder, &speaker_data_cb, this);
	AAudioStreamBuilder_setErrorCallback(builder, &speaker_error_cb, this);

	aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &speaker);
	if (result != AAUDIO_OK)
		spdlog::error("Cannot create output stream: {}", AAudio_convertResultToText(result));

	AAudioStream_requestStart(speaker);
	if (result == AAUDIO_OK)
		spdlog::info("Speaker stream started");
	else
	{
		AAudioStream_close(speaker);
		spdlog::warn("Speaker stream failed to start: {}", AAudio_convertResultToText(result));
	}
}

void wivrn::android::audio::recreate_stream(AAudioStreamStruct * stream)
{
	if (exiting)
		return;
	std::unique_lock lock(mutex);

	if (recreate_thread.joinable())
	{
		assert(recreate_thread.get_id() != std::this_thread::get_id());
		recreate_thread.join();
	}

	recreate_thread = utils::named_thread("recreate_audio", [=, this]() {
		std::unique_lock lock(mutex);
		size_t num_channels = AAudioStream_getChannelCount(stream);
		size_t sample_rate = AAudioStream_getSampleRate(stream);

		AAudioStream_requestStop(stream);
		AAudioStream_close(stream);

		AAudioStreamBuilder * builder;
		aaudio_result_t result = AAudio_createStreamBuilder(&builder);
		if (result != AAUDIO_OK)
			spdlog::error("Cannot create stream builder: {}", AAudio_convertResultToText(result));

		if (stream == speaker)
			build_speaker(builder, sample_rate, num_channels);
		else if (stream == microphone)
			build_microphone(builder, sample_rate, num_channels);
		else
			spdlog::error("Stream to recreate is neither speaker, not microphone!");

		AAudioStreamBuilder_delete(builder);
	});
}

void wivrn::android::audio::speaker_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error)
{
	auto self = (wivrn::android::audio *)userdata;
	spdlog::warn("Speaker stream interrupted: {}", AAudio_convertResultToText(error));
	if (error == AAUDIO_ERROR_DISCONNECTED)
		self->recreate_stream(stream);
}

void wivrn::android::audio::microphone_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error)
{
	auto self = (wivrn::android::audio *)userdata;
	spdlog::warn("Microphone stream interrupted: {}", AAudio_convertResultToText(error));
	if (error == AAUDIO_ERROR_DISCONNECTED)
		self->recreate_stream(stream);
}
#endif

// OpenSL ES backend (Android API 25 and below)
void wivrn::android::audio::opensles_init()
{
	SLresult result = slCreateEngine(&engine_object, 0, nullptr, 0, nullptr, nullptr);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot create audio engine: {}", (int)result);
		return;
	}

	result = (*engine_object)->Realize(engine_object, SL_BOOLEAN_FALSE);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot realize audio engine: {}", (int)result);
		(*engine_object)->Destroy(engine_object);
		engine_object = nullptr;
		return;
	}

	(*engine_object)->GetInterface(engine_object, SL_IID_ENGINE, &engine_itf);

	// Android's OpenSL ES rejects a NULL sink in CreateAudioPlayer
	// ("pAudioSnk: data sink cannot be NULL"), so the player needs an explicit
	// output mix to route to.
	result = (*engine_itf)->CreateOutputMix(engine_itf, &output_mix_object, 0, nullptr, nullptr);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot create audio output mix: {}", (int)result);
		(*engine_object)->Destroy(engine_object);
		engine_object = nullptr;
		engine_itf = nullptr;
		return;
	}

	result = (*output_mix_object)->Realize(output_mix_object, SL_BOOLEAN_FALSE);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot realize audio output mix: {}", (int)result);
		(*output_mix_object)->Destroy(output_mix_object);
		output_mix_object = nullptr;
		(*engine_object)->Destroy(engine_object);
		engine_object = nullptr;
		engine_itf = nullptr;
		return;
	}
}

void wivrn::android::audio::opensles_build_speaker(int32_t sample_rate, int32_t num_channels)
{
	speaker_rate = sample_rate;
	speaker_channels = num_channels;
	// 10ms of audio per buffer, two buffers in flight
	player_buffer.resize((size_t)sample_rate / 100 * num_channels * sizeof(uint16_t));

	SLDataFormat_PCM format{
	        .formatType = SL_DATAFORMAT_PCM,
	        .numChannels = (SLuint32)num_channels,
	        .samplesPerSec = (SLuint32)(sample_rate * 1000),
	        .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
	        .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
	        .channelMask = num_channels == 2 ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER,
	        .endianness = SL_BYTEORDER_LITTLEENDIAN,
	};
	SLDataLocator_AndroidSimpleBufferQueue locator{
	        .locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
	        .numBuffers = 2,
	};
	SLDataSource source{
	        .pLocator = &locator,
	        .pFormat = &format,
	};

	SLDataLocator_OutputMix output_mix_locator{
	        .locatorType = SL_DATALOCATOR_OUTPUTMIX,
	        .outputMix = output_mix_object,
	};
	SLDataSink sink{
	        .pLocator = &output_mix_locator,
	        .pFormat = nullptr,
	};

	SLInterfaceID interface_ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_ANDROIDCONFIGURATION};
	SLboolean required[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

	SLresult result = (*engine_itf)->CreateAudioPlayer(engine_itf, &player_object, &source, &sink, 2, interface_ids, required);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot create audio player: {}", (int)result);
		return;
	}

	SLAndroidConfigurationItf config = nullptr;
	if ((*player_object)->GetInterface(player_object, SL_IID_ANDROIDCONFIGURATION, &config) == SL_RESULT_SUCCESS and config)
	{
		SLuint32 performance_mode = SL_ANDROID_PERFORMANCE_LATENCY;
		(*config)->SetConfiguration(config, SL_ANDROID_KEY_PERFORMANCE_MODE, &performance_mode, sizeof(performance_mode));
		SLint32 stream_type = SL_ANDROID_STREAM_MEDIA;
		(*config)->SetConfiguration(config, SL_ANDROID_KEY_STREAM_TYPE, &stream_type, sizeof(stream_type));
	}

	result = (*player_object)->Realize(player_object, SL_BOOLEAN_FALSE);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot realize audio player: {}", (int)result);
		(*player_object)->Destroy(player_object);
		player_object = nullptr;
		return;
	}

	(*player_object)->GetInterface(player_object, SL_IID_PLAY, &player_itf);
	(*player_object)->GetInterface(player_object, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &player_queue);

	(*player_queue)->RegisterCallback(player_queue, &audio::speaker_buffer_cb, this);

	fill_speaker_buffer(player_buffer.data(), player_buffer.size(), num_channels, sample_rate);
	(*player_queue)->Enqueue(player_queue, player_buffer.data(), player_buffer.size());
	(*player_itf)->SetPlayState(player_itf, SL_PLAYSTATE_PLAYING);
	spdlog::info("OpenSL ES speaker started: {} Hz, {} channels", sample_rate, num_channels);
}

void wivrn::android::audio::opensles_build_microphone(int32_t sample_rate, int32_t num_channels)
{
	mic_rate = sample_rate;
	mic_channels = num_channels;
	recorder_buffer.resize((size_t)sample_rate / 100 * num_channels * sizeof(uint16_t));

	SLDataLocator_IODevice device_locator{
	        .locatorType = SL_DATALOCATOR_IODEVICE,
	        .deviceType = SL_IODEVICE_AUDIOINPUT,
	        .deviceID = SL_DEFAULTDEVICEID_AUDIOINPUT,
	        .device = nullptr,
	};
	SLDataSource source{
	        .pLocator = &device_locator,
	        .pFormat = nullptr,
	};

	SLDataFormat_PCM format{
	        .formatType = SL_DATAFORMAT_PCM,
	        .numChannels = (SLuint32)num_channels,
	        .samplesPerSec = (SLuint32)(sample_rate * 1000),
	        .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
	        .containerSize = SL_PCMSAMPLEFORMAT_FIXED_16,
	        .channelMask = num_channels == 2 ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER,
	        .endianness = SL_BYTEORDER_LITTLEENDIAN,
	};
	SLDataLocator_AndroidSimpleBufferQueue locator{
	        .locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
	        .numBuffers = 2,
	};
	SLDataSink sink{
	        .pLocator = &locator,
	        .pFormat = &format,
	};

	SLInterfaceID interface_ids[] = {SL_IID_ANDROIDCONFIGURATION, SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
	SLboolean required[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

	SLresult result = (*engine_itf)->CreateAudioRecorder(engine_itf, &recorder_object, &source, &sink, 2, interface_ids, required);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot create audio recorder: {}", (int)result);
		return;
	}

	// The recording preset must be set before realizing the recorder
	SLAndroidConfigurationItf config = nullptr;
	if ((*recorder_object)->GetInterface(recorder_object, SL_IID_ANDROIDCONFIGURATION, &config) == SL_RESULT_SUCCESS and config)
	{
		SLuint32 preset = application::get_config().mic_unprocessed_audio ? SL_ANDROID_RECORDING_PRESET_UNPROCESSED
		                                                                  : SL_ANDROID_RECORDING_PRESET_VOICE_COMMUNICATION;
		(*config)->SetConfiguration(config, SL_ANDROID_KEY_RECORDING_PRESET, &preset, sizeof(preset));
		SLuint32 performance_mode = SL_ANDROID_PERFORMANCE_LATENCY;
		(*config)->SetConfiguration(config, SL_ANDROID_KEY_PERFORMANCE_MODE, &performance_mode, sizeof(performance_mode));
	}

	result = (*recorder_object)->Realize(recorder_object, SL_BOOLEAN_FALSE);
	if (result != SL_RESULT_SUCCESS)
	{
		spdlog::error("Cannot realize audio recorder: {}", (int)result);
		(*recorder_object)->Destroy(recorder_object);
		recorder_object = nullptr;
		return;
	}

	(*recorder_object)->GetInterface(recorder_object, SL_IID_RECORD, &record_itf);
	(*recorder_object)->GetInterface(recorder_object, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &recorder_queue);

	(*recorder_queue)->RegisterCallback(recorder_queue, &audio::microphone_buffer_cb, this);
	// Recording is started manually by a tracking_control packet (set_mic_state)
}

void wivrn::android::audio::speaker_buffer_cb(SLAndroidSimpleBufferQueueItf queue, void * userdata)
{
	auto self = (wivrn::android::audio *)userdata;

	if (self->exiting)
	{
		self->speaker_stop_ack = true;
		self->speaker_stop_ack.notify_all();
		return;
	}

	self->fill_speaker_buffer(self->player_buffer.data(), self->player_buffer.size(), self->speaker_channels, self->speaker_rate);
	self->discard_excess_buffer(self->speaker_channels, self->speaker_rate);
	(*queue)->Enqueue(queue, self->player_buffer.data(), self->player_buffer.size());
}

void wivrn::android::audio::microphone_buffer_cb(SLAndroidSimpleBufferQueueItf queue, void * userdata)
{
	auto self = (wivrn::android::audio *)userdata;

	if (self->exiting)
	{
		self->microphone_stop_ack = true;
		self->microphone_stop_ack.notify_all();
		self->mic_running = false;
		return;
	}

	if (self->mic_running)
	{
		// The simple buffer queue has no "consumed bytes" query: the whole buffer is
		// filled by the recorder before the callback fires.
		if (not self->send_microphone_data(self->recorder_buffer.data(), self->recorder_buffer.size()))
		{
			self->microphone_stop_ack = true;
			self->microphone_stop_ack.notify_all();
			self->mic_running = false;
			return;
		}
	}

	(*queue)->Enqueue(queue, self->recorder_buffer.data(), self->recorder_buffer.size());
}

wivrn::android::audio::audio(const wivrn::to_headset::audio_stream_description & desc, wivrn_session & session, xr::instance & instance) :
        session(session), instance(instance)
{
	std::unique_lock lock(mutex);
	use_aaudio = android_api_level() >= 26;

	if (use_aaudio)
	{
#if __ANDROID_API__ >= 26
		AAudioStreamBuilder * builder;
		aaudio_result_t result = AAudio_createStreamBuilder(&builder);
		if (result != AAUDIO_OK)
			throw std::runtime_error(std::string("Cannot create stream builder: ") + AAudio_convertResultToText(result));

		if (desc.microphone)
			build_microphone(builder, desc.microphone->sample_rate, desc.microphone->num_channels);

		if (desc.speaker)
			build_speaker(builder, desc.speaker->sample_rate, desc.speaker->num_channels);

		AAudioStreamBuilder_delete(builder);
#endif
	}
	else
	{
		opensles_init();
		if (not engine_itf)
			throw std::runtime_error("Cannot create OpenSL ES engine");

		if (desc.microphone)
			opensles_build_microphone(desc.microphone->sample_rate, desc.microphone->num_channels);

		if (desc.speaker)
			opensles_build_speaker(desc.speaker->sample_rate, desc.speaker->num_channels);
	}
}

wivrn::android::audio::~audio()
{
	exit();

#if __ANDROID_API__ >= 26
	for (auto stream: {speaker, microphone})
	{
		if (stream)
			AAudioStream_close(stream);
	}
#endif

	for (SLObjectItf object: {player_object, recorder_object, output_mix_object, engine_object})
	{
		if (object)
			(*object)->Destroy(object);
	}

	player_object = nullptr;
	recorder_object = nullptr;
	output_mix_object = nullptr;
	engine_object = nullptr;
	player_itf = nullptr;
	record_itf = nullptr;
	engine_itf = nullptr;
	player_queue = nullptr;
	recorder_queue = nullptr;

	std::unique_lock lock(mutex);
	if (recreate_thread.joinable())
		recreate_thread.join();
}

void wivrn::android::audio::operator()(wivrn::audio_data && data)
{
	auto size = data.payload.size_bytes();
	if (output_buffer.write(std::move(data)))
		buffer_size_bytes.fetch_add(size);
}

void wivrn::android::audio::set_mic_state(bool running)
{
	if (use_aaudio)
	{
#if __ANDROID_API__ >= 26
		if (not microphone)
			return;

		auto old = mic_running.exchange(running);
		if (old == running)
			return;
		if (running)
			AAudioStream_requestStart(microphone);
		else
			AAudioStream_requestStop(microphone);
#endif
	}
	else
	{
		if (not record_itf)
			return;

		auto old = mic_running.exchange(running);
		if (old == running)
			return;
		if (running)
		{
			(*recorder_queue)->Enqueue(recorder_queue, recorder_buffer.data(), recorder_buffer.size());
			(*record_itf)->SetRecordState(record_itf, SL_RECORDSTATE_RECORDING);
		}
		else
			(*record_itf)->SetRecordState(record_itf, SL_RECORDSTATE_STOPPED);
	}
}

void wivrn::android::audio::get_audio_description(wivrn::from_headset::headset_info_packet & info)
{
	if (android_api_level() >= 26)
	{
#if __ANDROID_API__ >= 26
		AAudioStreamBuilder * builder;

		aaudio_result_t result = AAudio_createStreamBuilder(&builder);
		if (result != AAUDIO_OK)
			return;

		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
		AAudioStream * stream;
		result = AAudioStreamBuilder_openStream(builder, &stream);

		if (result == AAUDIO_OK)
		{
			info.speaker = {
			        .num_channels = (uint8_t)AAudioStream_getChannelCount(stream),
			        .sample_rate = (uint32_t)AAudioStream_getSampleRate(stream)};

			AAudioStream_close(stream);
		}

		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);

		AAudioStreamBuilder_setSampleRate(builder, 48'000);
		result = AAudioStreamBuilder_openStream(builder, &stream);

		if (result != AAUDIO_OK)
		{
			AAudioStreamBuilder_setSampleRate(builder, AAUDIO_UNSPECIFIED);
			result = AAudioStreamBuilder_openStream(builder, &stream);
		}

		if (result == AAUDIO_OK)
		{
			info.microphone = {
			        .num_channels = 1, // Some headsets report 2 channels but then fail
			        .sample_rate = (uint32_t)AAudioStream_getSampleRate(stream),
			};

			AAudioStream_close(stream);
		}

		AAudioStreamBuilder_delete(builder);
#endif
	}
	else
	{
		info.speaker = {
		        .num_channels = opensles_channels,
		        .sample_rate = opensles_sample_rate};

		info.microphone = {
		        .num_channels = 1, // Some headsets report 2 channels but then fail
		        .sample_rate = opensles_sample_rate};
	}
}
