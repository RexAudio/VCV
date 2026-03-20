#include "rack.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

using namespace rack;

extern Plugin* pluginInstance;

struct PWM : Module {
    enum ParamId {
        WIDTH_PARAM,
        WIDTH_MOD_RATE_PARAM,
        WIDTH_MOD_DEPTH_PARAM,
        MIX_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        IN_INPUT,
        VOCT_INPUT,
        WIDTH_MOD_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_OUTPUT,
        OUTPUTS_LEN
    };

    struct Voice {
        std::vector<float> delayBuffer;
        int writePos = 0;

        float freqA = 261.6256f;
        float freqB = 261.6256f;
        float targetFreqA = 261.6256f;
        float targetFreqB = 261.6256f;
        
        int activeLine = 0;
        float fadePos = 1.f;
        float lastVoct = -100.f; 
        float lfoPhase = 0.f;

        float filtIn = 0.f;
        float filtOut = 0.f;

        Voice() {
            delayBuffer.resize(192000, 0.f); 
        }
    };

    Voice voices[16];

    float smoothWidth = 0.5f;
    float smoothDepth = 0.5f;
    float smoothRate = 1.f;
    float smoothMix = 1.f; 

    PWM() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);
        
        configParam(WIDTH_PARAM, 0.01f, 0.99f, 0.5f, "Pulse Width");
        configParam(WIDTH_MOD_RATE_PARAM, 0.1f, 20.f, 1.f, "PWM Rate", " Hz");
        configParam(WIDTH_MOD_DEPTH_PARAM, 0.f, 1.f, 0.5f, "PWM Depth");
        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f);

        configInput(IN_INPUT, "Audio");
        configInput(VOCT_INPUT, "V/Oct");
        configInput(WIDTH_MOD_INPUT, "PWM CV");
        configOutput(OUT_OUTPUT, "Audio");
    }

    void process(const ProcessArgs& args) override {
        float sr = args.sampleRate;

        float paramSlew = 1.f - std::exp(-1.f / (0.01f * sr));
        smoothWidth += (params[WIDTH_PARAM].getValue() - smoothWidth) * paramSlew;
        smoothRate += (params[WIDTH_MOD_RATE_PARAM].getValue() - smoothRate) * paramSlew;
        smoothDepth += (params[WIDTH_MOD_DEPTH_PARAM].getValue() - smoothDepth) * paramSlew;
        smoothMix += (params[MIX_PARAM].getValue() - smoothMix) * paramSlew;

        int inChannels = inputs[IN_INPUT].getChannels();
        int voctChannels = inputs[VOCT_INPUT].getChannels();
        int modChannels = inputs[WIDTH_MOD_INPUT].getChannels();
        
        int channels = std::max({1, inChannels, voctChannels});
        outputs[OUT_OUTPUT].setChannels(channels);

        float filtCoeff = 1.f - std::exp(-2.f * M_PI * 19000.f / sr);
        float crossfadeInc = 1.f / (sr * 0.05f);
        
        float pitchGlideSlew = 1.f - std::exp(-1.f / (0.01f * sr));

        for (int c = 0; c < channels; c++) {
            Voice& v = voices[c];

            float inSig = inputs[IN_INPUT].getVoltage(inChannels == 1 ? 0 : c);
            float voct = inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getVoltage(voctChannels == 1 ? 0 : c) : 0.f;

            float deltaVoct = std::abs(voct - v.lastVoct);
            
            if (deltaVoct > 0.0001f) {
                float newFreq = dsp::FREQ_C4 * std::pow(2.f, voct);
                bool isMidFade = (v.fadePos < 1.0f);
                bool isLargeJump = (deltaVoct >= (1.f / 12.f));

                if (isLargeJump && !isMidFade) {
                    if (v.activeLine == 0) {
                        v.activeLine = 1;
                        v.targetFreqB = newFreq;
                        v.freqB = newFreq; 
                    } else {
                        v.activeLine = 0;
                        v.targetFreqA = newFreq;
                        v.freqA = newFreq; 
                    }
                    v.fadePos = 0.f;
                } else {
                    v.targetFreqA = newFreq;
                    v.targetFreqB = newFreq;
                }
                v.lastVoct = voct;
            }

            v.freqA += (v.targetFreqA - v.freqA) * pitchGlideSlew;
            v.freqB += (v.targetFreqB - v.freqB) * pitchGlideSlew;

            float modVal = 0.f;
            if (inputs[WIDTH_MOD_INPUT].isConnected()) {
                float cvIn = inputs[WIDTH_MOD_INPUT].getVoltage(modChannels == 1 ? 0 : c);
                modVal = clamp(cvIn / 5.f, -1.f, 1.f);
            } else {
                v.lfoPhase += smoothRate / sr;
                if (v.lfoPhase >= 1.f) v.lfoPhase -= 1.f;
                modVal = std::sin(v.lfoPhase * 2.f * M_PI);
            }

            float currentWidth = smoothWidth + (smoothWidth * smoothDepth * modVal);
            currentWidth = clamp(currentWidth, 0.01f, 0.99f);

            v.filtIn += (inSig - v.filtIn) * filtCoeff;
            v.delayBuffer[v.writePos] = v.filtIn;

            if (v.fadePos < 1.f) {
                v.fadePos += crossfadeInc;
                if (v.fadePos > 1.f) v.fadePos = 1.f;
            }

            float delayA = clamp((sr / v.freqA) * currentWidth, 1.f, (float)(v.delayBuffer.size() - 2));
            float delayB = clamp((sr / v.freqB) * currentWidth, 1.f, (float)(v.delayBuffer.size() - 2));

            auto readDelay = [&](float delaySamples) {
                float readPos = v.writePos - delaySamples;
                while (readPos < 0.f) readPos += v.delayBuffer.size();
                
                int idx1 = (int)readPos;
                float frac = readPos - idx1;
                int idx2 = (idx1 + 1) % v.delayBuffer.size();
                
                return v.delayBuffer[idx1] * (1.f - frac) + v.delayBuffer[idx2] * frac;
            };

            float delA = readDelay(delayA);
            float delB = readDelay(delayB);

            float volB = (v.activeLine == 1) ? v.fadePos : (1.f - v.fadePos);
            float volA = 1.f - volB;
            float delFinal = (delA * volA) + (delB * volB);

            v.writePos = (v.writePos + 1) % v.delayBuffer.size();

            // Output Filter (Wet Signal only)
            float rawWet = (inSig - delFinal) * 0.5f;
            v.filtOut += (rawWet - v.filtOut) * filtCoeff;

            // Dry / Wet Mix
            float mixedOut = (inSig * (1.f - smoothMix)) + (v.filtOut * smoothMix);
            outputs[OUT_OUTPUT].setVoltage(mixedOut, c);
        }
    }
};
// --- MODULE WIDGET UI ---
struct PWMWidget : ModuleWidget {
    PWMWidget(PWM* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/PWMpanel.svg")));

        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.14, 3.14))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(27.34, 3.14))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(3.14, 125.36))));
        addChild(createWidgetCentered<ScrewSilver>(mm2px(Vec(27.34, 125.36))));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.24, 20.13)), module, PWM::IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(21.24, 20.13)), module, PWM::VOCT_INPUT));

        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(15.24, 36.41)), module, PWM::WIDTH_PARAM));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(15.24, 52.44)), module, PWM::WIDTH_MOD_RATE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(15.24, 66.36)), module, PWM::WIDTH_MOD_DEPTH_PARAM));
        
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 80.04)), module, PWM::WIDTH_MOD_INPUT));
        
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 94.97)), module, PWM::MIX_PARAM));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(15.24, 110.90)), module, PWM::OUT_OUTPUT));
    }
};

Model* modelPWM = createModel<PWM, PWMWidget>("PWM");
