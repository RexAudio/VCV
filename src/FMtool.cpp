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
    static const int MAX_CHANNELS = 16; // VCV Rack max polyphony

    // State variables are now arrays to handle multiple channels independently
    float buffer[MAX_CHANNELS][BUFFER_SIZE] = {};
    float smoothedDepth[MAX_CHANNELS] = {};
    
    // We only need one writeIndex since all channels are processed synchronously at the same sample rate
    int writeIndex = 0;

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
        // 1. Determine the highest channel count patched into the primary inputs (default to 1)
        int channels = std::max({1, inputs[CARRIER_INPUT].getChannels(), inputs[MODULATOR_INPUT].getChannels()});
        
        // 2. Tell the output port how many channels it will be emitting
        outputs[AUDIO_OUTPUT].setChannels(channels);

        // Fetch parameters that apply globally to all channels
        float baseTargetDepth = params[DEPTH_PARAM].getValue();
        float cvAtten = params[DEPTH_CV_ATTEN_PARAM].getValue();
        
        const float slew = 0.001f;
        float nominalDelay = args.sampleRate * 0.005f; 
        int mask = BUFFER_SIZE - 1;

        // 3. Process each channel in a loop
        for (int c = 0; c < channels; ++c) {
            // getPolyVoltage grabs channel 'c', or seamlessly falls back to channel 0 if a mono cable is plugged in
            float carrier = inputs[CARRIER_INPUT].getPolyVoltage(c);
            float modulator = inputs[MODULATOR_INPUT].getPolyVoltage(c);
            
            buffer[c][writeIndex] = carrier;

            float targetDepth = baseTargetDepth;
            
            if (inputs[DEPTH_CV_INPUT].isConnected()) {
                float cv = inputs[DEPTH_CV_INPUT].getPolyVoltage(c) / 5.0f; 
                targetDepth += cv * cvAtten;
            }
            
            targetDepth = clamp(targetDepth, 0.0f, 1.0f);

            // Per-channel slewing to prevent clicks if channels jump
            smoothedDepth[c] += (targetDepth - smoothedDepth[c]) * slew;

            float modOffset = modulator * smoothedDepth[c] * 50.0f; 
            float readPos = (float)writeIndex - nominalDelay + modOffset;

            int i_readPos = (int)std::floor(readPos);
            float frac = readPos - i_readPos;

            int index1 = i_readPos & mask;
            int index2 = (i_readPos + 1) & mask;

            float val1 = buffer[c][index1];
            float val2 = buffer[c][index2];
            
            float out = val1 + frac * (val2 - val1);

            // Output the processed sample to the specific channel 'c'
            outputs[AUDIO_OUTPUT].setVoltage(out, c);
        }

        // Advance the shared write index once per processing frame
        writeIndex = (writeIndex + 1) & mask;
    }
};

// User Interface
// (This section remains exactly as you wrote it!)

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
