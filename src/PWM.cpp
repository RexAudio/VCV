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
        MODE_PARAM, 
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

    static const int BUFFER_SIZE = 262144;
    static const int BUFFER_MASK = 262143;

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

        float filtIn = 0.f;
        float filtOut = 0.f;

        float gateFilterState = 0.f;

        Voice() {
            delayBuffer.resize(BUFFER_SIZE, 0.f); 
        }
    };

    Voice voices[16];

    float smoothWidth = 0.5f;
    float smoothDepth = 0.5f;
    float smoothRate = 1.f;
    float smoothMix = 1.f; 

    float globalLfoPhase = 0.f;

    PWM() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);
        
        configParam(WIDTH_PARAM, 0.01f, 0.99f, 0.5f, "Pulse Width");
        configParam(WIDTH_MOD_RATE_PARAM, 0.1f, 20.f, 1.f, "PWM Rate", " Hz");
        configParam(WIDTH_MOD_DEPTH_PARAM, 0.f, 1.f, 0.5f, "PWM Depth");
        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f);
        
        configParam(MODE_PARAM, 0.f, 1.f, 1.f, "Algorithm Mode");

        configInput(IN_INPUT, "Audio");
        configInput(VOCT_INPUT, "V/Oct");
        configInput(WIDTH_MOD_INPUT, "PWM CV");
        configOutput(OUT_OUTPUT, "Audio");
    }

    void process(const ProcessArgs& args) override {
        float sr = args.sampleRate;
        bool isComparatorMode = params[MODE_PARAM].getValue() > 0.5f;

        float paramSlew = 1.f - std::exp2f(-1.f / (0.01f * sr));
        smoothWidth += (params[WIDTH_PARAM].getValue() - smoothWidth) * paramSlew;
        smoothRate += (params[WIDTH_MOD_RATE_PARAM].getValue() - smoothRate) * paramSlew;
        smoothDepth += (params[WIDTH_MOD_DEPTH_PARAM].getValue() - smoothDepth) * paramSlew;
        smoothMix += (params[MIX_PARAM].getValue() - smoothMix) * paramSlew;

        int inChannels = inputs[IN_INPUT].getChannels();
        int voctChannels = inputs[VOCT_INPUT].getChannels();
        int modChannels = inputs[WIDTH_MOD_INPUT].getChannels();
        
        int channels = std::max({1, inChannels, voctChannels});
        outputs[OUT_OUTPUT].setChannels(channels);

        float filtCoeff = 1.f - std::exp2f(-2.f * M_PI * 19000.f / sr);
        float crossfadeInc = 1.f / (sr * 0.05f);
        float pitchGlideSlew = 1.f - std::exp2f(-1.f / (0.01f * sr));
        float gateAlpha = clamp(4.0f * M_PI / sr, 0.0f, 1.0f);

        globalLfoPhase += smoothRate * args.sampleTime;
        if (globalLfoPhase >= 1.f) globalLfoPhase -= 1.f;
        float globalLfoVal = std::sinf(globalLfoPhase * 2.f * M_PI);
        simd::float_4 globalLfoSimd(globalLfoVal);

        bool modConnected = inputs[WIDTH_MOD_INPUT].isConnected();

        for (int c = 0; c < channels; c += 4) {
            
            simd::float_4 inSigSimd = (inChannels == 1) 
                ? simd::float_4(inputs[IN_INPUT].getVoltage(0)) 
                : simd::float_4::load(inputs[IN_INPUT].getVoltages(c));

            simd::float_4 voctSimd = (voctChannels == 1 && inputs[VOCT_INPUT].isConnected())
                ? simd::float_4(inputs[VOCT_INPUT].getVoltage(0))
                : simd::float_4::load(inputs[VOCT_INPUT].getVoltages(c));
            
            simd::float_4 newFreqsSimd = dsp::FREQ_C4 * simd::exp(voctSimd * 0.6931471805599453f);

            simd::float_4 modValSimd;
            if (modConnected) {
                // Handle Mono vs Poly for the Mod input
                modValSimd = (modChannels == 1)
                    ? simd::float_4(inputs[WIDTH_MOD_INPUT].getVoltage(0)) / 5.f
                    : simd::float_4::load(inputs[WIDTH_MOD_INPUT].getVoltages(c)) / 5.f;
                    
                modValSimd = simd::fmax(-1.f, simd::fmin(modValSimd, 1.f)); 
            } else {
                modValSimd = globalLfoSimd;
            }

            simd::float_4 widthSimd = smoothWidth + (smoothWidth * smoothDepth * modValSimd);
            widthSimd = simd::fmax(0.01f, simd::fmin(widthSimd, 0.99f));

            float inSigArray[4];     inSigSimd.store(inSigArray);
            float voctArray[4];      voctSimd.store(voctArray);
            float newFreqArray[4];   newFreqsSimd.store(newFreqArray);
            float widthArray[4];     widthSimd.store(widthArray);
            float rawWetArray[4] =   {0.f, 0.f, 0.f, 0.f};

            for (int i = 0; i < 4; i++) {
                int ch = c + i;
                if (ch >= channels) break; 

                Voice& v = voices[ch];
                float inSig = inSigArray[i];
                float vOctScalar = voctArray[i];
                float newFreq = newFreqArray[i];
                float width = widthArray[i];

                float deltaVoct = std::fabs(vOctScalar - v.lastVoct);
                if (deltaVoct > 0.0001f) {
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
                    v.lastVoct = vOctScalar;
                }

                v.freqA += (v.targetFreqA - v.freqA) * pitchGlideSlew;
                v.freqB += (v.targetFreqB - v.freqB) * pitchGlideSlew;

                if (isComparatorMode) {
                    float threshold = (0.5f - width) * 10.0f; 
                    float pwm_sig = (inSig > threshold) ? 5.0f : -5.0f;

                    float rectified = std::fabs(inSig) / 5.0f;
                    v.gateFilterState += gateAlpha * (rectified - v.gateFilterState);
                    
                    float gate_env = clamp(v.gateFilterState * 100.0f, 0.0f, 1.0f);
                    rawWetArray[i] = pwm_sig * gate_env;
                } else {
                    v.filtIn += (inSig - v.filtIn) * filtCoeff;
                    v.delayBuffer[v.writePos] = v.filtIn;

                    if (v.fadePos < 1.f) {
                        v.fadePos += crossfadeInc;
                        if (v.fadePos > 1.f) v.fadePos = 1.f;
                    }

                    float delayA = clamp((sr / v.freqA) * width, 1.f, (float)(BUFFER_SIZE - 2));
                    float delayB = clamp((sr / v.freqB) * width, 1.f, (float)(BUFFER_SIZE - 2));

                    auto readDelay = [&](float delaySamples) {
                        float readPos = v.writePos - delaySamples;
                        if (readPos < 0.f) readPos += (float)BUFFER_SIZE;
                        
                        int idx1 = (int)readPos;
                        float frac = readPos - (float)idx1;
                        
                        idx1 = idx1 & BUFFER_MASK;
                        int idx2 = (idx1 + 1) & BUFFER_MASK;
                        
                        return v.delayBuffer[idx1] * (1.f - frac) + v.delayBuffer[idx2] * frac;
                    };

                    float delA = readDelay(delayA);
                    float delB = readDelay(delayB);

                    float volB = (v.activeLine == 1) ? v.fadePos : (1.f - v.fadePos);
                    float volA = 1.f - volB;
                    float delFinal = (delA * volA) + (delB * volB);

                    v.writePos = (v.writePos + 1) & BUFFER_MASK;
                    rawWetArray[i] = (inSig - delFinal) * 0.5f;
                }
            }

            simd::float_4 rawWetSimd = simd::float_4::load(rawWetArray);
            
            float filtOutArray[4] = {
                (c < channels) ? voices[c].filtOut : 0.f,
                (c + 1 < channels) ? voices[c + 1].filtOut : 0.f,
                (c + 2 < channels) ? voices[c + 2].filtOut : 0.f,
                (c + 3 < channels) ? voices[c + 3].filtOut : 0.f
            };
            simd::float_4 filtOutSimd = simd::float_4::load(filtOutArray);

            filtOutSimd += (rawWetSimd - filtOutSimd) * filtCoeff;
            
            filtOutSimd.store(filtOutArray);
            for (int i = 0; i < 4; i++) {
                if (c + i < channels) voices[c + i].filtOut = filtOutArray[i];
            }

            simd::float_4 mixedOutSimd = (inSigSimd * (1.f - smoothMix)) + (filtOutSimd * smoothMix);
            mixedOutSimd.store(outputs[OUT_OUTPUT].getVoltages(c));
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

    void appendContextMenu(Menu* menu) override {
        PWM* module = dynamic_cast<PWM*>(this->module);
        if (!module) return;

        menu->addChild(new MenuEntry);
        menu->addChild(createMenuLabel("Algorithm Mode"));
        
        menu->addChild(createCheckMenuItem("Comparator Mode",
            "",
            [module]() { return module->params[PWM::MODE_PARAM].getValue() > 0.5f; },
            [module]() { module->paramQuantities[PWM::MODE_PARAM]->setValue(1.f); }
        ));
        
        menu->addChild(createCheckMenuItem("Delay Mode",
            "",
            [module]() { return module->params[PWM::MODE_PARAM].getValue() < 0.5f; },
            [module]() { module->paramQuantities[PWM::MODE_PARAM]->setValue(0.f); }
        ));
    }
};

Model* modelPWM = createModel<PWM, PWMWidget>("PWM");
