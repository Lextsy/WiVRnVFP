package org.meumeu.wivrn;

public class MainActivity extends com.htc.vr.sdk.VRActivity
{
	static
	{
		System.loadLibrary("wivrn");
	}

	public native void initWave();

	public native void waveDestroy();

	@Override
	public native void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults);

	@Override
	public native void onNewIntent(android.content.Intent intent);

	@Override
	public native void onActivityResult(int requestCode, int resultCode, android.content.Intent data);

	private android.content.BroadcastReceiver BatteryInfoReceiver = null;
	public android.net.ConnectivityManager.NetworkCallback netcb = null;

	@Override
	protected void onCreate(android.os.Bundle savedInstanceState)
	{
		initWave();
		super.onCreate(savedInstanceState);

		this.BatteryInfoReceiver = new BroadcastReceiver();
		this.netcb = new org.meumeu.wivrn.NetworkInfoCallback();

		this.registerReceiver(this.BatteryInfoReceiver, new android.content.IntentFilter(android.content.Intent.ACTION_BATTERY_CHANGED));
	}

	@Override
	protected void onDestroy()
	{
		waveDestroy();

		if (this.BatteryInfoReceiver != null)
			this.unregisterReceiver(this.BatteryInfoReceiver);

		super.onDestroy();
	}
}
