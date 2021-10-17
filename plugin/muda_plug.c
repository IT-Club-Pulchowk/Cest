
#include "../src/plugin.h"

MUDA_PLUGIN_INTERFACE 
void MudaEventHook(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface, Muda_Plugin_Event_Kind Event, const Muda_Plugin_Config *Config) {
	switch (Event) {
		case Muda_Plugin_Event_Kind_Detection: {
			Interface->LogInfo(Thread, "Plugin Detected!\n");
			Interface->PluginName = "DIO";
		} break;

		case Muda_Plugin_Event_Kind_Prebuild: {
			Interface->LogInfo(Thread, "Plugin Prebuild: %s\n", Config->Name);
		} break;

		case Muda_Plugin_Event_Kind_Postbuild: {
			Interface->LogInfo(Thread, "Plugin Postbuild: %s\n", Config->Name);
		} break;

		case Muda_Plugin_Event_Kind_Destroy: {
			Interface->LogInfo(Thread, "Bye bye... :(\n");
		} break;
	}
}
