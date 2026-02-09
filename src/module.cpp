#include <napi.h>

#include "GstPlayer.h"


Napi::Object Init(Napi::Env env, Napi::Object exports)
{
	return GstPlayer::Init(env, exports);
}

NODE_API_MODULE(node_gst_player, Init)
