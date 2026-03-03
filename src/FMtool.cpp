#include "rack.hpp"

using namespace rack;

Plugin* pluginInstance;

struct FMtool : Module {
    enum ParamIds {
        DEPTH_PARAM,
        DEPTH_CV_ATTEN_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        CARRIER_INPUT,
        MODULATOR_INPUT,
        DEPTH_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        AUDIO_OUTPUT,
        NUM_OUTPUTS
    };

    static const int BUFFER_SIZE = 4096;
    float buffer[BUFFER_SIZE] = {};
    int writeIndex = 0;

    float smoothedDepth = 0.0f;

    FMtool() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

        configParam(DEPTH_PARAM, 0.f, 1.f, 0.f, "FM Depth");
        
        configParam(DEPTH_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "CV Depth Attenuation");

        configInput(CARRIER_INPUT, "Carrier");
        configInput(MODULATOR_INPUT, "Modulator");
        configInput(DEPTH_CV_INPUT, "Depth CV");
        configOutput(AUDIO_OUTPUT, "Audio");
    }

    void process(const ProcessArgs& args) override {
        float carrier = inputs[CARRIER_INPUT].getVoltage();
        float modulator = inputs[MODULATOR_INPUT].getVoltage();
        
        buffer[writeIndex] = carrier;

        float targetDepth = params[DEPTH_PARAM].getValue();
        
        if (inputs[DEPTH_CV_INPUT].isConnected()) {
            float cv = inputs[DEPTH_CV_INPUT].getVoltage() / 5.0f; 
            targetDepth += cv * params[DEPTH_CV_ATTEN_PARAM].getValue();
        }
        
        targetDepth = clamp(targetDepth, 0.0f, 1.0f);

        const float slew = 0.001f;
        smoothedDepth += (targetDepth - smoothedDepth) * slew;

        float nominalDelay = args.sampleRate * 0.005f; 
        float modOffset = modulator * smoothedDepth * 50.0f; 
        float readPos = (float)writeIndex - nominalDelay + modOffset;

        int i_readPos = (int)std::floor(readPos);
        float frac = readPos - i_readPos;

        int mask = BUFFER_SIZE - 1;
        int index1 = i_readPos & mask;
        int index2 = (i_readPos + 1) & mask;

        float val1 = buffer[index1];
        float val2 = buffer[index2];
        
        float out = val1 + frac * (val2 - val1);

        outputs[AUDIO_OUTPUT].setVoltage(out);

        writeIndex = (writeIndex + 1) & mask;
    }
};

// User Interface

struct FMtoolWidget : ModuleWidget {
    FMtoolWidget(FMtool* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/fmtoolpanel.svg")));

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.14, 3.14))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(22.26, 3.14))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.14, 125.36))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(22.26, 125.36))));

        // Parameters
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(12.7, 26.01)), module, FMtool::DEPTH_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(12.7, 73.76)), module, FMtool::DEPTH_CV_ATTEN_PARAM));

        // Inputs
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 44.79)), module, FMtool::CARRIER_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 61.71)), module, FMtool::MODULATOR_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 88.94)), module, FMtool::DEPTH_CV_INPUT));

        // Output
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(12.7, 107.37)), module, FMtool::AUDIO_OUTPUT));
    }
};

Model* modelFMtool = createModel<FMtool, FMtoolWidget>("FMtool");

void init(rack::Plugin* p) {
    pluginInstance = p;
    p->addModel(modelFMtool);
}
