#include "TeensyControls.h"

// http://www.xsquawkbox.net/xpsdk/mediawiki/Category:Documentation


/// <summary>
/// This is called when the plugin is enabled. You do not need to do anything
/// in this callback, but if you want, we can allocate resources that we only
/// need while enabled.
/// </summary>
int PluginEnable(void)
{
	printf("Plugin Enable\n");
	TeensyControls_output(0.0, 1);
	return 1;
}

/// <summary>
/// This is called when the plugin is disabled. You do not need to do anything
/// in this callback, but if we want, you can deallocate resources that are only
/// needed while enabled. Once disabled, the plugin may not run again for a very
/// long time, so you should close any network connections that might time out otherwise.
/// </summary>
void PluginDisable(void)
{
	printf("Plugin Disable\n");
	TeensyControls_output(0.0, 2);
}
