#include <rack.hpp>
using namespace rack;

Plugin *pluginInstance;

//add modules here:
extern Model *modelFMtool;
extern Model *modelPWM;

void init(rack::Plugin *plugin) {
    pluginInstance = plugin;

    //add modules here:
    plugin->addModel(modelFMtool);
    plugin->addModel(modelPWM);
}
