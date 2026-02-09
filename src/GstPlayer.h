#pragma once

#include <map>
#include <optional>

#include <napi.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

class GstPlayer : public Napi::ObjectWrap<GstPlayer>
{
	static Napi::FunctionReference _constructor;

public:
	static Napi::Object Init(Napi::Env env, Napi::Object exports);

	GstPlayer(const Napi::CallbackInfo &info);
	~GstPlayer();

private:
	void parseLaunch(const Napi::CallbackInfo &info);
	void addAppSinkCallback(const Napi::CallbackInfo &info);
	void addCapsProbe(const Napi::CallbackInfo &info);
	void setState(const Napi::CallbackInfo &info);
	void sendEos(const Napi::CallbackInfo &info);

	void close();

private:
	enum
	{
		Setup = 0,
		NewPreroll,
		NewSample,
		Eos
	};
	GstElement *_pipeline = nullptr;
};
