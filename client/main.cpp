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

#include <cerrno>
#include <cstring>
#include <sched.h>
#include "application.h"
#include "scenes/lobby.h"
#include "spdlog/spdlog.h"
#include "version.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef __ANDROID__
#include "android/jnipp.h"
#include "spdlog/sinks/android_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "wave/wave_api.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#else
#include "spdlog/sinks/stdout_color_sinks.h"
#endif

#ifdef __ANDROID__
JavaVM * wave_vm = nullptr;
jobject wave_activity = nullptr; // global ref, owned for the process lifetime
AAssetManager * wave_asset_manager = nullptr;

extern "C" int wave_main(int, char **);
#endif

void real_main()
{
	if (wivrn::is_tag)
		spdlog::info("Starting WiVRn version {}", wivrn::display_version());
	else
		spdlog::info("Starting WiVRn version {} ({})", wivrn::display_version(), wivrn::git_commit);
	try
	{
		application_info info;
#ifdef __ANDROID__
		info.vm = wave_vm;
		info.activity = wave_activity;
		info.asset_manager = wave_asset_manager;
#endif
		info.name = "WiVRn";
		info.version = VK_MAKE_VERSION(1, 0, 0);
		application app(info);

		app.push_scene<scenes::lobby>();

		app.run();
	}
	catch (std::exception & e)
	{
		spdlog::error("Caught exception: \"{}\"", e.what());
	}
	catch (...)
	{
		spdlog::error("Caught unknown exception");
	}
}

#ifdef __ANDROID__
// Called by MainActivity.onCreate() before super.onCreate(): capture the JNI
// context (JavaVM, activity, asset manager) and register the Wave entry point.
// The Wave runtime invokes wave_main() on its VRThread once the VR session is
// established; WVR_Init() then runs there, as the WaveVR contract requires.
extern "C" void Java_org_meumeu_wivrn_MainActivity_initWave(JNIEnv * env, jobject activity)
{
	jni::jni_thread::setup_thread(env);
	env->GetJavaVM(&wave_vm);
	wave_activity = env->NewGlobalRef(activity);

	auto am = jni::object<"">(activity).call<jni::object<"android/content/res/AssetManager">>("getAssets");
	wave_asset_manager = AAssetManager_fromJava(env, am);

	// HTC mlogger suppresses app-side logcat output; mirror the log to the app's
	// external files dir (readable over adb) so the Wave render path is observable.
	auto ext_dir = jni::object<"">(activity).call<jni::object<"java/io/File">>("getExternalFilesDir", jni::object<"java/lang/String">(nullptr));
	std::string log_path = std::string(ext_dir.call<jni::string>("getAbsolutePath")) + "/wivrn.log";
	std::vector<spdlog::sink_ptr> sinks{
	        std::make_shared<spdlog::sinks::android_sink_mt>("WiVRn"),
	        std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path, true)};
	auto logger = std::make_shared<spdlog::logger>("WiVRn", sinks.begin(), sinks.end());
	logger->set_level(spdlog::level::info);
	logger->flush_on(spdlog::level::info);
	spdlog::set_default_logger(logger);

	if (not wvr::api::load())
	{
		spdlog::error("WaveVR: failed to load the WaveVR runtime");
		return;
	}

	wvr::api::get()->RegisterMain(wave_main);
}

extern "C" int wave_main(int, char **)
{
	jni::jni_thread::setup_thread(wave_vm);
	// Wave creates its VRThread as ordinary CFS while its own render threads
	// run SCHED_FIFO; the render loop then starves: pacing sleeps wake hundreds
	// of ms late on a busy device and the frame rate collapses. Join Wave's RT
	// band (the process already has RT grants — Wave's threads use them).
	sched_param rt{3};
	if (sched_setscheduler(0, SCHED_FIFO, &rt) != 0)
		spdlog::warn("WaveVR: VRThread SCHED_FIFO promotion failed: {}", std::strerror(errno));
	real_main();
	return 0;
}

// Called by MainActivity.onDestroy(): relay the exit request to the Wave main
// thread so run() returns and the VRThread finishes.
extern "C" void Java_org_meumeu_wivrn_MainActivity_waveDestroy(JNIEnv *, jobject)
{
	if (application::is_instance_created())
		application::request_exit();
}
#else
int main(int argc, char * argv[])
{
	spdlog::set_default_logger(spdlog::stdout_color_mt("WiVRn"));

	char * loglevel = getenv("WIVRN_LOGLEVEL");
	if (loglevel)
	{
		if (!strcasecmp(loglevel, "trace"))
			spdlog::set_level(spdlog::level::trace);
		else if (!strcasecmp(loglevel, "debug"))
			spdlog::set_level(spdlog::level::debug);
		else if (!strcasecmp(loglevel, "info"))
			spdlog::set_level(spdlog::level::info);
		else if (!strcasecmp(loglevel, "warning"))
			spdlog::set_level(spdlog::level::warn);
		else if (!strcasecmp(loglevel, "error"))
			spdlog::set_level(spdlog::level::err);
		else if (!strcasecmp(loglevel, "critical"))
			spdlog::set_level(spdlog::level::critical);
		else if (!strcasecmp(loglevel, "off"))
			spdlog::set_level(spdlog::level::off);
		else
			spdlog::warn("Invalid value for WIVRN_LOGLEVEL environment variable");
	}

	real_main();
}
#endif
