#include "GstPlayer.h"
#include <glib.h>
#include <cstring>

Napi::FunctionReference GstPlayer::_constructor;

Napi::Object GstPlayer::Init(Napi::Env env, Napi::Object exports)
{
	gst_init(nullptr, nullptr);

	Napi::Function func = DefineClass(
		env,
		"GstPlayer",
		{
			InstanceValue("GST_STATE_VOID_PENDING", Napi::Number::New(env, GST_STATE_VOID_PENDING)),
			InstanceValue("GST_STATE_NULL", Napi::Number::New(env, GST_STATE_NULL)),
			InstanceValue("GST_STATE_READY", Napi::Number::New(env, GST_STATE_READY)),
			InstanceValue("GST_STATE_PAUSED", Napi::Number::New(env, GST_STATE_PAUSED)),
			InstanceValue("GST_STATE_PLAYING", Napi::Number::New(env, GST_STATE_PLAYING)),
			InstanceValue("AppSinkSetup", Napi::Number::New(env, Setup)),
			InstanceValue("AppSinkNewPreroll", Napi::Number::New(env, NewPreroll)),
			InstanceValue("AppSinkNewSample", Napi::Number::New(env, NewSample)),
			InstanceValue("AppSinkEos", Napi::Number::New(env, Eos)),
			InstanceMethod("parseLaunch", &GstPlayer::parseLaunch),
			InstanceMethod("addAppSinkCallback", &GstPlayer::addAppSinkCallback),
			InstanceMethod("addCapsProbe", &GstPlayer::addCapsProbe),
			InstanceMethod("setState", &GstPlayer::setState),
			InstanceMethod("sendEos", &GstPlayer::sendEos),
		});

	_constructor = Napi::Persistent(func);
	_constructor.SuppressDestruct();
	exports.Set("GstPlayer", func);
	return exports;
}

GstPlayer::GstPlayer(const Napi::CallbackInfo &info) : Napi::ObjectWrap<GstPlayer>(info) {}

GstPlayer::~GstPlayer()
{
	close();
}

void GstPlayer::close()
{
	if (_pipeline)
	{
		gst_element_set_state(_pipeline, GST_STATE_NULL);

		gst_object_unref(_pipeline);
		_pipeline = nullptr;
	}
}

void GstPlayer::parseLaunch(const Napi::CallbackInfo &info)
{
	if (!info[0].IsString())
	{
		Napi::TypeError::New(info.Env(), "Expected string").ThrowAsJavaScriptException();
		return;
	}

	close();

	std::string desc = info[0].As<Napi::String>().Utf8Value();
	GError *error = nullptr;
	_pipeline = gst_parse_launch(desc.c_str(), &error);

	if (!_pipeline)
	{
		Napi::Error::New(info.Env(), error ? error->message : "Failed to parse launch").ThrowAsJavaScriptException();
	}
}

static Napi::Object createInfoFromCaps(Napi::Env env, GstCaps *caps);

void GstPlayer::addAppSinkCallback(const Napi::CallbackInfo &info)
{
	if (!_pipeline || !info[0].IsString() || !info[1].IsFunction())
	{
		Napi::TypeError::New(info.Env(), "Expected (string, function)").ThrowAsJavaScriptException();
		return;
	}

	std::string name = info[0].As<Napi::String>().Utf8Value();
	Napi::Function callback = info[1].As<Napi::Function>();

	GstElement *element = gst_bin_get_by_name(GST_BIN(_pipeline), name.c_str());
	if (!element)
	{
		Napi::Error::New(info.Env(), "Element not found").ThrowAsJavaScriptException();
		return;
	}

	GstAppSink *sink = GST_APP_SINK(element);
	if (!sink)
	{
		gst_object_unref(element);
		Napi::Error::New(info.Env(), "Not an app sink").ThrowAsJavaScriptException();
		return;
	}

	Napi::ThreadSafeFunction *tsfn = new Napi::ThreadSafeFunction(Napi::ThreadSafeFunction::New(
		info.Env(),
		callback,
		"AppSinkTSFN",
		0,
		1));

	auto onNewPreroll = [](GstAppSink *appsink, gpointer userData) -> GstFlowReturn
	{
		Napi::ThreadSafeFunction *tsfn = static_cast<Napi::ThreadSafeFunction *>(userData);

		GstSample *preroll = gst_app_sink_pull_preroll(appsink);
		if (preroll)
		{
			GstCaps *caps = gst_sample_get_caps(preroll);
			if (caps)
			{
				gst_caps_ref(caps);
				tsfn->NonBlockingCall(
					[caps](Napi::Env env, Napi::Function cb)
					{
						Napi::Object info = createInfoFromCaps(env, caps);
						gst_caps_unref(caps);
						cb.Call({Napi::Number::New(env, GstPlayer::Setup), info});
					});
			}

			GstBuffer *buf = gst_sample_get_buffer(preroll);
			if (buf)
			{
				gst_buffer_ref(buf); // 增加 buffer 引用
				tsfn->NonBlockingCall(
					[buf](Napi::Env env, Napi::Function cb)
					{
						GstMapInfo map;
						if (gst_buffer_map(buf, &map, GST_MAP_READ))
						{
							// 直接在 JavaScript 线程中拷贝，只拷贝一次
							auto buffer = Napi::Buffer<uint8_t>::NewOrCopy(env, map.data, map.size);
							gst_buffer_unmap(buf, &map);
							cb.Call({Napi::Number::New(env, GstPlayer::NewPreroll), buffer});
						}
						gst_buffer_unref(buf); // 释放 buffer 引用
					});
			}
			gst_sample_unref(preroll);
		}
		return GST_FLOW_OK;
	};
	auto onNewSample = [](GstAppSink *appsink, gpointer userData) -> GstFlowReturn
	{
		Napi::ThreadSafeFunction *tsfn = static_cast<Napi::ThreadSafeFunction *>(userData);
		GstSample *sample = gst_app_sink_pull_sample(appsink);
		if (sample)
		{
			GstBuffer *buf = gst_sample_get_buffer(sample);
			if (buf)
			{
				gst_buffer_ref(buf); // 增加 buffer 引用
				tsfn->NonBlockingCall(
					[buf](Napi::Env env, Napi::Function cb)
					{
						GstMapInfo map;
						if (gst_buffer_map(buf, &map, GST_MAP_READ))
						{
							// 直接在 JavaScript 线程中拷贝，只拷贝一次
							auto buffer = Napi::Buffer<uint8_t>::NewOrCopy(env, map.data, map.size);
							gst_buffer_unmap(buf, &map);
							cb.Call({Napi::Number::New(env, GstPlayer::NewSample), buffer});
						}
						gst_buffer_unref(buf); // 释放 buffer 引用
					});
			}
			gst_sample_unref(sample);
		}
		return GST_FLOW_OK;
	};
	auto onEos = [](GstAppSink *appsink, gpointer userData)
	{
		Napi::ThreadSafeFunction *tsfn = static_cast<Napi::ThreadSafeFunction *>(userData);
		tsfn->NonBlockingCall(
			[](Napi::Env env, Napi::Function cb)
			{
				cb.Call({Napi::Number::New(env, GstPlayer::Eos)});
			});
	};
	auto onDestroy = [](gpointer userData)
	{
		Napi::ThreadSafeFunction *tsfn = static_cast<Napi::ThreadSafeFunction *>(userData);
		tsfn->Release();
		delete tsfn;
	};

	GstAppSinkCallbacks callbacks = {
		.eos = onEos,
		.new_preroll = onNewPreroll,
		.new_sample = onNewSample,
	};
	gst_app_sink_set_callbacks(sink, &callbacks, tsfn, onDestroy);
	gst_object_unref(element);
}

void GstPlayer::addCapsProbe(const Napi::CallbackInfo &info)
{
	if (!_pipeline || !info[0].IsString() || !info[1].IsString() || !info[2].IsFunction())
	{
		Napi::TypeError::New(info.Env(), "Expected (string, string, function)").ThrowAsJavaScriptException();
		return;
	}

	std::string elemName = info[0].As<Napi::String>().Utf8Value();
	std::string padName = info[1].As<Napi::String>().Utf8Value();
	Napi::Function callback = info[2].As<Napi::Function>();

	GstElement *elem = gst_bin_get_by_name(GST_BIN(_pipeline), elemName.c_str());
	if (!elem)
	{
		Napi::Error::New(info.Env(), "Element not found").ThrowAsJavaScriptException();
		return;
	}

	GstPad *pad = gst_element_get_static_pad(elem, padName.c_str());
	if (!pad)
	{
		gst_object_unref(elem);
		Napi::Error::New(info.Env(), "Pad not found").ThrowAsJavaScriptException();
		return;
	}

	Napi::ThreadSafeFunction *tsfn = new Napi::ThreadSafeFunction(Napi::ThreadSafeFunction::New(
		info.Env(),
		callback,
		"CapsProbeTSFN",
		0,
		1));

	auto onProbe = [](GstPad *pad, GstPadProbeInfo *info, gpointer userData) -> GstPadProbeReturn
	{
		GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
		if (event && GST_EVENT_TYPE(event) == GST_EVENT_CAPS)
		{
			GstCaps *caps;
			gst_event_parse_caps(event, &caps);

			if (caps)
			{
				gst_caps_ref(caps);
				Napi::ThreadSafeFunction *tsfn = static_cast<Napi::ThreadSafeFunction *>(userData);
				tsfn->NonBlockingCall(
					[caps](Napi::Env env, Napi::Function cb)
					{
						Napi::Object info = createInfoFromCaps(env, caps);
						gst_caps_unref(caps);
						cb.Call({info});
					});
			}
		}
		return GST_PAD_PROBE_OK;
	};

	auto onDestroy = [](gpointer userData)
	{
		Napi::ThreadSafeFunction *tsfn = static_cast<Napi::ThreadSafeFunction *>(userData);
		tsfn->Release();
		delete tsfn;
	};

	gst_pad_add_probe(
		pad,
		GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
		onProbe,
		tsfn,
		onDestroy);

	gst_object_unref(pad);
	gst_object_unref(elem);
}

void GstPlayer::setState(const Napi::CallbackInfo &info)
{
	if (!_pipeline || !info[0].IsNumber())
		return;

	unsigned state = info[0].As<Napi::Number>().Uint32Value();
	gst_element_set_state(_pipeline, static_cast<GstState>(state));
}

void GstPlayer::sendEos(const Napi::CallbackInfo &info)
{
	if (_pipeline)
		gst_element_send_event(_pipeline, gst_event_new_eos());
}

static Napi::Object createInfoFromCaps(Napi::Env env, GstCaps *caps)
{
	Napi::Object info = Napi::Object::New(env);

	if (!caps || gst_caps_is_empty(caps))
		return info;

	GstStructure *s = gst_caps_get_structure(caps, 0);
	const gchar *name = gst_structure_get_name(s);
	const gchar *mediaType = name;

	// 添加 mediaType
	info.Set("mediaType", Napi::String::New(env, mediaType));

	if (g_str_has_prefix(name, "audio/"))
	{
		GstAudioInfo audioInfo;
		if (gst_audio_info_from_caps(&audioInfo, caps))
		{
			info.Set("channels", Napi::Number::New(env, audioInfo.channels));
			info.Set("samplingRate", Napi::Number::New(env, audioInfo.rate));
			info.Set("sampleSize", Napi::Number::New(env, audioInfo.bpf));
			info.Set("format", Napi::String::New(env, audioInfo.finfo->name));
		}
	}
	else if (g_str_has_prefix(name, "video/"))
	{
		GstVideoInfo videoInfo;
		if (gst_video_info_from_caps(&videoInfo, caps))
		{
			info.Set("pixelFormat", Napi::String::New(env, videoInfo.finfo->name));
			info.Set("width", Napi::Number::New(env, videoInfo.width));
			info.Set("height", Napi::Number::New(env, videoInfo.height));
			info.Set("fpsNum", Napi::Number::New(env, videoInfo.fps_n));
			info.Set("fpsDen", Napi::Number::New(env, videoInfo.fps_d));
			info.Set("parNum", Napi::Number::New(env, videoInfo.par_n));
			info.Set("parDen", Napi::Number::New(env, videoInfo.par_d));
		}
	}

	// 添加完整的 caps 字符串
	gchar *capsStr = gst_caps_to_string(caps);
	info.Set("caps", Napi::String::New(env, capsStr));
	g_free(capsStr);

	return info;
}