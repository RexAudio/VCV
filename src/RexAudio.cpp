#include <rack.hpp>
using namespace rack;

Plugin *pluginInstance;

extern Model *modelFMTool;
extern Model *modelPWM;

void init(rack::Plugin *plugin) {
    pluginInstance = plugin;
    
    plugin->addModel(modelFMTool);
    plugin->addModel(modelPWM);
}
